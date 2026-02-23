#!/usr/bin/env python3
import os

OUT = 'sprites/index.html'

pngs = [
    f for f in os.listdir('sprites')
    if f.lower().endswith('.png') and (f.startswith('hd_') or f.startswith('vil_'))
]
pngs.sort()

rows = []
rows.append('<!doctype html>')
rows.append('<html><head><meta charset="utf-8">')
rows.append('<title>Sprite Preview</title>')
rows.append('<style>body{font-family:Arial, sans-serif;background:#111;color:#eee} .grid{display:flex;flex-wrap:wrap;gap:12px} .card{background:#1a1a1a;border:1px solid #333;padding:8px} img{image-rendering:pixelated; background:#000}</style>')
rows.append('</head><body>')
rows.append('<h1>HD Sprite Sheets</h1>')
rows.append('<div class="grid">')
for f in pngs:
    rows.append('<div class="card">')
    rows.append(f'<div>{f}</div>')
    rows.append(f'<img src="{f}" />')
    rows.append('</div>')
rows.append('</div></body></html>')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w', encoding='utf-8') as out:
    out.write('\n'.join(rows))

print(f'Wrote {OUT} with {len(pngs)} entries')
