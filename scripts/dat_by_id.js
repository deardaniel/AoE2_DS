// Load the AoE2 HD .dat and index it by ID.
//
// genie-dat returns `graphics` and each civ's `objects` as arrays with the
// empty slots removed, so array position is NOT the graphic/unit ID. Indexing
// them directly silently returns a different entry (that is how the Town
// Center ended up with Imperial Age wings). Always go through these maps.
const fs = require('fs');
const genieDat = require('genie-dat');

const DAT_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat';

// cb(dat, { graphic(id), unit(id, civIdx = 1) })
module.exports = function loadDat(cb) {
    genieDat.load(fs.readFileSync(DAT_PATH), { version: 'african-kingdoms' }, function (err, dat) {
        if (err) {
            console.error('Failed to parse .dat file:', err);
            process.exit(1);
        }
        const graphics = new Map();
        dat.graphics.forEach(function (g) { if (g) graphics.set(g.id, g); });
        const units = dat.civilizations.map(function (civ) {
            const m = new Map();
            civ.objects.forEach(function (o) { if (o) m.set(o.id, o); });
            return m;
        });
        cb(dat, {
            graphic: function (id) { return graphics.get(id); },
            unit: function (id, civIdx) { return units[civIdx === undefined ? 1 : civIdx].get(id); },
        });
    });
};
