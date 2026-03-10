#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

/**
 * @brief Stress test for PCR/PTS rollover recovery.
 * Simulates multiple 26.5 hour rollovers to ensure 64-bit monotonicity.
 */
int main() {
    printf(">>> STARTING PCR ROLLOVER STRESS TEST <<<\n");

    tsa_config_t cfg = {0};
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint16_t pid = 0x100;

    /* 1. Initial State */
    uint64_t pts = 0x1FFFFFFF0ULL;  // Near rollover

    uint64_t recovered = tsa_recover_pts_64(h, pid, pts);
    printf("Initial recovered PTS: %llu\n", (unsigned long long)recovered);
    assert(recovered == 0x1FFFFFFF0ULL);

    /* Packet just after rollover */
    pts = 0x000000010ULL;
    recovered = tsa_recover_pts_64(h, pid, pts);
    printf("PTS after 1st rollover: %llu\n", (unsigned long long)recovered);
    assert(recovered == 0x200000010ULL);

    /* Simulate 2nd Rollover */
    h->es_tracks[pid].pes.last_pts_33 = 0x1FFFFFFF0ULL;
    pts = 0x000000010ULL;
    recovered = tsa_recover_pts_64(h, pid, pts);
    printf("PTS after 2nd rollover: %llu\n", (unsigned long long)recovered);
    assert(recovered == 0x400000010ULL);

    /* Simulate 100 Rollovers */
    for (int i = 0; i < 100; i++) {
        h->es_tracks[pid].pes.last_pts_33 = 0x1FFFFFFF0ULL;
        pts = 0x000000010ULL;
        recovered = tsa_recover_pts_64(h, pid, pts);
    }
    printf("PTS after 102 rollovers: %llu\n", (unsigned long long)recovered);
    assert(recovered == (0x200000000ULL * 102) + 0x10);

    /* Test Rollback (Seek backwards) */
    h->es_tracks[pid].pes.last_pts_33 = 0x000000010ULL;
    pts = 0x1FFFFFFF0ULL;
    recovered = tsa_recover_pts_64(h, pid, pts);
    printf("PTS after rollback: %llu\n", (unsigned long long)recovered);
    assert(recovered == (0x200000000ULL * 101) + 0x1FFFFFFF0ULL);

    tsa_destroy(h);
    printf(">>> PCR ROLLOVER STRESS TEST PASSED <<<\n");
    return 0;
}
