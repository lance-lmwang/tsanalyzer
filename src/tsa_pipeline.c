#include <stdlib.h>
#include "tsa_pipeline.h"

void tsa_pipeline_init(tsa_pipeline_t* p) {
    p->head = NULL;
    p->tail = NULL;
}

void tsa_pipeline_add_stage(tsa_pipeline_t* p, tsa_stage_t* stage) {
    if (!stage) return;
    stage->next = NULL;
    
    if (!p->head) {
        p->head = stage;
        p->tail = stage;
    } else {
        p->tail->next = stage;
        p->tail = stage;
    }
}

void tsa_pipeline_dispatch(tsa_pipeline_t* p, tsa_packet_t* pkt) {
    if (!p || !pkt) return;
    
    tsa_stage_t* curr = p->head;
    while (curr) {
        if (curr->process) {
            curr->process(curr, pkt);
        }
        curr = curr->next;
    }
}
