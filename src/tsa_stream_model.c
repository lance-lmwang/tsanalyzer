#include <string.h>
#include <stdio.h>
#include "tsa_stream_model.h"

void tsa_stream_model_init(tsa_ts_model_t *model) {
    if (!model) return;
    memset(model, 0, sizeof(tsa_ts_model_t));
    for (int i = 0; i < 8192; i++) {
        model->pid_to_program_idx[i] = -1;
    }
}

void tsa_stream_model_update_program(tsa_ts_model_t *model, uint16_t prog_num, uint16_t pmt_pid) {
    if (!model) return;

    int idx = -1;
    for (uint32_t i = 0; i < model->program_count; i++) {
        if (model->programs[i].program_number == prog_num) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        if (model->program_count >= MAX_PROGRAMS_PER_TS) return;
        idx = model->program_count++;
        model->programs[idx].program_number = prog_num;
    }

    model->programs[idx].pmt_pid = pmt_pid;
    model->programs[idx].active = true;

    if (pmt_pid < 8192) {
        model->pid_to_program_idx[pmt_pid] = (int16_t)idx;
    }
}

void tsa_stream_model_update_es(tsa_ts_model_t *model, uint16_t pmt_pid, uint16_t es_pid, uint8_t type) {
    if (!model || pmt_pid >= 8192) return;

    int16_t prog_idx = model->pid_to_program_idx[pmt_pid];
    if (prog_idx == -1) return;

    tsa_program_model_t *prog = &model->programs[prog_idx];

    int es_idx = -1;
    for (uint32_t i = 0; i < prog->es_count; i++) {
        if (prog->es[i].pid == es_pid) {
            es_idx = i;
            break;
        }
    }

    if (es_idx == -1) {
        if (prog->es_count >= MAX_ES_PER_PROGRAM) return;
        es_idx = prog->es_count++;
        prog->es[es_idx].pid = es_pid;
    }

    prog->es[es_idx].stream_type = type;
}
