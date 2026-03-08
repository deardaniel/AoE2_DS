#ifndef PROJECTILES_H
#define PROJECTILES_H

#include <nds.h>

struct GameState;

enum { MAX_PROJECTILES = 16 };

struct Projectile {
    s16 srcX, srcY;
    s16 dstX, dstY;
    s16 curX, curY;
    u8  frame;
    u8  lifetime;
    s8  targetUnit;
    u8  damage;
    u8  attackerOwner;
    bool active;
};

extern Projectile projectiles[MAX_PROJECTILES];

void projectiles_init();
void projectile_spawn(s16 srcX, s16 srcY, s16 dstX, s16 dstY,
                       u8 lifetime, s8 targetUnit, u8 damage, u8 owner);
void projectiles_update(GameState& gs);
void render_projectiles(u8* buf, const GameState& gs);

#endif
