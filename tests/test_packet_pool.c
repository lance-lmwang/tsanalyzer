#include <assert.h>
#include <stdio.h>

#include "tsa_log.h"
#include "tsa_packet_pool.h"

int main() {
    tsa_log_set_level(TSA_LOG_DEBUG);
    tsa_info("pool_test", "Starting packet pool tests...");

    tsa_packet_pool_t* pool = tsa_packet_pool_create(10);
    assert(pool != NULL);
    assert(pool->capacity == 10);

    tsa_packet_t* pkt1 = tsa_packet_pool_acquire(pool);
    assert(pkt1 != NULL);
    assert(pkt1->ref_count == 1);

    tsa_packet_ref(pkt1);
    assert(pkt1->ref_count == 2);

    tsa_packet_unref(pool, pkt1);
    assert(pkt1->ref_count == 1);

    tsa_packet_unref(pool, pkt1);
    assert(pkt1->ref_count == 0);  // Returned to pool

    tsa_packet_pool_destroy(pool);
    tsa_info("pool_test", "All packet pool tests PASSED.");
    return 0;
}
