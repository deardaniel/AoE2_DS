const fs = require('fs');
const slpParser = require('genie-slp');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

const SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics';
const PAL_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina';
const OUT_DIR = '/home/daniel/aoe2_dsi/.screenshots';

const palette = Palette(fs.readFileSync(PAL_PATH));

function renderFrame(slp, frameIdx, pal) {
    const frame = slp.frames[frameIdx];
    const w = frame.width, h = frame.height;
    // genie-slp renders via slp.renderFrame(frameIdx, palette, opts)
    const pixels = slp.renderFrame(frameIdx, pal, {player: 1});
    return { pixels, w, h, hotspot: frame.hotspot };
}

function saveFramePNG(slp, frameIdx, pal, outPath) {
    const { pixels, w, h, hotspot } = renderFrame(slp, frameIdx, pal);
    const png = new PNG({ width: w, height: h });
    // pixels is a Uint8Array of RGBA
    for (let i = 0; i < w * h * 4; i++) {
        png.data[i] = pixels[i];
    }
    const buf = PNG.sync.write(png);
    fs.writeFileSync(outPath, buf);
    console.log(`  Saved ${outPath} (${w}x${h}, hotspot=${hotspot.x},${hotspot.y})`);
}

// SLPs to extract
const buildings = [
    { id: 1181, name: 'house', desc: 'House (THSWD_FN)' },
    { id: 2683, name: 'barracks', desc: 'Barracks (BRKS1NNG)' },
];

for (const { id, name, desc } of buildings) {
    const slpPath = `${SLP_DIR}/${id}.slp`;
    if (!fs.existsSync(slpPath)) {
        console.log(`SLP ${id} (${desc}): NOT FOUND`);
        continue;
    }
    
    const slp = slpParser(fs.readFileSync(slpPath));
    console.log(`\n=== SLP ${id} (${desc}) ===`);
    console.log(`  Total frames: ${slp.frames.length}`);
    
    // Print all frame sizes
    for (let f = 0; f < Math.min(slp.frames.length, 8); f++) {
        const fr = slp.frames[f];
        console.log(`  Frame ${f}: ${fr.width}x${fr.height} hotspot=(${fr.hotspot.x},${fr.hotspot.y})`);
    }
    
    // Save frame 0
    saveFramePNG(slp, 0, palette, `${OUT_DIR}/slp_${id}_f0.png`);
    
    // Also save frame at different angles if multiple
    if (slp.frames.length > 1) {
        saveFramePNG(slp, 1, palette, `${OUT_DIR}/slp_${id}_f1.png`);
    }
}
