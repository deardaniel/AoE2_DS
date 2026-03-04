// Dump Town Center graphic chain from AoE2 .dat file
// Uses genie-dat callback API to parse the compressed .dat
const fs = require('fs');
const genieDat = require('genie-dat');

const DAT_PATH = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat';

const datBuf = fs.readFileSync(DAT_PATH);

genieDat.load(datBuf, {version: 'african-kingdoms'}, function(err, dat) {
    if (err) {
        console.error('Failed to parse .dat file:', err);
        process.exit(1);
    }

    console.log('Civilizations:', dat.civilizations.length);
    console.log('Graphics:', dat.graphics.length);

    function printGraphic(gfxId, indent) {
        indent = indent || '';
        if (gfxId < 0 || gfxId >= dat.graphics.length) {
            console.log(indent + 'Graphic ' + gfxId + ': OUT OF RANGE');
            return;
        }
        var g = dat.graphics[gfxId];
        if (!g) { console.log(indent + 'Graphic ' + gfxId + ': NULL'); return; }
        console.log(indent + 'Graphic ' + gfxId + ': SLP=' + g.slpID + ' name="' + g.name + '" frames=' + g.frameCount + ' angles=' + g.angleCount + ' layer=' + g.layer);
        if (g.deltas && g.deltas.length > 0) {
            for (var i = 0; i < g.deltas.length; i++) {
                var d = g.deltas[i];
                if (d.graphicID >= 0) {
                    console.log(indent + '  Delta[' + i + ']: graphicID=' + d.graphicID + ' offset=(' + d.offsetX + ',' + d.offsetY + ') angle=' + d.displayAngle);
                    printGraphic(d.graphicID, indent + '    ');
                }
            }
        }
    }

    function printUnit(unitId, civIdx) {
        civIdx = civIdx || 1;
        var civ = dat.civilizations[civIdx];
        if (!civ || !civ.objects[unitId]) {
            console.log('Unit ' + unitId + ' not found in civ ' + civIdx);
            return;
        }
        var u = civ.objects[unitId];
        console.log('\n=== Unit ' + unitId + ': "' + u.name + '" (civ ' + civIdx + ') ===');
        console.log('  Type: ' + u.type);
        console.log('  StandingGraphic: ' + u.standingGraphic);

        if (u.standingGraphic >= 0) {
            printGraphic(u.standingGraphic, '  ');
        }

        if (u.building) {
            var b = u.building;
            console.log('  Building.constructionGraphicID: ' + b.constructionGraphicID);
            console.log('  Building.headUnit: ' + b.headUnit);
            console.log('  Building.stackUnitID: ' + b.stackUnitID);
            if (b.annexes) {
                for (var i = 0; i < b.annexes.length; i++) {
                    var a = b.annexes[i];
                    if (a.unitID >= 0) {
                        console.log('  Annex[' + i + ']: unitID=' + a.unitID + ' misplacement=(' + a.misplacementX + ',' + a.misplacementY + ')');
                        var au = civ.objects[a.unitID];
                        if (au) {
                            console.log('    Annex "' + au.name + '" standingGraphic=' + au.standingGraphic);
                            if (au.standingGraphic >= 0) {
                                printGraphic(au.standingGraphic, '      ');
                            }
                        }
                    }
                }
            }
            if (b.headUnit >= 0) {
                console.log('\n  --- Head Unit ' + b.headUnit + ' ---');
                var hu = civ.objects[b.headUnit];
                if (hu) {
                    console.log('    "' + hu.name + '" standingGraphic=' + hu.standingGraphic);
                    if (hu.standingGraphic >= 0) {
                        printGraphic(hu.standingGraphic, '      ');
                    }
                }
            }
            if (b.stackUnitID >= 0) {
                console.log('\n  --- Stack Unit ' + b.stackUnitID + ' ---');
                var su = civ.objects[b.stackUnitID];
                if (su) {
                    console.log('    "' + su.name + '" standingGraphic=' + su.standingGraphic);
                    if (su.standingGraphic >= 0) {
                        printGraphic(su.standingGraphic, '      ');
                    }
                }
            }
        }
    }

    // TC unit ID is 109
    printUnit(109, 1);

    // Also dump House (70), Barracks (12) for comparison
    printUnit(70, 1);
    printUnit(12, 1);
});
