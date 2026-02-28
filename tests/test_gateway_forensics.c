#include <assert.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "tsa.h"
#include "tsa_internal.h"

void test_auto_forensics() {
    printf("Running test_auto_forensics...\n");
    tsa_gateway_config_t cfg = {0};
    cfg.enable_auto_forensics = true;
    cfg.forensic_ring_size = 1000;

    tsa_gateway_t* gw = tsa_gateway_create(&cfg);
    assert(gw != NULL);

    tsa_handle_t* tsa = tsa_gateway_get_tsa_handle(gw);
    uint8_t pkt[188];
    memset(pkt, 0, 188);
    pkt[0] = 0x47;
    uint64_t now = 1000000000ULL;

    for (int i = 0; i < 100; i++) {
        tsa_gateway_process(gw, pkt, now + i * 1000);
    }

    tsa->live.cc_error.count++;
    tsa_commit_snapshot(tsa, now + 200000);

    tsa_gateway_process(gw, pkt, now + 200000000ULL);

    glob_t g;
    if (glob("forensic_*.ts", 0, NULL, &g) != 0) {
        printf("FAILED: No forensic file found.\n");
        assert(0);
    }
    printf("Created %zu forensic files.\n", g.gl_pathc);
    assert(g.gl_pathc > 0);

    for (size_t i = 0; i < g.gl_pathc; i++) {
        unlink(g.gl_pathv[i]);
    }
    globfree(&g);

    tsa_gateway_destroy(gw);
    printf("test_auto_forensics passed.\n");
}

int main() {
    test_auto_forensics();
    return 0;
}
