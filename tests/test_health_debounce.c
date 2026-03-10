#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

void generate_packet(uint8_t* pkt, uint16_t pid, uint8_t cc, bool transport_error) {
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    pkt[1] = (pid >> 8) & 0x1F;
    if (transport_error) pkt[1] |= 0x80;
    pkt[2] = pid & 0xFF;
    pkt[3] = 0x10 | (cc & 0x0F);  // Payload only, no AF
}

int main() {
    printf("Testing Health & Alert Engine Model...\n");

    tsa_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.analysis.tr101290 = true;

    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    uint8_t pkt[188];
    uint64_t now = 1000000000ULL;  // 1s
    h->stc_ns = now;

    // 1. Initial State - Perfect Health
    h->signal_lock = true;
    tsa_commit_snapshot(h, now);
    printf("Initial Health: %.2f\n", h->last_health_score);
    assert(h->last_health_score > 99.0);

    // 2. CC Error (Single Packet)
    // Send some good packets first to establish state
    for (int i = 0; i < 10; i++) {
        generate_packet(pkt, 0x100, i % 16, false);
        tsa_process_packet(h, pkt, now + (i * 1000000ULL));
    }

    // Trigger one CC error
    generate_packet(pkt, 0x100, 12, false);  // Jump from 9 to 12
    tsa_process_packet(h, pkt, now + (10 * 1000000ULL));

    // In the new engine, CC error triggers FIRING immediately for safety
    tsa_commit_snapshot(h, now + (11 * 1000000ULL));
    bool is_fired = (h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_FIRING);
    printf("Health after transient CC: %.2f (Alarm: %s)\n", h->last_health_score, is_fired ? "FIRED" : "CLEAN");
    assert(is_fired == true);
    assert(h->last_health_score < 80.0);  // Deducted 25 points

    // 3. Recovery (Wait 5000ms - Default PID timeout)
    uint64_t recovery_start = now + (300 * 1000000ULL);
    h->stc_ns = recovery_start + 6000000000ULL;  // Jump forward 6s

    // Process one good packet to trigger cleanup check
    generate_packet(pkt, 0x100, 0, false);
    tsa_process_packet(h, pkt, h->stc_ns);

    tsa_alert_check_resolutions(h);  // Explicitly call resolution check
    tsa_commit_snapshot(h, h->stc_ns);

    is_fired = (h->alerts[TSA_ALERT_CC].status == TSA_ALERT_STATE_FIRING);
    printf("Health after recovery: %.2f (Alarm: %s)\n", h->last_health_score, is_fired ? "FIRED" : "CLEAN");
    assert(is_fired == false);
    assert(h->last_health_score > 90.0);

    tsa_destroy(h);
    printf("Test PASSED!\n");
    return 0;
}
