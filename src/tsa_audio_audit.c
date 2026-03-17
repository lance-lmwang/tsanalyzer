#include <ebur128.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "AUDIO_AUDIT"

/**
 * Initialize loudness engine for a PID.
 */
void tsa_audio_audit_init(tsa_es_track_t* es) {
    if (!es) return;
    if (es->audio.ebur128_state) return;

    // Default to 48kHz Stereo for now (Common in Broadcast)
    // In production, this should be updated dynamically from ADTS/AC3 headers
    uint32_t sr = es->audio.sample_rate ? es->audio.sample_rate : 48000;
    uint8_t ch = es->audio.channels ? es->audio.channels : 2;

    ebur128_state* st = ebur128_init(ch, sr, EBUR128_MODE_M);
    if (!st) {
        tsa_error(TAG, "Failed to initialize libebur128 for PID 0x%04x", es->pid);
        return;
    }

    es->audio.ebur128_state = st;
    es->audio.sample_rate = sr;
    es->audio.channels = ch;
    es->audio.loudness_lufs = -70.0f;
    es->audio.peak_db = -100.0f;
}

void tsa_audio_audit_destroy(tsa_es_track_t* es) {
    if (!es || !es->audio.ebur128_state) return;
    ebur128_destroy((ebur128_state**)&es->audio.ebur128_state);
}

/**
 * Process raw PCM samples extracted from the bitstream.
 * Expects interleaved float samples.
 */
void tsa_audio_audit_feed_pcm(tsa_es_track_t* es, const float* samples, size_t frames) {
    if (!es || !es->audio.ebur128_state || !samples || frames == 0) return;

    ebur128_state* st = (ebur128_state*)es->audio.ebur128_state;
    ebur128_add_frames_float(st, samples, frames);

    double momentary;
    if (ebur128_loudness_momentary(st, &momentary) == EBUR128_SUCCESS) {
        es->audio.loudness_lufs = (float)momentary;
    }

    // Update Peak (Simple max check for now)
    for (size_t i = 0; i < frames * es->audio.channels; i++) {
        float val = (samples[i] < 0) ? -samples[i] : samples[i];
        if (val > 0) {
            float db = 20.0f * (float)log10((double)val);
            if (db > es->audio.peak_db) es->audio.peak_db = db;
        }
    }
}

/**
 * Placeholder for ES bitstream to PCM extraction.
 * This would normally integrate with a lightweight AAC/AC3 decoder.
 */
void tsa_audio_audit_process_es(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len) {
    (void)h;
    (void)payload;
    (void)len;
    tsa_es_track_t* es = &h->es_tracks[pid];

    if (!tsa_is_audio(es->stream_type)) return;

    if (!es->audio.ebur128_state) {
        tsa_audio_audit_init(es);
    }

    /*
     * TODO: Integrate FDK-AAC or libavcodec here to extract PCM.
     * For MVP, we provide the infrastructure and libebur128 integration.
     */
}
