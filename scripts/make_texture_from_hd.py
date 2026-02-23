#!/usr/bin/env python3
import argparse
from PIL import Image
import os


def main():
    ap = argparse.ArgumentParser(description='Create an indexed 256x256 texture from HD source.')
    ap.add_argument('--src', required=True, help='Source texture PNG (RGBA)')
    ap.add_argument('--out', required=True, help='Output PNG (indexed)')
    args = ap.parse_args()

    img = Image.open(args.src).convert('RGBA')
    img = img.resize((256, 256), Image.NEAREST)
    img = img.convert('P', palette=Image.ADAPTIVE, colors=256)

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    img.save(args.out)
    print(f'Saved {args.out}')

if __name__ == '__main__':
    main()
