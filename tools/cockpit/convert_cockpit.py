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
    a.add_argument('--atlas', type=int, default=512, choices=(256, 512), help='atlas size; the cockpit fills the screen, 512 keeps the instruments legible')
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
    S = o.atlas; SH = S * 248 // 256  # bottom 8/256 of the atlas: white strip for flat colours
    crop = src.crop((int(u0 * W), int(v0 * H), int(u1 * W), int(v1 * H))).resize((S, SH), Image.LANCZOS)
    t = np.asarray(crop).astype(np.float32) * o.lift
    tint = TRIMS[o.trim]
    if tint is not None:
        lum = t.mean(-1, keepdims=True); sat = (t.max(-1, keepdims=True) - t.min(-1, keepdims=True)) / (t.max(-1, keepdims=True) + 1)
        neutral = np.clip(1 - sat * 4, 0, 1)
        t = t * (1 - neutral) + (lum / 255.0) * np.array(tint) * 1.6 * neutral
    atlas = np.zeros((S, S, 3), np.float32); atlas[:SH] = t; atlas[SH:] = 255
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
    every_face = []  # all driver-facing interior surfaces beyond the hub (lens, hood, faces)
    for p in interior:
        P = frame(p['pos'], o.forward)
        for f in p['idx']:
            T = P[f[FLIP]]; n = np.cross(T[1] - T[0], T[2] - T[0]); ln = np.linalg.norm(n)
            if ln and n[2] / ln < -.2: every_face.append(T)
    every_face = np.concatenate(every_face) if every_face else np.zeros((0, 3))
    def surface_z(x, y, r=.035):
        # Needles must lie in front of whatever the driver sees over the dial
        # (cluster lens or hood included): nearest surface overlapping the dial.
        far = every_face[every_face[:, 2] > centre[2] + .08]
        near = far[np.hypot(far[:, 0] - x, far[:, 1] - y) < r * .9] if len(far) else far
        if len(near): return near[:, 2].min()
        d = np.hypot(far[:, 0] - x, far[:, 1] - y) if len(far) else np.array([])
        return far[d.argmin(), 2] if len(d) else centre[2] + .2
    # Ray cast from the driver's eye: the first interior surface hit beyond the
    # wheel is what the driver actually sees (lens, hood or dial face).
    TV = np.array([P[f[FLIP]] for p in interior for P in [frame(p['pos'], o.forward)] for f in p['idx']])
    def raycast(target):
        d = target - eye; d = d / np.linalg.norm(d)
        e1 = TV[:, 1] - TV[:, 0]; e2 = TV[:, 2] - TV[:, 0]; pv = np.cross(d, e2); det = (e1 * pv).sum(1)
        ok = np.abs(det) > 1e-9; inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
        tv = eye - TV[:, 0]; u = (tv * pv).sum(1) * inv; qv = np.cross(tv, e1); v = (qv @ d) * inv; t = (e2 * qv).sum(1) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0)
        pts = eye + np.outer(t, d); hit &= pts[:, 2] > centre[2] + .10  # beyond hub and column
        if not hit.any(): return None, None
        k = np.where(hit)[0][t[hit].argmin()]; n = np.cross(e1[k], e2[k]); n /= np.linalg.norm(n)
        if n[2] > 0: n = -n
        return pts[k] - d * .006, n
    def painted_dials():
        # Render what the driver sees of the cluster with the full-resolution
        # texture, find the painted dials as circles on screen (OpenCV Hough),
        # pick the matching pair, and cast rays back to the dash for 3D centres.
        try:
            import cv2
        except ImportError:
            return None
        look = centre + np.array([0, radius * .5, .15]) - eye; look /= np.linalg.norm(look)
        right = np.cross(np.array([0, 1., 0]), look); right /= np.linalg.norm(right)
        if right[0] < 0: right = -right
        up = np.cross(look, right); up = up if up[1] > 0 else -up
        W, H = 960, 640; foc = W / 2 / np.tan(np.radians(34) / 2)
        texf = np.asarray(src).astype(np.float32); TH2, TW2 = texf.shape[:2]
        img = np.zeros((H, W, 3), np.float32); zb = np.full((H, W), 1e9)
        for p in interior:
            P = frame(p['pos'], o.forward)
            for fidx in p['idx']:
                fidx = fidx[FLIP]; V = P[fidx] - eye; UV = p['uv'][fidx]
                z = V @ look; x = V @ right; y = V @ up
                if z.min() < .05: continue
                pts = np.c_[W / 2 + x / z * foc, H / 2 - y / z * foc]
                x0, y0 = np.maximum(np.floor(pts.min(0)).astype(int), 0); x1, y1 = np.minimum(np.ceil(pts.max(0)).astype(int), [W - 1, H - 1])
                if x0 > x1 or y0 > y1: continue
                ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1] + .5
                (a1, b1), (a2, b2), (a3, b3) = pts; den = (b2 - b3) * (a1 - a3) + (a3 - a2) * (b1 - b3)
                if abs(den) < 1e-12: continue
                l1 = ((b2 - b3) * (xs - a3) + (a3 - a2) * (ys - b3)) / den; l2 = ((b3 - b1) * (xs - a3) + (a1 - a3) * (ys - b3)) / den; l3 = 1 - l1 - l2
                ins = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
                if not ins.any(): continue
                zz = 1 / (l1 / z[0] + l2 / z[1] + l3 / z[2]); yy = ys[ins].astype(int); xx = xs[ins].astype(int); zi = zz[ins]; ok = zi < zb[yy, xx]
                if not ok.any(): continue
                u = ((l1 * UV[0, 0] / z[0] + l2 * UV[1, 0] / z[1] + l3 * UV[2, 0] / z[2]) * zz)[ins][ok]
                v = ((l1 * UV[0, 1] / z[0] + l2 * UV[1, 1] / z[1] + l3 * UV[2, 1] / z[2]) * zz)[ins][ok]
                img[yy[ok], xx[ok]] = texf[((v % 1) * (TH2 - 1)).astype(int), ((u % 1) * (TW2 - 1)).astype(int)]; zb[yy[ok], xx[ok]] = zi[ok]
        g = cv2.GaussianBlur(cv2.cvtColor(img.clip(0, 255).astype(np.uint8), cv2.COLOR_RGB2GRAY), (5, 5), 1.2)
        found = cv2.HoughCircles(g, cv2.HOUGH_GRADIENT, dp=1.2, minDist=30, param1=80, param2=24, minRadius=14, maxRadius=110)
        if found is None: return None
        C = found[0][:12]; best = None
        for i in range(len(C)):
            for j in range(i + 1, len(C)):
                (xa, ya, ra), (xb, yb, rb) = C[i], C[j]; rm = (ra + rb) / 2
                if abs(ya - yb) > .25 * rm or abs(ra - rb) > .2 * rm or abs(xa - xb) < 1.6 * rm: continue
                score = ra + rb - 3 * abs(ra - rb) - 3 * abs(ya - yb)
                if best is None or score > best[0]: best = (score, C[i], C[j])
        if best is None: return None
        out, normals = [], []
        for cx, cy, r in best[1:]:
            d = look * foc + right * (cx - W / 2) - up * (cy - H / 2)
            hit, hn = raycast(eye + d)
            if hit is None: return None
            depth = (hit - eye) @ look
            # Needle colour: the light paint of the dial (ticks and numerals).
            yy, xx = np.mgrid[int(max(0, cy - r)):int(min(H, cy + r)), int(max(0, cx - r)):int(min(W, cx + r))]
            ring = img[yy, xx][((xx - cx) ** 2 + (yy - cy) ** 2 < (r * .9) ** 2)]
            lum = ring.mean(1); paint = ring[lum >= np.percentile(lum, 97)].mean(0) if len(ring) else np.array([220, 210, 190])
            out.append((hit[0], hit[1], hit[2], r * depth / foc * .95, paint)); normals.append(hn)
        out.sort(key=lambda g: abs(g[0] - centre[0]))  # speedometer nearest the column
        return out, np.mean(normals, 0)

    dials.sort(key=lambda d: -d[2]); real = len(dials) >= 2
    needle_colour = np.array([235, 225, 200])
    if real:
        big = [d for d in dials if d[2] > dials[0][2] * .85][:2]
        if len(big) < 2: big = dials[:2]
        big.sort(key=lambda d: abs(d[0] - centre[0]))  # speedometer nearest the column
        gauges = []
        for x, y, r in big:
            hit, _ = raycast(np.array([x, y, surface_z(x, y, r)]))
            z = surface_z(x, y, r) - .006
            gauges.append((x, y, min(z, hit[2]) if hit is not None else z, r))
    else:
        painted = painted_dials()
        gauges, normals = [], []
    if not real and painted:
        gauges = [g[:4] for g in painted[0]]; cand_n = np.array([painted[1]]); real = True
        paint = np.mean([g[4] for g in painted[0]], 0)
        needle_colour = np.clip(paint * 230 / max(1, paint.max()), 0, 255)  # same hue, readable brightness
    elif not real:
        # Virtual pair seen through the upper half of the wheel, either side of the column.
        for dx in (-.065, .065):
            hit, hn = raycast(centre + np.array([dx, radius * .62, 0]))
            if hit is not None: gauges.append((hit[0], hit[1], hit[2], .04)); normals.append(hn)
        if len(gauges) < 2:
            gz = min(surface_z(centre[0] + dx, centre[1] + radius * .45) for dx in (-.07, .07)) - .006
            gauges = [(centre[0] + dx, centre[1] + radius * .45, gz, .034) for dx in (-.07, .07)]
        else:  # one shared dial plane, at the nearer of the two hits
            z = min(g[2] for g in gauges); gauges = [(g[0], g[1], z, g[3]) for g in gauges]
            cand_n = np.array(normals)
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
        meta += [float(v) for v in needle_colour]  # [21..23] needle paint (0-255)
        meta += [float(S)]  # [24] atlas size (0 or 256 in older files)
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
          f'{"dials" if real else "virtual dials"} {[tuple(round(float(v), 3) for v in g) for g in gauges]} needles {needle_colour.astype(int).tolist()}, '
          f'car {hi-lo} m, roof {roof_y:.2f} front {roof_front:.2f}, wheel {centre.round(3)} r={radius:.3f} axis {axis.round(2)}, seat {seat.round(3)}')

if __name__ == '__main__':
    main()
