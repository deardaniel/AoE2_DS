// SLP to PNG extractor - fixed for AoE2 DE
const fs = require('fs');
const path = require('path');
const SLP = require('genie-slp');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

const slpPath = process.argv[2];
const outputDir = process.argv[3] || 'output';
const rawArg4 = process.argv[4];
const rawArg5 = process.argv[5];
const parsedIndex = rawArg4 !== undefined ? parseInt(rawArg4, 10) : NaN;
const hasFrameIndex = rawArg4 !== undefined && !Number.isNaN(parsedIndex);
const slpIndex = parsedIndex; // which frame to extract (optional)
const paletteOverride = hasFrameIndex ? rawArg5 : rawArg4; // optional palette path

if (!slpPath) {
    console.log('Usage: node extract-slp.js <slp-file> [output-dir] [frame-index] [palette]');
    console.log('Example: node extract-slp.js "game_b1.slp" assets 1');
    console.log('Example: node extract-slp.js "2.slp" output 0 "/path/to/pal_5.pal"');
    process.exit(1);
}

if (!fs.existsSync(slpPath)) {
    console.error('File not found:', slpPath);
    process.exit(1);
}

if (hasFrameIndex && slpIndex < 0) {
    console.error('Invalid frame index:', process.argv[4]);
    process.exit(1);
}

if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
}

// Try to find palette - check multiple possible locations
// The correct AoE2 unit palette is 50500.bina from the interface DRS directory (JASC-PAL format)
const slpDir = path.dirname(slpPath);
const possiblePalettes = [
    ...(paletteOverride ? [paletteOverride] : []),
    // Correct AoE2 HD unit rendering palette
    '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina',
    // Fallback paths
    path.join(slpDir, '..', 'interface', '50500.bina'),
    path.join(slpDir, '..', '..', 'palettes', 'original.pal'),
    path.join(slpDir, '..', 'palettes', 'original.pal'),
    '/mnt/c/Program Files (x86)/Steam/steamapps/common/AoE2DE/resources/_common/palettes/original.pal',
    'original.pal'
];

let paletteBuffer = null;
for (const p of possiblePalettes) {
    try {
        if (fs.existsSync(p)) {
            paletteBuffer = fs.readFileSync(p);
            console.log('Found palette:', p);
            break;
        }
    } catch (e) {}
}

if (!paletteBuffer) {
    console.error('Could not find palette file');
    process.exit(1);
}

const palette = Palette(paletteBuffer);
// Guard against out-of-range or malformed color indices
const safePalette = new Array(1024);
for (let i = 0; i < safePalette.length; i++) safePalette[i] = [0, 0, 0];
for (let i = 0; i < palette.length; i++) safePalette[i] = palette[i];
safePalette.undefined = [0, 0, 0];
const slpBuffer = fs.readFileSync(slpPath);
const slp = new SLP(slpBuffer);

console.log(`SLP: ${path.basename(slpPath)}`);
console.log(`Frames: ${slp.numFrames}`);

// Extract all frames or a single frame
const start = hasFrameIndex ? slpIndex : 0;
const end = hasFrameIndex ? Math.min(slpIndex + 1, slp.numFrames) : slp.numFrames;

for (let i = start; i < end; i++) {
    const frameInfo = slp.frames[i];
    console.log(`Frame ${i}: ${frameInfo.width}x${frameInfo.height}`);
    
    // Skip huge frames (likely backgrounds/terrain)
    if (frameInfo.width > 400 || frameInfo.height > 400) {
        console.log(`  Skipping (too large)`);
        continue;
    }
    
    try {
        const imageData = slp.renderFrame(i, safePalette, { player: 1, drawOutline: false });
        if (!imageData) continue;
        
        const { width, height, data: pixels } = imageData;
        
        const png = new PNG({ width, height });
        png.data.set(pixels);
        
        const outPath = path.join(outputDir, `frame_${i}.png`);
        fs.writeFileSync(outPath, PNG.sync.write(png));
        console.log(`  Saved: ${path.basename(outPath)}`);
        
    } catch (e) {
        console.log(`  Error: ${e.message}`);
    }
}

console.log('Done!');
