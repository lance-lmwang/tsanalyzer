#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "tsa_stream_model.h"

void test_hierarchy() {
    printf("Testing Stream Model Hierarchy...\n");
    tsa_ts_model_t model;
    tsa_stream_model_init(&model);

    // 1. Add Program 101 with PMT PID 0x100
    tsa_stream_model_update_program(&model, 101, 0x100);
    assert(model.program_count == 1);
    assert(model.programs[0].program_number == 101);
    assert(model.programs[0].pmt_pid == 0x100);
    assert(model.pid_to_program_idx[0x100] == 0);

    // 2. Add ES to Program 101
    tsa_stream_model_update_es(&model, 0x100, 0x101, 0x1b); // H.264
    tsa_stream_model_update_es(&model, 0x100, 0x102, 0x0f); // AAC

    assert(model.programs[0].es_count == 2);
    assert(model.programs[0].es[0].pid == 0x101);
    assert(model.programs[0].es[0].stream_type == 0x1b);
    assert(model.programs[0].es[1].pid == 0x102);
    assert(model.programs[0].es[1].stream_type == 0x0f);

    // 3. Add Program 102 with PMT PID 0x200
    tsa_stream_model_update_program(&model, 102, 0x200);
    tsa_stream_model_update_es(&model, 0x200, 0x201, 0x24); // HEVC

    assert(model.program_count == 2);
    assert(model.programs[1].program_number == 102);
    assert(model.programs[1].es_count == 1);
    assert(model.programs[1].es[0].pid == 0x201);

    printf("  [PASS] Hierarchy validated.\n");
}

int main() {
    test_hierarchy();
    printf("ALL STREAM MODEL TESTS PASSED\n");
    return 0;
}
