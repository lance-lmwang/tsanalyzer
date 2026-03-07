#ifndef TSA_PIPELINE_H
#define TSA_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

/* --- Memory Model: Zero-Copy Packet --- */
typedef struct {
    uint8_t data[188];
    uint64_t ingress_time_ns;
    int ref_count;
} tsa_packet_t;

/* --- Pipeline Stage Interface --- */
typedef struct tsa_stage tsa_stage_t;

struct tsa_stage {
    const char* name;
    bool is_mutating;
    
    /**
     * Process a packet. For passive taps, the packet is read-only.
     * Mutating stages can modify the data.
     */
    void (*process)(tsa_stage_t* stage, tsa_packet_t* pkt);
    
    /**
     * Link to the next stage in the pipeline.
     */
    tsa_stage_t* next;
    
    void* private_data;
};

/* --- Pipeline Manager --- */
typedef struct {
    tsa_stage_t* head;
    tsa_stage_t* tail;
} tsa_pipeline_t;

void tsa_pipeline_init(tsa_pipeline_t* p);
void tsa_pipeline_add_stage(tsa_pipeline_t* p, tsa_stage_t* stage);
void tsa_pipeline_dispatch(tsa_pipeline_t* p, tsa_packet_t* pkt);

#endif
