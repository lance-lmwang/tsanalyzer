#ifndef TSA_SHAPER_H
#define TSA_SHAPER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tsa_shaper tsa_shaper_t;

// Context Management
tsa_shaper_t* tsa_shaper_create(uint64_t total_bitrate);
void tsa_shaper_destroy(tsa_shaper_t* ctx);

// Program & Bitrate Control
int tsa_shaper_add_program(tsa_shaper_t* ctx, int program_id);
int tsa_shaper_set_program_bitrate(tsa_shaper_t* ctx, int program_id, uint64_t bps);

// Data Ingest (Opaque Push)
int tsa_shaper_push(tsa_shaper_t* ctx, int program_id, const uint8_t* ts_packet);

// Output Protocol Selection
typedef enum {
    TSA_OUT_UDP,
    TSA_OUT_RTP,
    TSA_OUT_SRT,
    TSA_OUT_FILE
} tsa_output_mode_t;

int tsa_shaper_set_output(tsa_shaper_t* ctx, tsa_output_mode_t mode, const char* url);

// Performance Monitoring
typedef struct {
    uint64_t bytes_sent;
    double pcr_jitter_ns;
    double buffer_fullness_avg;
} tsa_shaper_stats_t;

void tsa_shaper_get_stats(tsa_shaper_t* ctx, tsa_shaper_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif // TSA_SHAPER_H
