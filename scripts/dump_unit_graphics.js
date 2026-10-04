// Print the graphics (SLP IDs) of units from the AoE2 HD .dat, resolved by ID.
//
//   node scripts/dump_unit_graphics.js 109 4 87          # British (civ 1)
//   node scripts/dump_unit_graphics.js --civ 3 87        # another civ
//   node scripts/dump_unit_graphics.js --name ARCHR      # search by name
//
// For each unit: standing / walking / dying graphics with their delta
// (layered) graphics and offsets, plus annex and head units for multi-part
// buildings such as the Town Center (unit 109).
const loadDat = require('./dat_by_id.js');

const args = process.argv.slice(2);
let civ = 1;
let nameSearch = null;
const ids = [];
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--civ') civ = parseInt(args[++i], 10);
    else if (args[i] === '--name') nameSearch = args[++i].toUpperCase();
    else ids.push(parseInt(args[i], 10));
}
if (!ids.length && !nameSearch) {
    console.error('Usage: node scripts/dump_unit_graphics.js [--civ N] [--name TEXT] [unitId ...]');
    process.exit(1);
}

loadDat(function (dat, db) {
    console.log('civ ' + civ + ': ' + dat.civilizations[civ].name.trim());

    function graphic(id, indent, seen) {
        if (id < 0) return;
        const g = db.graphic(id);
        if (!g) { console.log(indent + 'graphic ' + id + ' MISSING'); return; }
        console.log(indent + 'graphic ' + id + ' ' + g.name.trim() + ' SLP ' + g.slpId +
            ' (' + g.frameCount + ' frames x ' + g.angleCount + ' angles, layer ' + g.layer + ')');
        if (seen.indexOf(id) >= 0) return;
        g.deltas.forEach(function (d) {
            if (d.graphicId < 0) return;
            console.log(indent + '  delta (' + d.offsetX + ',' + d.offsetY + '):');
            graphic(d.graphicId, indent + '    ', seen.concat([id]));
        });
    }

    function unit(id, indent, nested) {
        const u = db.unit(id, civ);
        if (!u) { console.log(indent + 'unit ' + id + ' not found'); return; }
        console.log('\n' + indent + 'unit ' + id + ' ' + u.name.trim());
        [['standing', u.standingGraphic0], ['walking', u.walkingGraphics0], ['dying', u.dyingGraphic]]
            .forEach(function (p) {
                if (p[1] === undefined || p[1] < 0) return;
                console.log(indent + '  ' + p[0] + ':');
                graphic(p[1], indent + '    ', []);
            });
        if (nested || !u.annexes) return;
        u.annexes.forEach(function (a) {
            if (a.objectId < 0) return;
            console.log(indent + '  annex at tile offset (' + a.misplaced0 + ',' + a.misplaced1 + '):');
            unit(a.objectId, indent + '    ', true);
        });
        if (u.headObjectId >= 0) {
            console.log(indent + '  head unit:');
            unit(u.headObjectId, indent + '    ', true);
        }
    }

    if (nameSearch) {
        dat.civilizations[civ].objects.forEach(function (o) {
            if (o && o.name.toUpperCase().indexOf(nameSearch) >= 0) ids.push(o.id);
        });
    }
    ids.forEach(function (id) { unit(id, '', false); });
});
