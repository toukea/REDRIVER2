#!/usr/bin/env python3
"""Convert a glTF/GLB car with interior into a REDRIVER2 cockpit (.rdc, version 2).

The result keeps the PlayStation look of Driver 2: one 256x256 atlas, 8-bit UVs,
15-bit colours with ordered dither, a few thousand triangles. Only the cabin is
kept: interior mesh, steering wheel (rotated in game) and the inside of the body
shell (roof, pillars, doors, floor) seen from the driver's seat.

Source frame: glTF, +Y up. Use --forward to name the source forward axis.
Cockpit frame written to the file: X right, Y up, Z forward, metres.
"""
import argparse, os, struct, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gltf_reader

TRIMS = {  # period trim colours applied to neutral texels only
    'black': None, 'tan': (176, 132, 86), 'red': (150, 52, 44), 'blue': (70, 88, 140),
    'green': (78, 108, 78), 'white': (205, 198, 182)}

def frame(pos, forward):
    # glTF is right-handed with +Y up. Return X right, Y up, Z forward: a
    # left-handed frame, so every face list must also be reversed (FLIP).
    if forward == '+x':   return np.c_[pos[:, 2], pos[:, 1], pos[:, 0]]
    if forward == '-x':   return np.c_[-pos[:, 2], pos[:, 1], -pos[:, 0]]
    if forward == '+z':   return np.c_[-pos[:, 0], pos[:, 1], pos[:, 2]]
    if forward == '-z':   return np.c_[pos[:, 0], pos[:, 1], -pos[:, 2]]
    raise SystemExit('bad --forward')

FLIP = [0, 2, 1]  # keep front faces front after the handedness change

def dither15(img):
    h, w = img.shape[:2]
    bayer = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]])
    d = bayer[np.arange(h) % 4][:, np.arange(w) % 4][..., None]
    return (np.clip(img + d, 0, 255).astype(np.uint8) >> 3) << 3

def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('source'); a.add_argument('output')
    a.add_argument('--forward', default='+x', help='source forward axis: +x -x +z -z')
    a.add_argument('--interior', default='Interior', help='substring of interior mesh names (comma list)')
    a.add_argument('--wheel', default='Steering', help='substring of steering wheel mesh names')
    a.add_argument('--shell', default='Body,Bottom', help='body meshes whose inside forms the cabin (use Bottom alone when the interior already has its roof and pillars)')
    a.add_argument('--trim', default='tan', choices=sorted(TRIMS))
    a.add_argument('--lift', type=float, default=1.35, help='brightness lift for dark sources')
    a.add_argument('--shell-color', default='46,42,38', help='roof/pillar colour seen from inside')
    a.add_argument('--floor-color', default='38,34,30')
    a.add_argument('--credit', default='')
    a.add_argument('--shell-budget', type=int, default=900, help='max triangles for roof, pillars, doors and floor')
    o = a.parse_args()

    prims, mats = gltf_reader.load(o.source)
    pick = lambda p, keys: any(k and k.lower() in (p['node'] + ' ' + p['mesh']).lower() for k in keys.split(','))
    interior = [p for p in prims if pick(p, o.interior)]
    wheel = [p for p in prims if pick(p, o.wheel)]
    shell = [p for p in prims if pick(p, o.shell) and p not in interior and p not in wheel]
    if not interior or not wheel: raise SystemExit('interior or steering wheel mesh not found')
    every = np.concatenate([frame(p['pos'], o.forward) for p in prims])
    lo, hi = every.min(0), every.max(0)

    # Texture: the interior material image; crop to the UV region actually used.
    img_path = mats[interior[0]['mat']]['image']
    uv_all = np.concatenate([p['uv'] for p in interior + wheel])
    u0, v0 = np.floor(uv_all.min(0) * 8) / 8; u1, v1 = np.ceil(uv_all.max(0) * 8) / 8
    src = Image.open(img_path).convert('RGB'); W, H = src.size
    crop = src.crop((int(u0 * W), int(v0 * H), int(u1 * W), int(v1 * H))).resize((256, 248), Image.LANCZOS)
    t = np.asarray(crop).astype(np.float32) * o.lift
    tint = TRIMS[o.trim]
    if tint is not None:
        lum = t.mean(-1, keepdims=True); sat = (t.max(-1, keepdims=True) - t.min(-1, keepdims=True)) / (t.max(-1, keepdims=True) + 1)
        neutral = np.clip(1 - sat * 4, 0, 1)
        t = t * (1 - neutral) + (lum / 255.0) * np.array(tint) * 1.6 * neutral
    atlas = np.zeros((256, 256, 3), np.float32); atlas[:248] = t; atlas[248:] = 255
    atlas = dither15(atlas)

    def uv8(uv):
        u = (uv[:, 0] - u0) / (u1 - u0); v = (uv[:, 1] - v0) / (v1 - v0)
        return np.c_[np.clip(np.round(u * 255), 0, 255), np.clip(np.round(v * 247), 0, 247)].astype(np.uint8)

    # Steering wheel: centre, axis (towards the driver), radius.
    wp = np.concatenate([frame(p['pos'], o.forward) for p in wheel])
    centre = (wp.min(0) + wp.max(0)) / 2
    _, _, vt = np.linalg.svd(wp - centre); axis = vt[2]
    if axis[2] > 0: axis = -axis
    radius = np.percentile(np.linalg.norm((wp - centre) - np.outer((wp - centre) @ axis, axis), axis=1), 98)
    # Driver seat cushion: highest interior surface behind the wheel, below its rim.
    ip = np.concatenate([frame(p['pos'], o.forward) for p in interior])
    seat_zone = ip[(np.abs(ip[:, 0] - centre[0]) < .13) & (ip[:, 2] < centre[2] - .30) & (ip[:, 2] > centre[2] - .85) & (ip[:, 1] < centre[1] - .05)]
    seat = np.array([centre[0], np.percentile(seat_zone[:, 1], 90) if len(seat_zone) else centre[1] - .35, centre[2] - .55])
    eye = np.array([centre[0], seat[1] + .64, centre[2] - .62])  # 1.80 m adult, reclined seat

    # Instruments: chrome dial rings behind the wheel (RANSAC circle fit on bright,
    # driver-facing surfaces); otherwise two virtual dials either side of the column.
    tex_l = np.asarray(src.convert('L')).astype(float); TH, TW = tex_l.shape
    cand, cand_b, cand_n = [], [], []
    for p in interior:
        P = frame(p['pos'], o.forward)
        for f in p['idx']:
            f = f[FLIP]; T = P[f]; c = T.mean(0)
            if abs(c[0] - centre[0]) > .32 or not (centre[1] - .08 < c[1] < centre[1] + .30) or not (centre[2] + .02 < c[2] < centre[2] + .45): continue
            n = np.cross(T[1] - T[0], T[2] - T[0]); ln = np.linalg.norm(n)
            if ln == 0 or n[2] / ln > -.4: continue
            uv = p['uv'][f].mean(0)
            cand.append(c); cand_n.append(n / ln); cand_b.append(tex_l[int((uv[1] % 1) * (TH - 1)), int((uv[0] % 1) * (TW - 1))])
    cand = np.array(cand); cand_b = np.array(cand_b); cand_n = np.array(cand_n)
    dials = []
    if len(cand) > 20:
        q = cand[cand_b >= max(np.percentile(cand_b, 55), 35)][:, :2].copy(); rng = np.random.default_rng(1)
        for _ in range(4):
            best = None
            for _ in range(3000):
                if len(q) < 10: break
                a_, b_, c_ = q[rng.choice(len(q), 3, replace=False)]
                d = 2 * (a_[0] * (b_[1] - c_[1]) + b_[0] * (c_[1] - a_[1]) + c_[0] * (a_[1] - b_[1]))
                if abs(d) < 1e-9: continue
                ux = ((a_ @ a_) * (b_[1] - c_[1]) + (b_ @ b_) * (c_[1] - a_[1]) + (c_ @ c_) * (a_[1] - b_[1])) / d
                uy = ((a_ @ a_) * (c_[0] - b_[0]) + (b_ @ b_) * (a_[0] - c_[0]) + (c_ @ c_) * (b_[0] - a_[0])) / d
                r = np.hypot(a_[0] - ux, a_[1] - uy)
                if not .02 < r < .08: continue
                inl = np.abs(np.hypot(q[:, 0] - ux, q[:, 1] - uy) - r) < .005
                ang = np.arctan2(q[inl, 1] - uy, q[inl, 0] - ux)
                score = inl.sum() if len(np.unique(((ang + np.pi) // (np.pi / 6)).astype(int))) >= 9 else 0
                if best is None or score > best[0]: best = (score, ux, uy, r, inl)
            if best is None or best[0] < 10: break
            dials.append((best[1], best[2], best[3])); q = q[~best[4]]
    def surface_z(x, y):
        # Instrument faces sit well beyond the column shroud and the hub.
        far = cand[cand[:, 2] > centre[2] + .12] if len(cand) else cand
        d = np.hypot(far[:, 0] - x, far[:, 1] - y) if len(far) else np.array([])
        return far[d.argmin(), 2] if len(d) else centre[2] + .2
    dials.sort(key=lambda d: -d[2]); real = len(dials) >= 2
    if real:
        big = [d for d in dials if d[2] > dials[0][2] * .85][:2]
        if len(big) < 2: big = dials[:2]
        big.sort(key=lambda d: abs(d[0] - centre[0]))  # speedometer nearest the column
        gauges = [(x, y, surface_z(x, y) - .004, r) for x, y, r in big]
    else:
        gy = centre[1] + radius * .45
        gz = min(surface_z(centre[0] + dx, gy) for dx in (-.07, .07)) - .004  # nearest visible face
        gauges = [(centre[0] + dx, gy, gz, .035) for dx in (-.07, .07)]
    dial_n = cand_n.mean(0) if len(cand_n) else np.array([0, .3, -.95]); dial_n /= np.linalg.norm(dial_n)

    def visible(P):
        # Interior surfaces are single sided: drop faces whose back is towards the
        # seated driver's head (never seen from the driver's seat).
        n = np.cross(P[1] - P[0], P[2] - P[0]); ln = np.linalg.norm(n)
        if ln == 0: return False
        return (eye - P.mean(0)) @ (n / ln) > -0.03 or np.linalg.norm(P.mean(0) - eye) < 0.25

    tris = []  # (positions[3,3], uv[3,2] uint8, rgb, part)
    for part, group in ((0, interior), (1, wheel)):
        for p in group:
            P = frame(p['pos'], o.forward); U = uv8(p['uv'])
            for f in p['idx']:
                f = f[FLIP]
                if part == 1 or (visible(P[f])): tris.append((P[f], U[f], (128, 128, 128), part))
    ibox_lo = np.concatenate([frame(p['pos'], o.forward) for p in interior]).min(0)
    ibox_hi = np.concatenate([frame(p['pos'], o.forward) for p in interior]).max(0)
    shell_col = tuple(int(c) for c in o.shell_color.split(',')); floor_col = tuple(int(c) for c in o.floor_color.split(','))
    white = np.array([[128, 252]] * 3, np.uint8)
    roof_pts = []; shell_tris = {True: [], False: []}
    for p in shell:
        P = frame(p['pos'], o.forward)
        floor = 'bottom' in (p['node'] + p['mesh']).lower()
        for f in p['idx']:
            f = f[FLIP]; c = P[f].mean(0)
            if not (ibox_lo[0] - .08 < c[0] < ibox_hi[0] + .08 and ibox_lo[2] - .05 < c[2] < ibox_hi[2] + .05): continue
            if c[1] < ibox_lo[1] - .12: continue
            # Underbody meshes also carry engine bay parts: only the floor pan is cabin.
            if floor and c[1] > seat[1] + .03: continue
            shell_tris[floor].append(P[f])
            if not floor and abs(c[0] - (ibox_lo[0] + ibox_hi[0]) / 2) < (ibox_hi[0] - ibox_lo[0]) * .3: roof_pts.append(P[f])
    total_shell = sum(len(v) for v in shell_tris.values())
    for floor, group in shell_tris.items():
        if not group: continue
        T = np.array(group); keep = max(16, int(o.shell_budget * len(T) / max(1, total_shell)))
        if len(T) > keep:
            try:
                import fast_simplification as fs
                q = np.round(T.reshape(-1, 3) * 1000).astype(np.int64)
                uq, idx = np.unique(q, axis=0, return_inverse=True)
                V = uq / 1000.0; F = idx.reshape(-1, 3)
                F = F[(F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])]
                V2, F2 = fs.simplify(V.astype(np.float32), F.astype(np.int32), target_reduction=1 - keep / len(F))
                T = V2[F2]
            except ImportError:
                T = T[np.linspace(0, len(T) - 1, keep).astype(int)]
        col = floor_col if floor else shell_col
        for P in T: tris.append((P, white, tuple(v // 2 for v in col), 0))
    # Roof: highest shell points above the cabin; front edge = windshield header.
    # Roof over the front seats; its front edge is the windscreen header.
    rp = np.concatenate(roof_pts) if roof_pts else every
    near = rp[(rp[:, 2] > seat[2] - .6) & (rp[:, 2] < centre[2] + 1.2)]
    if len(near) > 8: rp = near
    roof_y = np.percentile(rp[:, 1], 97)
    roof_front = rp[rp[:, 1] > roof_y - .06][:, 2].max()

    with open(o.output, 'wb') as f:
        f.write(b'RD2C'); f.write(struct.pack('<II', 2, len(tris)))
        meta = [*lo, *hi, roof_y, roof_front, *seat, *centre, *axis, radius, *eye]
        meta += [0.0] * (28 - len(meta))  # version 1 header: 28 floats
        for g in gauges: meta += list(map(float, g))  # speedometer, tachometer: centre, radius
        meta += [*map(float, dial_n), 1.0 if real else 0.0]  # dial face normal, detected rings
        f.write(struct.pack('<%df' % len(meta), *meta))
        credit = o.credit.encode('utf8')[:255]; f.write(struct.pack('<B', len(credit))); f.write(credit)
        f.write(atlas.tobytes())
        for P, U, col, part in tris:
            f.write(struct.pack('<9f', *P.reshape(-1))); f.write(U.astype(np.uint8).tobytes())
            f.write(struct.pack('<4B', col[0], col[1], col[2], part))
    Image.fromarray(atlas).save(os.path.splitext(o.output)[0] + '_atlas.png')
    print(f'{o.output}: {len(tris)} triangles ({sum(1 for t in tris if t[3]==1)} wheel), '
          f'{"dials" if real else "virtual dials"} {[tuple(round(v, 3) for v in g) for g in gauges]}, '
          f'car {hi-lo} m, roof {roof_y:.2f} front {roof_front:.2f}, wheel {centre.round(3)} r={radius:.3f} axis {axis.round(2)}, seat {seat.round(3)}')

if __name__ == '__main__':
    main()
