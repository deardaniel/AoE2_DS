#pragma once
#include "config.h"

// Sound effect IDs
enum SoundFX {
    SFX_SWORD_HIT = 0,
    SFX_ARROW_FIRE,
    SFX_BUILDING_PLACE,
    SFX_BUILDING_COMPLETE,
    SFX_VILLAGER_YES,
    SFX_UNIT_DEATH,
    SFX_AGE_UP,
    SFX_COUNT
};

// Stub implementation — sound will be added later with maxmod
void sound_init();
void sound_play(int sfxId);
