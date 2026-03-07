#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa.h"

void test_simple_config() {
    tsa_config_t cfg = {0};
    (void)cfg;
}

int main() {
    test_simple_config();
    printf("Config test passed!\n");
    return 0;
}
