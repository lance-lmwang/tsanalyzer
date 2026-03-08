#ifndef TSA_STREAM_MODEL_H
#define TSA_STREAM_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/**
 * TSA Stream Model: A hierarchical representation of the Transport Stream.
 * Inspired by libltntstools streammodel.c
 */

#define MAX_ES_PER_PROGRAM 32
#define MAX_PROGRAMS_PER_TS 64

typedef struct {
    uint16_t pid;
    uint8_t stream_type;
    char codec_name[16];
    uint32_t bitrate_bps;
    void *private_data;  // For engine-specific state
} tsa_es_model_t;

typedef struct {
    uint16_t program_number;
    uint16_t pmt_pid;
    uint16_t pcr_pid;
    uint16_t lcn;
    char service_name[256];
    char provider_name[256];

    uint32_t es_count;
    tsa_es_model_t es[MAX_ES_PER_PROGRAM];

    bool active;
    uint32_t version;
} tsa_program_model_t;

typedef struct {
    uint16_t ts_id;
    uint32_t program_count;
    tsa_program_model_t programs[MAX_PROGRAMS_PER_TS];

    // Fast lookup maps
    int16_t pid_to_program_idx[8192];
    uint32_t pat_version;
} tsa_ts_model_t;

/**
 * Initializes the stream model.
 */
void tsa_stream_model_init(tsa_ts_model_t *model);

/**
 * Updates or adds a program to the model.
 */
void tsa_stream_model_update_program(tsa_ts_model_t *model, uint16_t prog_num, uint16_t pmt_pid);

/**
 * Updates an ES entry within a program.
 */
void tsa_stream_model_update_es(tsa_ts_model_t *model, uint16_t pmt_pid, uint16_t es_pid, uint8_t type);

#endif /* TSA_STREAM_MODEL_H */
