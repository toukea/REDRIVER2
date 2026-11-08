#!/usr/bin/env python3
"""Render a .rdc cockpit from the driver's eye, PSX style (320x240, 15-bit dither)."""
import struct, sys, numpy as np
from PIL import Image
def load(path):
    b = open(path, 'rb').read(); assert b[:4] == b'RD2C'
    ver, n = struct.unpack_from('<II', b, 4); count = 40 if ver >= 2 else 28
    meta = struct.unpack_from('<%df' % count, b, 12); p = 12 + 4 * count
    ln = b[p]; p += 1 + ln
    atlas = np.frombuffer(b, np.uint8, 256 * 256 * 3, p).reshape(256, 256, 3).astype(np.float32); p += 256 * 256 * 3
    tris = []
    for _ in range(n):
        v = np.array(struct.unpack_from('<9f', b, p)).reshape(3, 3); e = b[p + 36:p + 46]; p += 46
        e = np.frombuffer(e, np.uint8); tris.append((v, e[:6].reshape(3, 2).astype(float), e[6:9].astype(float), int(e[9])))
    return meta, atlas, tris
def needles(meta, speed_kmh, rpm):
    # Same construction as cockpit::needles() in src_rebuild/utils/cockpit_import.h.
    if len(meta) < 40 or meta[31] <= .01: return []
    n = np.array(meta[36:39]); n /= np.linalg.norm(n)
    up = np.array([0, 1., 0]) - n * n[1]; up /= np.linalg.norm(up)
    right = np.cross(up, n); right /= np.linalg.norm(right)
    if right[0] < 0: right = -right
    white = np.array([[128, 252], [129, 252], [128, 253]], float); out = []
    def at(c, r, deg, lift):
        a = np.radians(deg); return c + n * lift + (right * np.cos(a) + up * np.sin(a)) * r
    def tri(P, col): out.append((np.array(P), white, np.array(col, float) / 2, 2))
    for i, value in enumerate((speed_kmh / 200, rpm / 8000)):
        c = np.array(meta[28 + i * 4:31 + i * 4]); r = meta[31 + i * 4] * .92
        for k in range(16): tri((c + n * .001, at(c, r, k * 22.5 + 22.5, .001), at(c, r, k * 22.5, .001)), (232, 222, 190))
        for k in range(9):
            deg = 210 - k * 30; w = 1.2 if k % 2 else 2.2
            tri((at(c, r * .97, deg - w, .002), at(c, r * .97, deg + w, .002), at(c, r * (.84 if k % 2 else .76), deg, .002)), (34, 30, 26))
        a = np.radians(210 - np.clip(value, 0, 1) * 240); d = right * np.cos(a) + up * np.sin(a); side = -right * np.sin(a) + up * np.cos(a)
        h = c + n * .003; w = max(.003, r * .1)
        tri((h - d * r * .2 + side * w, h + d * r * .9, h - d * r * .2 - side * w), (235, 96, 36))
    return out
def render(path, out, yaw=0.0, pitch=-6.0, steer=0.0, W=320, H=240, fov=74, speed=90.0, rpm=4000.0):
    meta, atlas, tris = load(path)
    tris = tris + needles(meta, speed, rpm)
    eye = np.array(meta[18:21]); f = W / 2 / np.tan(np.radians(fov) / 2)
    cy, sy = np.cos(np.radians(yaw)), np.sin(np.radians(yaw)); cp, sp = np.cos(np.radians(pitch)), np.sin(np.radians(pitch))
    img = np.zeros((H, W, 3), np.float32); img[:] = (120, 150, 185); zb = np.full((H, W), 1e9)
    L = np.array([-.3, 1, .2]); L /= np.linalg.norm(L)
    for P, UV, col, part in tris:
        v = P - eye
        x = v[:, 0] * cy - v[:, 2] * sy; z0 = v[:, 0] * sy + v[:, 2] * cy; y0 = v[:, 1]
        z = z0 * cp + y0 * sp; y = y0 * cp - z0 * sp  # negative pitch looks down
        if z.min() < .03: continue
        n = np.cross(P[1] - P[0], P[2] - P[0]); ln = np.linalg.norm(n)
        if ln == 0: continue
        shade = .72 + .28 * abs(n @ L / ln)
        pts = np.c_[W / 2 + x / z * f, H / 2 - y / z * f]
        x0, y0_ = np.maximum(np.floor(pts.min(0)).astype(int), 0); x1, y1 = np.minimum(np.ceil(pts.max(0)).astype(int), [W - 1, H - 1])
        if x0 > x1 or y0_ > y1: continue
        ys, xs = np.mgrid[y0_:y1 + 1, x0:x1 + 1] + .5
        (a1, b1), (a2, b2), (a3, b3) = pts; den = (b2 - b3) * (a1 - a3) + (a3 - a2) * (b1 - b3)
        if abs(den) < 1e-9: continue
        l1 = ((b2 - b3) * (xs - a3) + (a3 - a2) * (ys - b3)) / den; l2 = ((b3 - b1) * (xs - a3) + (a1 - a3) * (ys - b3)) / den; l3 = 1 - l1 - l2
        ins = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        if not ins.any(): continue
        zz = 1 / (l1 / z[0] + l2 / z[1] + l3 / z[2])
        yy = ys[ins].astype(int); xx = xs[ins].astype(int); zi = zz[ins]; ok = zi < zb[yy, xx]
        if not ok.any(): continue
        u = ((l1 * UV[0, 0] / z[0] + l2 * UV[1, 0] / z[1] + l3 * UV[2, 0] / z[2]) * zz)[ins][ok]
        w = ((l1 * UV[0, 1] / z[0] + l2 * UV[1, 1] / z[1] + l3 * UV[2, 1] / z[2]) * zz)[ins][ok]
        texel = atlas[np.clip(w, 0, 255).astype(int), np.clip(u, 0, 255).astype(int)]
        img[yy[ok], xx[ok]] = np.clip(texel * col / 128 * shade, 0, 255); zb[yy[ok], xx[ok]] = zi[ok]
    bayer = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]])
    o = (np.clip(img + bayer[np.arange(H) % 4][:, np.arange(W) % 4][..., None], 0, 255).astype(np.uint8) >> 3) << 3
    Image.fromarray(o).resize((W * 2, H * 2), Image.NEAREST).save(out)
if __name__ == '__main__':
    render(sys.argv[1], sys.argv[2], *(float(a) for a in sys.argv[3:]))
