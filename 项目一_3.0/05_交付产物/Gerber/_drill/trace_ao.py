import re

def parse_coord(s):
    neg = s.startswith('-')
    if neg: s = s[1:]
    s = s.zfill(9)
    return -(int(s[:4]) + int(s[4:])/100000.0) if neg else int(s[:4]) + int(s[4:])/100000.0

def yeda(y): return 80 + y

def load(path):
    apr = {}; pads = []; tracks = []
    cur = None; cx = cy = 0.0
    for line in open(path, encoding='utf-8', errors='ignore'):
        line = line.strip()
        if line.startswith('%ADD'):
            m = re.match(r'%ADD(\d+)([CRO]),([\d.X]+)\*%', line)
            if m:
                apr[int(m.group(1))] = (m.group(2), [float(v) for v in m.group(3).split('X')])
            continue
        if line.startswith('G36') or line.startswith('G37'): continue
        m = re.match(r'^(?:G0?1)?X(-?\d+)Y(-?\d+)D0?([123])\*?$', line)
        if m:
            x = parse_coord(m.group(1)); y = parse_coord(m.group(2)); d = m.group(3)
            if d == '3': pads.append((x, y, cur))
            elif d == '1':
                if cur is not None: tracks.append((cx, cy, x, y, cur))
                cx, cy = x, y
            else: cx, cy = x, y
            continue
        m = re.match(r'^(?:G54)?D(\d+)\*$', line)
        if m: cur = int(m.group(1))
    return apr, pads, tracks

print('### 找 J2 焊盘：Φ1.7~1.8 圆盘，y_eda 28~34 之间 ###')
for name in ['Gerber_TopLayer.GTL', 'Gerber_BottomLayer.GBL']:
    apr, pads, tracks = load(name)
    hits = [(x, y, d, apr.get(d)) for (x, y, d) in pads
            if 28 <= yeda(y) <= 34 and apr.get(d) and apr[d][0] == 'C' and 1.6 <= apr[d][1][0] <= 1.9]
    print(name, '->', [(round(x,2), round(yeda(y),2), a[1][0]) for (x,y,d,a) in hits])

print()
print('### 0.381mm 走线全表（AO1/AO2 候选），坐标已换成 EDA 系 ###')
for name in ['Gerber_TopLayer.GTL', 'Gerber_BottomLayer.GBL']:
    apr, pads, tracks = load(name)
    print('---', name)
    for (x1, y1, x2, y2, d) in tracks:
        ap = apr.get(d)
        if ap and ap[0] == 'C' and abs(ap[1][0] - 0.381) < 0.005:
            print('  (%.2f,%.2f) -> (%.2f,%.2f)' % (x1, yeda(y1), x2, yeda(y2)))
