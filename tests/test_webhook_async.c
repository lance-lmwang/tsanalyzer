#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tsa_webhook.h"

int main() {
    // We don't actually need a server to test the queue and thread logic
    // We test that the engine starts, accepts messages, and shuts down cleanly.
    printf("Testing Webhook Engine (Async)...\n");

    tsa_webhook_engine_t* eng = tsa_webhook_init("http://127.0.0.1:9999/unused");
    assert(eng != NULL);

    tsa_webhook_push(eng, "{\"test\":\"data1\"}");
    tsa_webhook_push_event(eng, "test_stream", "UNIT_TEST", "Hello World", "INFO");

    // Let the worker thread pick up messages
    usleep(100000);

    tsa_webhook_destroy(eng);
    printf("Webhook Engine Test: PASSED (Clean startup/shutdown)\n");
    return 0;
}
