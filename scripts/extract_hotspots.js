const fs = require('fs');
const parseSLP = require('genie-slp');
const DatFile = require('genie-dat');

// AoE2 HD SLPs can be in multiple directories
const SLP_DIRS = [
    '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics',
    '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/slp',
];

const DAT_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat';
const datBuf = fs.readFileSync(DAT_PATH);

function findSlp(id) {
    for (const dir of SLP_DIRS) {
        const p = dir + '/' + id + '.slp';
        if (fs.existsSync(p)) return p;
    }
    return null;
}

DatFile.load(datBuf, {version: 'african-kingdoms'}, function(err, dat) {
    if (err) { console.error('dat load error:', err); return; }

    function getUnit(unitId) { return dat.civilizations[1].objects[unitId]; }
    function getGraphic(gfxId) { return dat.graphics[gfxId]; }

    const BUILDINGS = {
        'House': 70,
        'Barracks': 12,
        'Archery Range': 87,
        'Stable': 101,
        'Town Center': 109,
        'Mining Camp': 584,
        'Lumber Camp': 562,
        'Farm': 50,
    };

    for (const [name, unitId] of Object.entries(BUILDINGS)) {
        const u = getUnit(unitId);
        if (!u) { console.log(name, ': unit not found'); continue; }
        const gfxId = u.standingGraphic0;
        const g = getGraphic(gfxId);
        if (!g) continue;
        const slpId = g.slpId;

        const slpPath = findSlp(slpId);
        if (!slpPath) { console.log(name + ': SLP ' + slpId + ' not found'); continue; }

        try {
            const slpBuf = fs.readFileSync(slpPath);
            const slp = parseSLP(slpBuf);
            const f = slp.frames[0];
            console.log(name + ' (unit ' + unitId + '): SLP=' + slpId +
                ', size=' + f.width + 'x' + f.height +
                ', hotspot=(' + f.hotspot_x + ',' + f.hotspot_y + ')' +
                ', frames=' + slp.frames.length);

            // Check a few more frames for consistency
            if (slp.frames.length > 5) {
                for (let i = 0; i < Math.min(5, slp.frames.length); i++) {
                    const fi = slp.frames[i];
                    console.log('  frame[' + i + ']: ' + fi.width + 'x' + fi.height + ' hotspot=(' + fi.hotspot_x + ',' + fi.hotspot_y + ')');
                }
            }
        } catch(e) {
            console.log(name + ': error: ' + e.message);
        }
    }
});
