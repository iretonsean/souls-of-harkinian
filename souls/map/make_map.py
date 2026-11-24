#!/usr/bin/env python3
"""Render the schematic Souls of Harkinian map of Hyrule.

Reads hyrule.json (next to this file) and writes hyrule-chests.svg, plus hyrule-chests.png when
rsvg-convert is installed. Everything is drawn from simple shapes in code: no game textures,
sprites or traced maps.

usage: python3 make_map.py [--scale 2]
"""
import json, math, os, random, shutil, subprocess, sys
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = json.load(open(os.path.join(HERE, 'hyrule.json')))
MW, MH = DATA['size']
TOP = 78            # title band above the map
LEG = 400           # legend band below the map
W, H = MW, TOP + MH + LEG
FONT = "'Noto Serif', 'Liberation Serif', Georgia, serif"

PAPER, INK, INK2 = '#efe2bf', '#4a3622', '#7a6040'
COL = {
    'land': '#e3d2a2', 'coast': '#8b6c45',
    'desert': '#e8cd86', 'canyon': '#bf9563', 'field': '#a8c46d', 'grove': '#93b862',
    'mountain': '#a37d55', 'village': '#d8c494', 'rock': '#a8b5a6', 'forest': '#5e8c4c',
    'meadow': '#86b366', 'water': '#5fb0b0', 'lava': '#d4573b',
    'river': '#5fa3b0', 'road': '#c9a45f',
}

def blob(c, r, seed, n=16, wob=0.13):
    rnd = random.Random(seed)
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        k = 1 + wob * (rnd.random() * 2 - 1)
        pts.append((c[0] + r[0] * k * math.cos(a), c[1] + r[1] * k * math.sin(a)))
    return smooth(pts, closed=True)

def smooth(pts, closed=False):
    """Catmull-Rom spline through the points, as an SVG path."""
    if len(pts) < 3 and not closed:
        return 'M%.1f %.1f L%.1f %.1f' % (*pts[0], *pts[-1])
    P = pts + pts[:3] if closed else [pts[0]] + pts + [pts[-1]]
    rng = range(len(pts)) if closed else range(len(pts) - 1)
    off = 0 if closed else 0
    d = 'M%.1f %.1f' % P[1 if closed else 1]
    for i in rng:
        p0, p1, p2, p3 = P[i + off], P[i + 1], P[i + 2], P[i + 3] if i + 3 < len(P) else P[-1]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        d += ' C%.1f %.1f %.1f %.1f %.1f %.1f' % (*c1, *c2, *p2)
    return d + (' Z' if closed else '')

def inside(c, r, x, y, k=0.72):
    return ((x - c[0]) / r[0]) ** 2 + ((y - c[1]) / r[1]) ** 2 < k

def texture(t):
    """Small glyphs that show the terrain kind: peaks, trees, dunes, waves."""
    c, r, kind = t['c'], t['r'], t['kind']
    rnd = random.Random(t['seed'] * 7)
    out = []
    step = {'mountain': 26, 'canyon': 24, 'forest': 22, 'grove': 24, 'desert': 30, 'water': 26, 'rock': 30}.get(kind)
    if not step:
        return ''
    y = c[1] - r[1]
    row = 0
    while y < c[1] + r[1]:
        x = c[0] - r[0] + (step / 2 if row % 2 else 0)
        while x < c[0] + r[0]:
            jx, jy = x + rnd.uniform(-5, 5), y + rnd.uniform(-4, 4)
            if inside(c, r, jx, jy):
                if kind in ('mountain', 'canyon', 'rock'):
                    s = rnd.uniform(7, 11) if kind != 'rock' else 6
                    shade = '#6e5236' if kind != 'rock' else '#6d7d70'
                    out.append('<path d="M%.1f %.1f l%.1f %.1f l%.1f %.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-linejoin="round" stroke-linecap="round"/>'
                               % (jx - s, jy + s * 0.6, s, -s * 1.2, s, s * 1.2, shade))
                elif kind in ('forest', 'grove'):
                    s = rnd.uniform(5, 7.5)
                    out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="#3f6634" stroke-width="1"/>'
                               % (jx, jy, s, '#4d7a3e' if kind == 'forest' else '#7aa553'))
                elif kind == 'desert':
                    out.append('<path d="M%.1f %.1f q8 -6 16 0" fill="none" stroke="#c4a35c" stroke-width="1.4" stroke-linecap="round"/>' % (jx - 8, jy))
                elif kind == 'water':
                    out.append('<path d="M%.1f %.1f q4 -3 8 0 t8 0" fill="none" stroke="#8fd0cc" stroke-width="1.3" stroke-linecap="round"/>' % (jx - 8, jy))
            x += step
        y += step * 0.8
        row += 1
    return '\n'.join(out)

# ---------- icons (all simple geometric shapes) ----------
def icon(kind, x, y):
    if kind == 'village':   # small house
        return ('<path d="M%.1f %.1f l7 -7 l7 7 v7 h-14 z" fill="#f4ead0" stroke="%s" stroke-width="1.6" stroke-linejoin="round"/>'
                '<rect x="%.1f" y="%.1f" width="4" height="5" fill="%s"/>' % (x - 7, y, INK, x - 2, y + 2, INK))
    if kind in ('dungeon', 'temple'):   # tower with an arched door
        fill = '#7b2f2a' if kind == 'dungeon' else '#b8892e'
        return ('<path d="M%.1f %.1f v-11 h3 v3 h3 v-3 h3 v3 h3 v-3 h3 v11 z" fill="%s" stroke="#2e1d12" stroke-width="1.3" stroke-linejoin="round"/>'
                '<path d="M%.1f %.1f v-4 a2.5 2.5 0 0 1 5 0 v4 z" fill="#f4ead0"/>' % (x - 7.5, y + 6, fill, x - 2.5, y + 6))
    if kind == 'fairy':     # five-petal flower
        petals = ''.join('<circle cx="%.1f" cy="%.1f" r="3.4" fill="#f3a6c8" stroke="#a8527c" stroke-width="1"/>'
                         % (x + 4.6 * math.cos(a), y + 4.6 * math.sin(a)) for a in [i * 2 * math.pi / 5 - math.pi / 2 for i in range(5)])
        return petals + '<circle cx="%.1f" cy="%.1f" r="2.6" fill="#ffe9a8" stroke="#a8527c" stroke-width="0.8"/>' % (x, y)
    if kind == 'shop':      # bag
        return ('<path d="M%.1f %.1f a3 3 0 0 1 6 0" fill="none" stroke="%s" stroke-width="1.4"/>'
                '<rect x="%.1f" y="%.1f" width="11" height="9" rx="2.5" fill="#6aa56b" stroke="#2f5a34" stroke-width="1.3"/>'
                % (x - 3, y - 2, '#2f5a34', x - 5.5, y - 2.5))
    if kind == 'grotto':    # hole in the ground
        return ('<ellipse cx="%.1f" cy="%.1f" rx="7" ry="4" fill="#3a2a1c" stroke="#8b6c45" stroke-width="1.5"/>' % (x, y))
    if kind == 'landmark':
        return '<circle cx="%.1f" cy="%.1f" r="4" fill="%s" stroke="#f4ead0" stroke-width="1.4"/>' % (x, y, INK)
    raise ValueError(kind)

def chest(x, y, n, age, badge=True):
    body, band = ('#8a5a2b', '#e0b34a') if age == 'child' else ('#33506f', '#c9d3dc')
    s = ('<g><rect x="%.1f" y="%.1f" width="20" height="14" rx="2" fill="%s" stroke="#20140b" stroke-width="1.4"/>'
         '<path d="M%.1f %.1f h20" stroke="#20140b" stroke-width="1.2"/>'
         '<rect x="%.1f" y="%.1f" width="4" height="14" fill="%s" stroke="#20140b" stroke-width="0.8"/>'
         % (x - 10, y - 7, body, x - 10, y - 2, x - 2, y - 7, band))
    if not badge:
        return s + '</g>'
    s += ('<circle cx="%.1f" cy="%.1f" r="7.5" fill="#fff8e6" stroke="#20140b" stroke-width="1.3"/>'
          '<text x="%.1f" y="%.1f" font-family="%s" font-size="9.5" font-weight="700" text-anchor="middle" fill="#20140b">%d</text></g>'
          % (x + 11, y - 9, x + 11, y - 5.6, FONT, n))
    return s

def label(text, x, y, size=12, anchor='start', weight='400', style='normal', fill=INK, rotate=None):
    tr = ' transform="rotate(%s %.1f %.1f)"' % (rotate, x, y) if rotate is not None else ''
    return ('<text x="%.1f" y="%.1f" font-family="%s" font-size="%s" font-weight="%s" font-style="%s" text-anchor="%s" '
            'fill="%s" stroke="%s" stroke-width="3.2" stroke-linejoin="round" paint-order="stroke"%s>%s</text>'
            % (x, y, FONT, size, weight, style, anchor, fill, PAPER, tr, escape(text)))

def poi_label(p):
    x, y = p['at']
    where = p.get('label', 'right')
    style = dict(village=('13', '700', 'normal'), dungeon=('11.5', '600', 'italic'), temple=('11.5', '600', 'italic'),
                 landmark=('11', '400', 'normal')).get(p['type'])
    if where == 'none' or not style:
        return ''
    size, weight, st = style
    pos = {'right': (x + 12, y + 4, 'start'), 'left': (x - 12, y + 4, 'end'),
           'above': (x, y - 13, 'middle'), 'below': (x, y + 22, 'middle')}[where]
    return label(p['name'], pos[0], pos[1], size, pos[2], weight, st)

def build():
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (W, H, W, H)]
    o.append('<defs><filter id="grain" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency="0.012" numOctaves="3" seed="4"/>'
             '<feColorMatrix values="0 0 0 0 0.35  0 0 0 0 0.25  0 0 0 0 0.12  0 0 0 0.16 0"/></filter>'
             '<radialGradient id="vig" cx="50%" cy="45%" r="75%"><stop offset="0.6" stop-color="#000" stop-opacity="0"/>'
             '<stop offset="1" stop-color="#5a3d1e" stop-opacity="0.28"/></radialGradient></defs>')
    o.append('<rect width="100%%" height="100%%" fill="%s"/>' % PAPER)
    # title
    o.append(label('Souls of Harkinian', 30, 44, 28, 'start', '700'))
    o.append(label('Swords of Hyrule: where each custom sword waits in a chest', 30, 66, 14, 'start', '400', 'italic', INK2))
    # map
    o.append('<g transform="translate(0 %d)">' % TOP)
    lands = [blob(l['c'], l['r'], l['seed']) for l in DATA['land']]
    o += ['<path d="%s" fill="none" stroke="%s" stroke-width="7" stroke-linejoin="round"/>' % (d, COL['coast']) for d in lands]
    o += ['<path d="%s" fill="%s"/>' % (d, COL['land']) for d in lands]
    for t in DATA['terrain']:
        edge = {'water': '#3f8a8c', 'lava': '#8f2f1f'}.get(t['kind'], 'none')
        o.append('<path d="%s" fill="%s" stroke="%s" stroke-width="2" opacity="0.95"/>' % (blob(t['c'], t['r'], t['seed'], wob=0.10), COL[t['kind']], edge))
        o.append(texture(t))
    for rv in DATA['rivers']:
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>' % (smooth([tuple(p) for p in rv]), COL['river']))
    for rd in DATA['roads']:
        o.append('<path d="%s" fill="none" stroke="%s" stroke-width="2.6" stroke-dasharray="7 5" stroke-linecap="round"/>' % (smooth([tuple(p) for p in rd]), COL['road']))
    for a in DATA['areaLabels']:
        o.append(label(a['text'].upper(), a['at'][0], a['at'][1], 13, 'middle', '600', 'normal', INK2, a.get('rotate'))
                 .replace('font-size="13"', 'font-size="13" letter-spacing="2.5"'))
    order = ['landmark', 'grotto', 'shop', 'fairy', 'village', 'temple', 'dungeon']
    for kind in order:
        for p in DATA['poi']:
            if p['type'] == kind:
                o.append(icon(kind, *p['at']))
    for p in DATA['poi']:
        o.append(poi_label(p))
    for c in DATA['chests']['list']:
        dx, dy = c.get('off', (16, -18))
        o.append(chest(c['at'][0] + dx, c['at'][1] + dy, c['n'], c['age']))
    # compass (an arrow, not a star)
    cx, cy = 60, 640
    o.append('<g><circle cx="%d" cy="%d" r="24" fill="none" stroke="%s" stroke-width="1.5"/>'
             '<path d="M%d %d l9 26 l-9 -6 l-9 6 z" fill="%s"/>' % (cx, cy, INK2, cx, cy - 20, INK) +
             label('N', cx, cy - 30, 14, 'middle', '700') + '</g>')
    o.append('</g>')
    # legend
    y0 = TOP + MH + 18
    o.append('<path d="M30 %d H%d" stroke="%s" stroke-width="1.2"/>' % (y0, W - 30, INK2))
    o.append(label('Key', 30, y0 + 30, 15, 'start', '700'))
    keys = [('village', 'Village'), ('dungeon', 'Dungeon'), ('temple', 'Temple of Time'), ('fairy', 'Great Fairy Fountain'),
            ('shop', 'Shop'), ('grotto', 'Grotto'), ('landmark', 'Landmark')]
    for i, (k, t) in enumerate(keys):
        yy = y0 + 58 + i * 26
        o.append(icon(k, 42, yy - 4))
        o.append(label(t, 62, yy, 12.5))
    yy = y0 + 58 + len(keys) * 26
    o.append(chest(40, yy - 4, 0, 'child', badge=False))
    o.append(label('Chest, child Link', 62, yy, 12.5))
    o.append(chest(40, yy + 24, 0, 'adult', badge=False))
    o.append(label('Chest, adult Link', 62, yy + 28, 12.5))
    o.append(label('Swords (creator, age, chest)', 260, y0 + 30, 15, 'start', '700'))
    lst = DATA['chests']['list']
    half = (len(lst) + 1) // 2
    for i, c in enumerate(lst):
        col, row = divmod(i, half)
        x, yy = 260 + col * 470, y0 + 62 + row * 44
        o.append(chest(x + 10, yy - 2, c['n'], c['age']))
        o.append(label(c['sword'], x + 40, yy - 2, 13, 'start', '700'))
        o.append(label('%s · %s · %s' % (c['creator'], c['age'], c['where']), x + 40, yy + 14, 11, 'start', '400', 'normal', INK2))
    o.append(label('Map drawn by make_map.py from hyrule.json: simple shapes only, no game art. Sword models come from their '
                   'creators’ packs, downloaded by souls/install.sh.', 30, H - 18, 10.5, 'start', '400', 'italic', INK2))
    o.append('<rect width="100%" height="100%" filter="url(#grain)"/>')
    o.append('<rect width="100%" height="100%" fill="url(#vig)"/>')
    o.append('</svg>')
    return '\n'.join(o)

def main():
    scale = float(sys.argv[sys.argv.index('--scale') + 1]) if '--scale' in sys.argv else 2
    svg = os.path.join(HERE, 'hyrule-chests.svg')
    open(svg, 'w').write(build())
    print('wrote', svg)
    if shutil.which('rsvg-convert'):
        png = os.path.join(HERE, 'hyrule-chests.png')
        subprocess.run(['rsvg-convert', '-z', str(scale), '-o', png, svg], check=True)
        if shutil.which('magick'):   # 256 colors: about a third of the size, no visible change
            subprocess.run(['magick', png, '-dither', 'None', '-colors', '256', 'PNG8:' + png], check=True)
        print('wrote', png)
    else:
        print('rsvg-convert not found: PNG skipped')

if __name__ == '__main__':
    main()
