#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tsa.h"
int main() {
    tsa_config_t cfg = {.pcr_ema_alpha = 0.01};
    tsa_handle_t* h = tsa_create(&cfg);
    char buf[4096];
    tsa_export_prometheus(h, buf, sizeof(buf));
    // Verify new metric names after terminology alignment
    assert(strstr(buf, "tsa_continuity_errors_total") != NULL);
    tsa_destroy(h);
    return 0;
}
