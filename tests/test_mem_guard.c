#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

void test_pes_quota() {
    printf("Running test_pes_quota...\n");
    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);

    h->pes_max_quota = 1024 * 1024;  // 1MB quota = 256 PIDs
    uint8_t pkt[188] = {0x47, 0x40, 0x00, 0x10};
    uint64_t now = 1000000000ULL;

    int allocated_count = 0;
    for (int p = 0; p < 8192; p++) {
        pkt[1] = 0x40 | ((p >> 8) & 0x1F);
        pkt[2] = p & 0xFF;
        h->live->pid_is_referenced[p] = true;
        tsa_process_packet(h, pkt, now + p * 1000);
        /* Zero-copy: An active PES accumulation is indicated by having packet references */
        if (h->es_tracks[p].pes.ref_count > 0) allocated_count++;
    }

    printf("Allocated PIDs: %d, Total Memory: %zu bytes\n", allocated_count, h->pes_total_allocated);
    assert(allocated_count <= 260);
    assert(h->pes_total_allocated <= h->pes_max_quota);

    tsa_destroy(h);
    printf("test_pes_quota passed.\n");
}

int main() {
    test_pes_quota();
    return 0;
}
