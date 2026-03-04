// Direct SLP parser - based on freeaoe/genieutils source
const fs = require('fs');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

const SLP_CMD_COPY = 0x00;
const SLP_CMD_SKIP = 0x01;
const SLP_CMD_BIG_COPY = 0x02;
const SLP_CMD_BIG_SKIP = 0x03;
const SLP_CMD_TRANSFORM = 0x04;
const SLP_CMD_FILL = 0x05;
const SLP_CMD_END_ROW = 0x0F;

// Read SLP file directly
function readSLP(slpPath) {
    const buf = fs.readFileSync(slpPath);
    
    // Parse header
    const numFrames = buf.readUInt32LE(8);
    const comment = buf.slice(12, 36).toString('ascii');
    
    console.log('SLP:', slpPath);
    console.log('Frames:', numFrames);
    console.log('Comment:', comment);
    
    // Read frame headers (at offset 36)
    const frames = [];
    for (let i = 0; i < numFrames; i++) {
        const offset = 36 + i * 32;
        frames.push({
            cmdTableOffset: buf.readUInt32LE(offset + 0),
            outlineTableOffset: buf.readUInt32LE(offset + 4),
            paletteOffset: buf.readUInt32LE(offset + 8),
            properties: buf.readUInt32LE(offset + 12),
            width: buf.readInt32LE(offset + 16),
            height: buf.readInt32LE(offset + 20),
            hotspotX: buf.readInt32LE(offset + 24),
            hotspotY: buf.readInt32LE(offset + 28)
        });
    }
    
    return { buf, frames };
}

// Render a single frame
function renderFrame(buf, frame, palette) {
    const { width, height, cmdTableOffset, outlineTableOffset } = frame;
    
    console.log('Rendering frame:', width, 'x', height);
    console.log('Cmd table:', cmdTableOffset, 'Outline:', outlineTableOffset);
    
    // Read outline table (left/right edges for each row)
    const leftEdges = [];
    const rightEdges = [];
    for (let y = 0; y < height; y++) {
        const left = buf.readUInt16LE(outlineTableOffset + y * 4);
        const right = buf.readUInt16LE(outlineTableOffset + y * 4 + 2);
        leftEdges.push(left);
        rightEdges.push(right);
    }
    
    // Read command offsets (one per row)
    const cmdOffsets = [];
    for (let y = 0; y < height; y++) {
        cmdOffsets.push(buf.readUInt32LE(cmdTableOffset + y * 4));
    }
    
    // Create pixel buffer (RGBA)
    const pixels = Buffer.alloc(width * height * 4);
    
    // Process each row
    for (let row = 0; row < height; row++) {
        // Skip transparent rows
        if (leftEdges[row] === 0x8000 || rightEdges[row] === 0x8000) {
            continue;
        }
        
        let pos = leftEdges[row]; // Start at left edge
        let cmdOffset = cmdOffsets[row];
        
        while (true) {
            const cmd = buf.readUInt8(cmdOffset++);
            
            if (cmd === SLP_CMD_END_ROW) {
                break;
            }
            
            const lowerBits = cmd & 0x03;
            const upperBits = cmd & 0xF0;
            
            if (lowerBits === SLP_CMD_COPY) {
                // Lesser block copy: (cmd >> 2) pixels
                const count = cmd >> 2;
                for (let i = 0; i < count && pos < width; i++) {
                    const colorIdx = buf.readUInt8(cmdOffset++);
                    const color = palette[colorIdx] || [0, 0, 0];
                    const idx = (row * width + pos) * 4;
                    pixels[idx] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = 255;
                    pos++;
                }
            } else if (lowerBits === SLP_CMD_SKIP) {
                // Lesser skip: (cmd >> 2) transparent pixels
                pos += cmd >> 2;
            } else if (upperBits === SLP_CMD_BIG_COPY) {
                // Greater block copy
                const count = ((cmd & 0x0C) << 4) | buf.readUInt8(cmdOffset++);
                for (let i = 0; i < count && pos < width; i++) {
                    const colorIdx = buf.readUInt8(cmdOffset++);
                    const color = palette[colorIdx] || [0, 0, 0];
                    const idx = (row * width + pos) * 4;
                    pixels[idx] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = 255;
                    pos++;
                }
            } else if (upperBits === SLP_CMD_BIG_SKIP) {
                // Greater skip
                const count = ((cmd & 0x0C) << 4) | buf.readUInt8(cmdOffset++);
                pos += count;
            } else if (upperBits === SLP_CMD_TRANSFORM) {
                // Copy and transform (player color)
                // Get pixel count from data
                const pxCount = getPixelCount(buf, cmd, cmdOffset);
                cmdOffset++;
                for (let i = 0; i < pxCount && pos < width; i++) {
                    const colorIdx = buf.readUInt8(cmdOffset++) + 16; // Player color offset
                    const color = palette[colorIdx] || palette[colorIdx - 16] || [0, 0, 0];
                    const idx = (row * width + pos) * 4;
                    pixels[idx] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = 255;
                    pos++;
                }
            } else if (upperBits === SLP_CMD_FILL) {
                // Fill (run of color)
                const pxCount = getPixelCount(buf, cmd, cmdOffset);
                cmdOffset++;
                const colorIdx = buf.readUInt8(cmdOffset++);
                const color = palette[colorIdx] || [0, 0, 0];
                for (let i = 0; i < pxCount && pos < width; i++) {
                    const idx = (row * width + pos) * 4;
                    pixels[idx] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = 255;
                    pos++;
                }
            } else {
                // Unknown command
                console.log('Unknown cmd:', cmd.toString(16), 'at', cmdOffset - 1);
                break;
            }
        }
    }
    
    return { width, height, pixels };
}

function getPixelCount(buf, cmd, offset) {
    // From genieutils:
    // bits 2-3 tell us how to read count
    const bits = (cmd >> 2) & 0x03;
    if (bits === 0) return 0;
    if (bits === 1) return 1;
    if (bits === 2) return buf.readUInt8(offset);
    return (buf.readUInt8(offset) << 8) | buf.readUInt8(offset + 1);
}

// Main
const slpPath = process.argv[2] || '/mnt/c/Program Files (x86)/Steam/steamapps/common/AoE2DE/resources/_common/drs/graphics/game_b3.slp';
const frameIdx = parseInt(process.argv[3]) || 5;

const paletteBuffer = fs.readFileSync('/mnt/c/Program Files (x86)/Steam/steamapps/common/AoE2DE/resources/_common/palettes/original.pal');
const palette = Palette(paletteBuffer);

const { buf, frames } = readSLP(slpPath);
const frame = frames[frameIdx];

console.log('\\nFrame', frameIdx, ':', frame);

if (frame.width > 0 && frame.height > 0) {
    const result = renderFrame(buf, frame, palette);
    const png = new PNG({ width: result.width, height: result.height });
    result.pixels.copy(png.data);
    fs.writeFileSync('debug_direct.png', PNG.sync.write(png));
    console.log('\\nSaved debug_direct.png');
}
