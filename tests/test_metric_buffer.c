#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

void test_metric_buffer() {
    printf("Running test_metric_buffer...\n");

    char storage[128];
    tsa_metric_buffer_t buf;

    // Test initialization
    tsa_mbuf_init(&buf, storage, sizeof(storage));
    assert(buf.base == storage);
    assert(buf.capacity == 128);
    assert(buf.offset == 0);

    // Test string append
    tsa_mbuf_append_str(&buf, "metric_name{label=\"");
    assert(buf.offset == 19);

    // Test int append
    tsa_mbuf_append_int(&buf, 42);
    assert(buf.offset == 21);

    // Test string append again
    tsa_mbuf_append_str(&buf, "\"} ");
    assert(buf.offset == 24);

    // Test float append
    tsa_mbuf_append_float(&buf, 3.14f, 2);
    assert(buf.offset == 28);

    // Test newline
    tsa_mbuf_append_char(&buf, '\n');
    assert(buf.offset == 29);

    // Verify content
    buf.base[buf.offset] = '\0';  // Null terminate just for strcmp
    assert(strcmp(buf.base, "metric_name{label=\"42\"} 3.14\n") == 0);

    // Test overflow safety
    // We have 128 capacity, currently at 29. Let's append a long string (100 chars).
    // Remaining capacity is 99 (128 - 29). The append should truncate safely.
    char long_str[101];
    memset(long_str, 'A', 100);
    long_str[100] = '\0';

    tsa_mbuf_append_str(&buf, long_str);
    assert(buf.offset == buf.capacity);  // Should be full

    // Verify it doesn't write past capacity
    assert(storage[127] == 'A');

    printf("test_metric_buffer passed.\n");
}

int main() {
    test_metric_buffer();
    return 0;
}
