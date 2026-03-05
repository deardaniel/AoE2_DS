#pragma once
#include <maxmod9.h>
#include "soundbank.h"
#include "soundbank_bin.h"

// Map friendly names to mmutil-generated SFX IDs
enum SoundFX {
    SFX_SWORD_HIT         = SFX_SFX_SWORD,
    SFX_ARROW_FIRE        = SFX_SFX_ARROW,
    SFX_BUILDING_PLACE    = SFX_SFX_BUILD_PLACE,
    SFX_BUILDING_COMPLETE = SFX_SFX_COMPLETE,
    SFX_UNIT_DEATH        = SFX_SFX_DEATH,
    SFX_CHOP              = SFX_SFX_CHOP,
    SFX_CLICK             = SFX_SFX_CLICK,
};

// Villager voice ranges (Britons)
#define SFX_VILL_CMD_FIRST  SFX_SFX_VILL_CMD1
#define SFX_VILL_CMD_COUNT  4
#define SFX_VILL_SEL_FIRST  SFX_SFX_VILL_SEL1
#define SFX_VILL_SEL_COUNT  3

void sound_init();
void sound_play(int sfxId);
void sound_play_random(int firstId, int count);

void sound_music_start();
void sound_music_stop();
void sound_music_toggle();
bool sound_music_playing();
void sound_music_update();
