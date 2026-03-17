#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

// Forward declaration from src/tsa_audio_audit.c
void tsa_audio_audit_init(tsa_es_track_t* es);
void tsa_audio_audit_destroy(tsa_es_track_t* es);
void tsa_audio_audit_feed_pcm(tsa_es_track_t* es, const float* samples, size_t frames);

int main() {
    printf("Testing Audio Loudness Engine (libebur128 Integration)...\n");

    tsa_es_track_t es;
    memset(&es, 0, sizeof(es));
    es.pid = 0x101;
    es.stream_type = TSA_TYPE_AUDIO_AAC;

    tsa_audio_audit_init(&es);
    assert(es.audio.ebur128_state != NULL);
    printf("  [PASS] libebur128 state initialized\n");

    // Feed a 1kHz sine wave at -20dBFS for 1 second
    int sr = 48000;
    int ch = 2;
    int total_frames = sr;  // 1 second
    float* pcm = malloc(total_frames * ch * sizeof(float));

    float amplitude = 0.1f;  // ~ -20dB
    for (int i = 0; i < total_frames; i++) {
        float val = amplitude * sinf(2.0f * M_PI * 1000.0f * i / sr);
        pcm[i * 2] = val;      // Left
        pcm[i * 2 + 1] = val;  // Right
    }

    // Feed in chunks of 480 frames (10ms)
    int chunk_size = 480;
    for (int i = 0; i < total_frames; i += chunk_size) {
        tsa_audio_audit_feed_pcm(&es, pcm + (i * ch), chunk_size);
    }

    printf("  [INFO] Momentary Loudness: %.2f LUFS\n", es.audio.loudness_lufs);
    printf("  [INFO] Peak Level: %.2f dB\n", es.audio.peak_db);

    // Assert reasonable values for a -20dB sine wave
    // (Loudness is slightly different from peak for sine waves due to weighting)
    assert(es.audio.loudness_lufs > -25.0f && es.audio.loudness_lufs < -15.0f);
    assert(es.audio.peak_db > -21.0f && es.audio.peak_db < -19.0f);

    printf("  [PASS] Loudness and Peak values within expected range\n");

    tsa_audio_audit_destroy(&es);
    assert(es.audio.ebur128_state == NULL);
    printf("  [PASS] Cleanup successful\n");

    free(pcm);
    printf("ALL LOUDNESS TESTS PASSED\n");
    return 0;
}
