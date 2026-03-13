#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_descriptors.h"
#include "tsa_internal.h"

// Mocking required internal structures if not fully available via headers
// but since we include tsa_internal.h it should be fine.

void test_metadata_parsing() {
    printf("Testing metadata parsing (Service/LCN)...\n");

    tsa_handle_t h;
    memset(&h, 0, sizeof(h));
    tsa_stream_model_init(&h.ts_model);
    tsa_descriptors_init();

    // 1. Create a program in the model
    uint16_t prog_num = 101;
    uint16_t pmt_pid = 0x100;
    tsa_stream_model_update_program(&h.ts_model, prog_num, pmt_pid);

    // 2. Mock Service Descriptor (0x48)
    // tag 0x48, len 15, service_type 0x01, provider_len 4 "TEST", service_len 8 "SERVICE1"
    uint8_t service_desc[] = {0x48, 15, 0x01, 4, 'T', 'E', 'S', 'T', 8, 'S', 'E', 'R', 'V', 'I', 'C', 'E', '1'};

    // Process it as a program-level descriptor (using pmt_pid)
    tsa_descriptors_process(&h, pmt_pid, service_desc, sizeof(service_desc), NULL);

    tsa_program_model_t *p = &h.ts_model.programs[0];
    (void)p;
    assert(strcmp(p->provider_name, "TEST") == 0);
    assert(strcmp(p->service_name, "SERVICE1") == 0);
    printf("  [PASS] Service Descriptor parsed correctly from PMT PID.\n");

    // 3. Mock LCN Descriptor (0x83)
    // tag 0x83, len 4, service_id 101, visible 1, lcn 12
    // data[0]=0, data[1]=0x65 (101)
    // data[2]=0x80 (visible) | 0x00 (lcn high)
    // data[3]=12 (0x0C)
    uint8_t lcn_desc[] = {0x83, 4, 0x00, 0x65, 0x80, 12};

    tsa_descriptors_process(&h, pmt_pid, lcn_desc, sizeof(lcn_desc), NULL);
    assert(p->lcn == 12);
    printf("  [PASS] LCN Descriptor parsed correctly.\n");

    // 4. Test parsing via Service ID (SDT style)
    uint16_t sid = 101;
    uint8_t service_desc2[] = {0x48, 15, 0x01, 4, 'A', 'B', 'C', 'D', 8, 'S', 'E', 'R', 'V', 'I', 'C', 'E', '2'};
    tsa_descriptors_process(&h, sid, service_desc2, sizeof(service_desc2), NULL);
    assert(strcmp(p->provider_name, "ABCD") == 0);
    assert(strcmp(p->service_name, "SERVICE2") == 0);
    printf("  [PASS] Service Descriptor parsed correctly via Service ID.\n");
}

int main() {
    test_metadata_parsing();
    printf("ALL METADATA TESTS PASSED\n");
    return 0;
}
