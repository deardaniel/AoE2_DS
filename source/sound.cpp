#include "sound.h"
#include <nds.h>
#include <stdlib.h>
#include <stdio.h>

#define SFX_COUNT 14

// Music streaming state
static bool musicPlaying = false;
static FILE* musicFile = NULL;

// Stream callback: reads PCM directly from NitroFS file, loops at EOF
// Called from mmStreamUpdate() in main thread, so fread() is safe
static mm_word stream_fill_callback(mm_word length, mm_addr dest, mm_stream_formats format) {
    s16* out = (s16*)dest;
    mm_word samplesRead = 0;

    while (samplesRead < length) {
        mm_word remaining = length - samplesRead;
        size_t got = fread(&out[samplesRead], sizeof(s16), remaining, musicFile);
        if (got == 0) {
            // EOF — loop back to start
            fseek(musicFile, 0, SEEK_SET);
        }
        // Attenuate samples we just read (~50% volume)
        for (size_t i = 0; i < got; i++) {
            out[samplesRead + i] >>= 1;
        }
        samplesRead += got;
    }
    return length;
}

void sound_init() {
    mmInitDefaultMem((mm_addr)soundbank_bin);
    for (int i = 0; i < SFX_COUNT; i++) {
        mmLoadEffect(i);
    }

    // Open music file from NitroFS (keep open for streaming)
    musicFile = fopen("nitro:/music_game.bin", "rb");
}

void sound_play(int sfxId) {
    mmEffect(sfxId);
}

void sound_play_random(int firstId, int count) {
    mmEffect(firstId + (rand() % count));
}

void sound_music_start() {
    if (musicPlaying || !musicFile) return;
    fseek(musicFile, 0, SEEK_SET);

    mm_stream stream;
    stream.sampling_rate = 8000;
    stream.buffer_length = 1024;
    stream.callback = stream_fill_callback;
    stream.format = MM_STREAM_16BIT_MONO;
    stream.timer = MM_TIMER2;
    stream.manual = true;

    mmStreamOpen(&stream);
    musicPlaying = true;
}

void sound_music_stop() {
    if (!musicPlaying) return;
    mmStreamClose();
    musicPlaying = false;
}

void sound_music_toggle() {
    if (musicPlaying) sound_music_stop();
    else sound_music_start();
}

bool sound_music_playing() {
    return musicPlaying;
}

void sound_music_update() {
    if (musicPlaying) {
        mmStreamUpdate();
    }
}
