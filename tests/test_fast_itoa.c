#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa_internal.h"

void test_zero_allocation_itoa() {
    printf("Running test_zero_allocation_itoa...\n");
    char buffer[64];
    int len;
    (void)len;

    // Test positive integer
    len = tsa_fast_itoa(buffer, 12345);
    assert(len == 5);
    assert(strncmp(buffer, "12345", 5) == 0);

    // Test negative integer
    len = tsa_fast_itoa(buffer, -9876);
    assert(len == 5);
    assert(strncmp(buffer, "-9876", 5) == 0);

    // Test zero
    len = tsa_fast_itoa(buffer, 0);
    assert(len == 1);
    assert(strncmp(buffer, "0", 1) == 0);

    // Test float (2 decimal places for typical metrics)
    len = tsa_fast_ftoa(buffer, 123.45f, 2);
    assert(len == 6);
    assert(strncmp(buffer, "123.45", 6) == 0);

    // Test negative float
    len = tsa_fast_ftoa(buffer, -0.01f, 2);
    assert(len == 5);
    assert(strncmp(buffer, "-0.01", 5) == 0);

    printf("test_zero_allocation_itoa passed.\n");
}

int main() {
    test_zero_allocation_itoa();
    return 0;
}
