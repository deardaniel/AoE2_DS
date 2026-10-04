#include "projectiles.h"
#include "config.h"
#include "game.h"
#include "units.h"
#include "iso.h"

Projectile projectiles[MAX_PROJECTILES];

void projectiles_init() {
    for (int i = 0; i < MAX_PROJECTILES; i++)
        projectiles[i].active = false;
}

void projectile_spawn(s16 srcX, s16 srcY, s16 dstX, s16 dstY,
                       u8 lifetime, s8 targetUnit, u8 damage, u8 owner) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            Projectile& p = projectiles[i];
            p.srcX = srcX;
            p.srcY = srcY;
            p.dstX = dstX;
            p.dstY = dstY;
            p.curX = srcX;
            p.curY = srcY;
            p.frame = 0;
            p.lifetime = lifetime;
            p.targetUnit = targetUnit;
            p.damage = damage;
            p.attackerOwner = owner;
            p.active = true;
            return;
        }
    }
    // Pool full — apply damage immediately as fallback
    if (targetUnit >= 0 && targetUnit < MAX_UNITS && units[targetUnit].alive) {
        units[targetUnit].hp -= damage;
        if (units[targetUnit].hp <= 0)
            unit_kill(targetUnit);
    }
}

void projectiles_update(GameState& gs) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile& p = projectiles[i];
        if (!p.active) continue;

        p.frame++;

        // Interpolate position
        if (p.lifetime > 0) {
            p.curX = p.srcX + (p.dstX - p.srcX) * p.frame / p.lifetime;
            p.curY = p.srcY + (p.dstY - p.srcY) * p.frame / p.lifetime;
        }

        // Arrived
        if (p.frame >= p.lifetime) {
            if (p.targetUnit >= 0 && p.targetUnit < MAX_UNITS &&
                units[p.targetUnit].alive && units[p.targetUnit].state != USTATE_DEAD) {
                units[p.targetUnit].hp -= p.damage;

                // Under-attack alert
                if (units[p.targetUnit].owner == 0 && gs.underAttackTimer == 0) {
                    gs.underAttackTimer = 180;
                    gs.attackAlertTX = units[p.targetUnit].x / TILE_PX;
                    gs.attackAlertTY = units[p.targetUnit].y / TILE_PX;
                }

                if (units[p.targetUnit].hp <= 0) {
#ifdef SHOWCASE
                    extern void dbg_log_kill(int killerOwner, int killerType, int victim);
                    dbg_log_kill(p.attackerOwner, -1, p.targetUnit);
#endif
                    unit_kill(p.targetUnit);
                }
            }
            p.active = false;
        }
    }
}

// Draw a short line segment (3px) along the projectile's travel direction
static void draw_pixel(u8* buf, int x, int y, u8 color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
        buf[y * 256 + x] = color;
}

void render_projectiles(u8* buf, const GameState& gs) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        const Projectile& p = projectiles[i];
        if (!p.active) continue;

        // Convert current world position to screen
        int isoX, isoY;
        worldToIso(p.curX, p.curY, isoX, isoY);
        int sx = isoX - gs.camX + ISO_TILE_W / 2;
        int sy = isoY - gs.camY;

        // Calculate direction for the line segment
        int dx = p.dstX - p.srcX;
        int dy = p.dstY - p.srcY;

        // Normalize to ~3px length using isometric projection
        // Convert direction to iso space for proper visual angle
        int isoDx = dx - dy;  // simplified iso X component
        int isoDy = (dx + dy) / 2; // simplified iso Y component

        // Scale to 3px length
        int len2 = isoDx * isoDx + isoDy * isoDy;
        if (len2 > 0) {
            // Integer sqrt approximation: find scale factor for 3px
            int len = 1;
            while (len * len < len2) len++;
            int nx = isoDx * 3 / len;
            int ny = isoDy * 3 / len;

            // Draw 3-pixel arrow body
            draw_pixel(buf, sx, sy, PAL_YELLOW);
            draw_pixel(buf, sx - nx, sy - ny, PAL_YELLOW);
            draw_pixel(buf, sx - nx * 2 / 3, sy - ny * 2 / 3, PAL_ORANGE);
        } else {
            draw_pixel(buf, sx, sy, PAL_YELLOW);
        }
    }
}
