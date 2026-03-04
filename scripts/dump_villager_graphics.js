// Dump all type 70 (creatable) villager-related units and their graphics
const fs = require('fs');
const genieDat = require('genie-dat');

const DAT_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat';
const datBuf = fs.readFileSync(DAT_PATH);

genieDat.load(datBuf, {version: 'african-kingdoms'}, function(err, dat) {
    if (err) { console.error('Failed:', err); process.exit(1); }

    var civ = dat.civilizations[1]; // Britons

    function gfxInfo(gfxId) {
        if (gfxId === undefined || gfxId < 0 || gfxId >= dat.graphics.length || !dat.graphics[gfxId]) return 'N/A (id=' + gfxId + ')';
        var g = dat.graphics[gfxId];
        return 'SLP=' + g.slpID + ' "' + (g.name || '').trim() + '" frames=' + g.frameCount + ' angles=' + g.angleCount;
    }

    // Known villager variant IDs to check
    var checkIds = [83, 293, 118, 120, 122, 124, 156, 214, 216, 259, 579, 581, 583, 590, 592];

    // Also scan for all type 70 units with names containing V (villager prefix)
    for (var id = 0; id < civ.objects.length; id++) {
        var u = civ.objects[id];
        if (!u) continue;
        if (u.type === 70) {
            var name = (u.name || '').trim().toUpperCase();
            if (name.indexOf('V') === 0 || name.indexOf('YOUR') === 0) {
                if (checkIds.indexOf(id) < 0) checkIds.push(id);
            }
        }
    }

    checkIds.sort(function(a,b){return a-b;});

    checkIds.forEach(function(id) {
        var u = civ.objects[id];
        if (!u) return;
        console.log('Unit ' + id + ': "' + (u.name||'').trim() + '" type=' + u.type);

        if (u.standingGraphic !== undefined && u.standingGraphic >= 0)
            console.log('  standingGraphic: ' + gfxInfo(u.standingGraphic));
        if (u.standingGraphic0 !== undefined && u.standingGraphic0 >= 0)
            console.log('  standingGraphic0: ' + gfxInfo(u.standingGraphic0));

        // Check deadFish (type 60 base) for walking graphic
        if (u.deadFish) {
            if (u.deadFish.walkingGraphic !== undefined && u.deadFish.walkingGraphic >= 0)
                console.log('  walkingGraphic: ' + gfxInfo(u.deadFish.walkingGraphic));
        }

        // Check combat for attack graphic
        if (u.combat) {
            var ck = Object.keys(u.combat);
            ck.forEach(function(key) {
                if (key.toLowerCase().indexOf('graphic') >= 0 && typeof u.combat[key] === 'number' && u.combat[key] >= 0) {
                    console.log('  combat.' + key + ': ' + gfxInfo(u.combat[key]));
                }
            });
        }

        // Check creatable for other graphics
        if (u.creatable) {
            var crk = Object.keys(u.creatable);
            crk.forEach(function(key) {
                if (key.toLowerCase().indexOf('graphic') >= 0 && typeof u.creatable[key] === 'number' && u.creatable[key] >= 0) {
                    console.log('  creatable.' + key + ': ' + gfxInfo(u.creatable[key]));
                }
            });
        }

        // Check top-level graphic properties
        if (u.dyingGraphic !== undefined && u.dyingGraphic >= 0)
            console.log('  dyingGraphic: ' + gfxInfo(u.dyingGraphic));

        console.log('');
    });
});
