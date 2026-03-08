#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TS_PACKET_SIZE 188
#define NS_PER_SEC 1000000000ULL

typedef struct {
    uint64_t start_ns;
    uint64_t pkts_sent;
    uint64_t br;
} pacer_state_t;

uint64_t calculate_to_send(pacer_state_t* s, uint64_t now_ns) {
    if (s->start_ns == 0) {
        s->start_ns = now_ns;
        return 0;
    }

    uint64_t elapsed_ns = now_ns - s->start_ns;
    uint64_t target_bits = (uint64_t)(((__int128)elapsed_ns * s->br) / NS_PER_SEC);
    uint64_t current_bits = s->pkts_sent * TS_PACKET_SIZE * 8;

    if (current_bits >= target_bits) return 0;

    uint64_t diff_bits = target_bits - current_bits;
    return diff_bits / (TS_PACKET_SIZE * 8);
}

void test_basic_pacing() {
    pacer_state_t s = {0, 0, 10000000};  // 10 Mbps

    uint64_t now = 1000000000ULL;  // 1s
    uint64_t to_send = calculate_to_send(&s, now);
    assert(s.start_ns == now);
    assert(to_send == 0);

    // Move 100ms forward
    now += 100000000ULL;
    to_send = calculate_to_send(&s, now);
    // 100ms at 10Mbps = 1,000,000 bits
    // Packets = 1,000,000 / (188*8) = 1,000,000 / 1504 = 664.89 -> 664 pkts
    printf("To send at 100ms: %lu (Expected: ~664)\n", to_send);
    assert(to_send >= 664 && to_send <= 665);

    s.pkts_sent += to_send;

    // Move another 1ms
    now += 1000000ULL;
    to_send = calculate_to_send(&s, now);
    // 1ms at 10Mbps = 10,000 bits
    // Packets = 10,000 / 1504 = 6.64 -> 6 pkts
    printf("To send at +1ms: %lu (Expected: ~6)\n", to_send);
    assert(to_send >= 6 && to_send <= 7);
}

int main() {
    test_basic_pacing();
    printf("Pacer logic test PASSED\n");
    return 0;
}
