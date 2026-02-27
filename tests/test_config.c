#include <assert.h>
#include <stdio.h>

#include "tsp.h"

int main() {
    tsp_config_t cfg;
    cfg.cpu_core = 5;
    assert(cfg.cpu_core == 5);
    printf("Test passed: cpu_core field exists and is assignable.\n");
    return 0;
}
