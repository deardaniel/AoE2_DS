const fs = require('fs');
const parseSLP = require('genie-slp');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

const SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics';
const PAL_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina';

const palette = Palette(fs.readFileSync(PAL_PATH));

// Check these SLPs
const slps = [
    { id: 1181, name: 'House (THSWD_FN)' },
    { id: 2683, name: 'Barracks (BRKS1NNG)' },
];

for (const { id, name } of slps) {
    const slpPath = `${SLP_DIR}/${id}.slp`;
    if (!fs.existsSync(slpPath)) {
        console.log(`SLP ${id} (${name}): FILE NOT FOUND`);
        continue;
    }
    
    const slpBuf = fs.readFileSync(slpPath);
    const slp = parseSLP(slpBuf);
    
    console.log(`\n=== SLP ${id} (${name}) ===`);
    console.log(`  Frames: ${slp.length || slp.frames?.length || 'unknown'}`);
    
    // Check first few frames
    const numFrames = slp.length || slp.frames?.length || 0;
    for (let f = 0; f < Math.min(numFrames, 4); f++) {
        const frame = slp[f] || slp.frames[f];
        console.log(`  Frame ${f}: ${frame.width}x${frame.height}, hotspot=(${frame.hotspot?.x || frame.hotspot_x},${frame.hotspot?.y || frame.hotspot_y})`);
        
        // Render frame 0 to PNG for inspection
        if (f === 0) {
            const w = frame.width, h = frame.height;
            const png = new PNG({ width: w, height: h });
            const pixels = frame.render(palette);
            for (let y = 0; y < h; y++) {
                for (let x = 0; x < w; x++) {
                    const si = (y * w + x) * 4;
                    const di = (y * w + x) << 2;
                    png.data[di]   = pixels[si];
                    png.data[di+1] = pixels[si+1];
                    png.data[di+2] = pixels[si+2];
                    png.data[di+3] = pixels[si+3];
                }
            }
            const outPath = `/home/daniel/aoe2_dsi/.screenshots/slp_${id}_frame0.png`;
            png.pack().pipe(fs.createWriteStream(outPath));
            console.log(`  -> Saved frame 0 to ${outPath}`);
        }
    }
}
