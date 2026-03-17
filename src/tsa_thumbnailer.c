#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "nalu_sniffer.h"
#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "THUMBNAILER"

typedef struct {
    char output_dir[256];
    uint64_t last_thumbnail_ns;
    uint64_t interval_ns;
} tsa_thumbnailer_ctx_t;

void tsa_thumbnailer_init(tsa_handle_t* h, const char* out_dir, uint64_t interval_ms) {
    if (!h) return;

    // We store the context in the first available plugin slot or a dedicated field if we added one
    // For now, let's just use a static context for simplicity in this MVP phase,
    // or attach it to the handle if we modify the struct.
    // Given the constraints, let's assume we can attach it as a plugin later.
    // For this implementation, we will perform the logic directly in the ES handler for now
    // but keep the code modular here.
}

void tsa_thumbnailer_process(tsa_handle_t* h, uint16_t pid, const uint8_t* payload, int len,
                             const tsa_nalu_info_t* nalu) {
    if (!h || !payload || len <= 0 || !nalu) return;

    // Only process IDR frames for thumbnails
    if (nalu->nalu_type_abstract != NALU_TYPE_IDR) return;

    // Rate limit: 1 thumbnail per 10 seconds per PID (hardcoded for MVP)
    uint64_t now = h->stc_ns;
    // In a real implementation, we'd check h->es_tracks[pid].last_thumbnail_ns
    // But we haven't added that field yet. Let's just log it for now to verify detection.

    // Check if we should dump this IDR
    // For the MVP, we will dump the first IDR seen after startup to a file

    static bool dumped_once = false;
    if (!dumped_once) {
        char filename[512];
        snprintf(filename, sizeof(filename), "/tmp/tsa_thumb_pid_%d.h264", pid);
        FILE* f = fopen(filename, "wb");
        if (f) {
            // Write Start Code
            uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
            fwrite(start_code, 1, 4, f);
            fwrite(payload, 1, len, f);
            fclose(f);
            tsa_info(TAG, "Dumped IDR frame for PID %d to %s", pid, filename);
            dumped_once = true;
        }
    }
}
