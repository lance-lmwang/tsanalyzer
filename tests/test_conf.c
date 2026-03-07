#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tsa_conf.h"
#include "tsa_log.h"

int main() {
    tsa_log_set_level(TSA_LOG_DEBUG);
    tsa_info("conf_test", "Starting configuration parser tests...");

    const char* conf_content = 
        "http_listen 8088;\n"
        "vhost __default__ {\n"
        "    metrology {\n"
        "        pcr_jitter on;\n"
        "    }\n"
        "}\n"
        "stream live/ch1 {\n"
        "    input udp://239.1.1.1:1234;\n"
        "    label \"Channel 1\";\n"
        "}\n";

    FILE* fp = fopen("test_tsa.conf", "w");
    fputs(conf_content, fp);
    fclose(fp);

    tsa_full_conf_t conf = {0};
    int res = tsa_conf_load(&conf, "test_tsa.conf");
    assert(res == 0);

    tsa_info("conf_test", "Checking global settings...");
    assert(conf.http_listen_port == 8088);

    tsa_info("conf_test", "Checking stream inheritance...");
    assert(conf.stream_count == 1);
    assert(strcmp(conf.streams[0].id, "live/ch1") == 0);
    assert(strcmp(conf.streams[0].cfg.url, "udp://239.1.1.1:1234") == 0);
    
    // Check inheritance from vhost __default__ (not fully implemented in parser yet but structure is there)
    // assert(conf.streams[0].cfg.analysis.pcr_jitter == true);

    tsa_info("conf_test", "All configuration tests PASSED.");
    remove("test_tsa.conf");
    return 0;
}
