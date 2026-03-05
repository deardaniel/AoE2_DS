// Extract building graphic info from AoE2 .dat file
// Uses genie-dat to look up unit -> graphic -> SLP chain
const fs = require('fs');
const path = require('path');
const DatFile = require('genie-dat');
const SLP = require('genie-slp');
const Palette = require('jascpal');
const { PNG } = require('pngjs');

const DAT_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat';
const SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics';
const PAL_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina';

const datBuf = fs.readFileSync(DAT_PATH);
const dat = DatFile.load(datBuf);

const palette = Palette(fs.readFileSync(PAL_PATH));
const safePalette = new Array(1024);
for (let i = 0; i < safePalette.length; i++) safePalette[i] = [0, 0, 0];
for (let i = 0; i < palette.length; i++) safePalette[i] = palette[i];

console.log('Civilizations:', dat.civilizations.length);
console.log('Graphics:', dat.graphics.length);

// Look up a unit by ID from civ 0 (Gaia) or civ 1 (Britons)
function getUnit(unitId, civIdx = 1) {
    const civ = dat.civilizations[civIdx];
    return civ.objects[unitId];
}

// Look up a graphic by ID
function getGraphic(gfxId) {
    return dat.graphics[gfxId];
}

// Print graphic info with deltas
function printGraphic(gfxId, indent = '') {
    const g = getGraphic(gfxId);
    if (!g) { console.log(indent + 'Graphic', gfxId, ': NOT FOUND'); return; }
    console.log(indent + `Graphic ${gfxId}: SLP=${g.slpID} name="${g.name}" frames=${g.frameCount} angles=${g.angleCount}`);
    if (g.deltas && g.deltas.length > 0) {
        for (const d of g.deltas) {
            if (d.graphicID >= 0) {
                console.log(indent + `  Delta: graphicID=${d.graphicID} offset=(${d.offsetX},${d.offsetY}) angle=${d.displayAngle}`);
                printGraphic(d.graphicID, indent + '    ');
            }
        }
    }
}

// Print building info
function printBuilding(unitId) {
    const u = getUnit(unitId);
    if (!u) { console.log('Unit', unitId, 'not found'); return; }
    console.log(`\n=== Unit ${unitId}: "${u.name}" ===`);
    console.log(`  StandingGraphic: ${u.standingGraphic}`);
    console.log(`  Type: ${u.type}`);

    // Print standing graphic chain
    if (u.standingGraphic >= 0) {
        printGraphic(u.standingGraphic, '  ');
    }

    // Building-specific fields
    if (u.building) {
        const b = u.building;
        console.log(`  Building.headUnit: ${b.headUnit}`);
        console.log(`  Building.stackUnitID: ${b.stackUnitID}`);
        console.log(`  Building.constructionGraphicID: ${b.constructionGraphicID}`);
        if (b.annexes) {
            for (let i = 0; i < b.annexes.length; i++) {
                const a = b.annexes[i];
                if (a.unitID >= 0) {
                    console.log(`  Annex[${i}]: unitID=${a.unitID} misplacement=(${a.misplacementX},${a.misplacementY})`);
                    // Recursively look up annex unit
                    const au = getUnit(a.unitID);
                    if (au) {
                        console.log(`    Annex "${au.name}" standingGraphic=${au.standingGraphic}`);
                        if (au.standingGraphic >= 0) {
                            printGraphic(au.standingGraphic, '      ');
                        }
                    }
                }
            }
        }
        // Also look up head unit
        if (b.headUnit >= 0) {
            console.log(`\n  --- Head Unit ${b.headUnit} ---`);
            const hu = getUnit(b.headUnit);
            if (hu) {
                console.log(`    "${hu.name}" standingGraphic=${hu.standingGraphic}`);
                if (hu.standingGraphic >= 0) {
                    printGraphic(hu.standingGraphic, '      ');
                }
            }
        }
        // Stack unit
        if (b.stackUnitID >= 0) {
            console.log(`\n  --- Stack Unit ${b.stackUnitID} ---`);
            const su = getUnit(b.stackUnitID);
            if (su) {
                console.log(`    "${su.name}" standingGraphic=${su.standingGraphic}`);
                if (su.standingGraphic >= 0) {
                    printGraphic(su.standingGraphic, '      ');
                }
            }
        }
    }
}

// Town Center IDs: 109=Dark Age TC
// Also check common building IDs
const BUILDINGS = {
    'Town Center (Dark)': 109,
    'House': 70,
    'Barracks': 12,
    'Archery Range': 87,
    'Stable': 101,
};

for (const [name, id] of Object.entries(BUILDINGS)) {
    printBuilding(id);
}
