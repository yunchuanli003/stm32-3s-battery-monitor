import re, sys, math

TARGET = (85.49894, -54.54904)   # AO1 换层孔
R = 0.9

def parse_coord(s):
    neg = s.startswith('-')
    if neg: s = s[1:]
    s = s.zfill(9)
    return -(int(s[:4]) + int(s[4:])/100000.0) if neg else int(s[:4]) + int(s[4:])/100000.0

def load(path):
    apertures = {}
    pads = []      # (x,y,dcode,shape,size)
    tracks = []    # (x1,y1,x2,y2,dcode,width)
    cur_d = None
    cur_w = 0
    cx = cy = 0.0
    in_region = False
    regions = 0
    for line in open(path, encoding='utf-8', errors='ignore'):
        line = line.strip()
        if line.startswith('%ADD'):
            m = re.match(r'%ADD(\d+)([CRO]),([\d.X]+)\*%', line)
            if m:
                d = int(m.group(1)); shape = m.group(2)
                dims = [float(v) for v in m.group(3).split('X')]
                apertures[d] = (shape, dims)
            continue
        if line.startswith('G36'): in_region = True; regions += 1; continue
        if line.startswith('G37'): in_region = False; continue
        m = re.match(r'^(?:G0?1)?X(-?\d+)Y(-?\d+)D0?([123])\*?$', line)
        if m:
            x = parse_coord(m.group(1)); y = parse_coord(m.group(2)); d = m.group(3)
            if d == '3':
                pads.append((x, y, cur_d))
            elif d == '1':
                if cur_d is not None:
                    tracks.append((cx, cy, x, y, cur_d))
                cx, cy = x, y
            else:
                cx, cy = x, y
            continue
        m = re.match(r'^(?:G54)?D(\d+)\*$', line)
        if m:
            cur_d = int(m.group(1))
            ap = apertures.get(cur_d)
            cur_w = ap[1][0] if ap and ap[0] == 'C' else 0
            continue
    return apertures, pads, tracks, regions

def near(x, y):
    return abs(x-TARGET[0]) < R and abs(y-TARGET[1]) < R

for name in ['Gerber_TopLayer.GTL', 'Gerber_BottomLayer.GBL']:
    ap, pads, tracks, regions = load(name)
    print('=' * 60)
    print(name, '| apertures:', len(ap), '| flashes:', len(pads), '| tracks:', len(tracks), '| regions:', regions)
    print('-- 目标点 0.9mm 内的 flash --')
    hit = 0
    for (x, y, d) in pads:
        if near(x, y):
            hit += 1
            print('   flash at (%.4f, %.4f) D%d ap=%s' % (x, y, d, ap.get(d)))
    if not hit: print('   (none)')
    print('-- 目标点 0.9mm 内的走线端点 --')
    hit = 0
    for (x1, y1, x2, y2, d) in tracks:
        if near(x1, y1) or near(x2, y2):
            hit += 1
            w = ap.get(d, ('?', [0]))[1][0]
            print('   (%.3f,%.3f)->(%.3f,%.3f)  w=%.3fmm' % (x1, y1, x2, y2, w))
    if not hit: print('   (none)')
