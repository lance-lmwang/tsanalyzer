#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
#include "tsa_internal.h"

int main() {
    printf(">>> STARTING FORENSIC OFFSET UNIT TEST <<<\n");

    tsa_config_t cfg = {0};
    cfg.op_mode = TSA_MODE_REPLAY;
    tsa_handle_t* h = tsa_create(&cfg);
    assert(h != NULL);

    /* Construct two packets:
     * Packet 1: OK, PID 0x100
     * Packet 2: SCRAMBLED, PID 0x100
     */
    uint8_t buf[188 * 2];
    memset(buf, 0, sizeof(buf));

    // Packet 1
    buf[0] = 0x47;
    buf[1] = 0x01;
    buf[2] = 0x00;
    buf[3] = 0x10;  // Payload

    // Packet 2
    buf[188] = 0x47;
    buf[189] = 0x01;
    buf[190] = 0x00;
    buf[191] = 0x90;  // Scrambled (Transport Scrambling Control = 10)

    /* Lock engine */
    h->signal_lock = true;
    h->sync_state = TS_SYNC_LOCKED;

    /* Feed both packets at once to test offset calculation inside loop */
    tsa_feed_data(h, buf, 188 * 2, 1000000000ULL);

    /* First packet starts at offset 0, second starts at offset 188.
     * The scrambled event happens on the second packet. */
    printf("Processed Bytes: %llu\n", (unsigned long long)h->processed_bytes);
    assert(h->processed_bytes == 188 * 2);

    /* Verify Event Queue */
    size_t head = atomic_load_explicit(&h->event_q->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&h->event_q->tail, memory_order_relaxed);

    bool found_scrambled = false;
    uint64_t scrambled_offset = 0;
    (void)found_scrambled;
    while (tail < head) {
        tsa_event_t* ev = &h->event_q->events[tail % MAX_EVENT_QUEUE];
        if (ev->type == TSA_EVENT_SCRAMBLED) {
            found_scrambled = true;
            scrambled_offset = ev->absolute_byte_offset;
            break;
        }
        tail++;
    }

    printf("Scrambled Event Offset: %llu\n", (unsigned long long)scrambled_offset);
    assert(found_scrambled);
    assert(scrambled_offset == 188);

    tsa_destroy(h);
    printf(">>> FORENSIC OFFSET UNIT TEST PASSED <<<\n");
    return 0;
}
