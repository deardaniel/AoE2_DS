#include "sound.h"

// Stub implementation
// To enable sound: add maxmod library, create soundbank with mmutil,
// and replace these stubs with real mmEffect calls.

void sound_init() {
    // TODO: mmInitDefaultMem((mm_addr)soundbank_bin);
}

void sound_play(int sfxId) {
    (void)sfxId;
    // TODO: mmEffect(sfxId);
}
