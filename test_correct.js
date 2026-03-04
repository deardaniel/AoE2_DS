// Correct SLP extractor based on freeaoe/genieutils
const fs = require('fs');
const SLP = require('genie-slp');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

// SLP command constants (from genieutils)
const SLP_CMD_COPY = 0x00;      // Lesser block copy
const SLP_CMD_SKIP = 0x01;      // Lesser skip
const SLP_CMD_END_ROW = 0x0F;  // End of row
const SLP_CMD_BIG_COPY = 0x02; // Greater block copy
const SLP_CMD_BIG_SKIP = 0x03; // Greater skip
const SLP_CMD_TRANSFORM = 0x04; // Copy and transform (player color)
const SLP_CMD_FILL = 0x05;     // Run of color (fill)

function renderFrameCorrect(slp, frameIdx, palette, player = 1) {
    const frame = slp.parseFrame(frameIdx);
    if (!frame) return null;
    
    const { width, height, commands, outlines } = frame;
    const pixels = Buffer.alloc(width * height * 4);
    pixels.fill(0);
    
    // Player color offset in palette (16 colors per player)
    const PLAYER_OFFSET = 16 * player;
    
    let row = 0;
    let pos = 0; // pixel position in row
    
    for (const cmd of commands) {
        if (row >= height) break;
        
        const cmdByte = cmd.command;
        const lowerBits = cmdByte & 0x03;
        
        // Get current row's left edge as starting position
        const outline = outlines[row] || { left: 0, right: width };
        
        switch (lowerBits) {
            case 0x00: // Lesser block copy
                // Copy (cmdByte >> 2) pixels directly
                const copyCount = (cmdByte >> 2);
                for (let i = 0; i < copyCount && pos < width; i++) {
                    const color = palette[cmd.arg] || [0, 0, 0];
                    const idx = (row * width + pos) * 4;
                    pixels[idx] = color[0];
                    pixels[idx + 1] = color[1];
                    pixels[idx + 2] = color[2];
                    pixels[idx + 3] = 255;
                    pos++;
                    cmd.arg = undefined; // Only use once
                }
                break;
                
            case 0x01: // Lesser skip (transparent)
                const skipCount = (cmdByte >> 2);
                pos += skipCount;
                break;
                
            default:
                // Check upper nibble for big commands
                const upperNibble = (cmdByte & 0xF0) >> 4;
                
                switch (upperNibble) {
                    case 0x00: // Greater block copy
                    case 0x01: // Greater skip  
                    case 0x02: // Copy and transform (player color)
                    case 0x03: // Fill color
                        // These have pxCount in cmd.arg
                        const count = cmd.arg?.pxCount || 1;
                        const colorIdx = cmd.arg?.color || 0;
                        
                        if (upperNibble === 0x01) {
                            // Greater skip
                            pos += count;
                        } else if (upperNibble === 0x02) {
                            // Player color
                            const pColor = palette[colorIdx + PLAYER_OFFSET] || palette[colorIdx] || [0, 0, 0];
                            for (let i = 0; i < count && pos < width; i++) {
                                const idx = (row * width + pos) * 4;
                                pixels[idx] = pColor[0];
                                pixels[idx + 1] = pColor[1];
                                pixels[idx + 2] = pColor[2];
                                pixels[idx + 3] = 255;
                                pos++;
                            }
                        } else {
                            // Block copy or fill
                            const color = palette[colorIdx] || [0, 0, 0];
                            for (let i = 0; i < count && pos < width; i++) {
                                const idx = (row * width + pos) * 4;
                                pixels[idx] = color[0];
                                pixels[idx + 1] = color[1];
                                pixels[idx + 2] = color[2];
                                pixels[idx + 3] = 255;
                                pos++;
                            }
                        }
                        break;
                }
                break;
        }
        
        // Check for end of row - this comes as separate command 0x0F
        if (cmd.command === SLP_CMD_END_ROW) {
            row++;
            pos = 0;
        }
    }
    
    return { width, height, pixels };
}

// Test with game_b3 frame 5
const slpBuffer = fs.readFileSync('/mnt/c/Program Files (x86)/Steam/steamapps/common/AoE2DE/resources/_common/drs/graphics/game_b3.slp');
const paletteBuffer = fs.readFileSync('/mnt/c/Program Files (x86)/Steam/steamapps/common/AoE2DE/resources/_common/palettes/original.pal');
const palette = Palette(paletteBuffer);

const slp = new SLP(slpBuffer);

// Extract frame 5 (the small one)
const result = renderFrameCorrect(slp, 5, palette);

if (result) {
    const { width, height, pixels } = result;
    console.log('Rendered:', width, 'x', height);
    
    const png = new PNG({ width, height });
    pixels.copy(png.data);
    fs.writeFileSync('test_correct.png', PNG.sync.write(png));
    console.log('Saved test_correct.png');
} else {
    console.log('Failed to render');
}
