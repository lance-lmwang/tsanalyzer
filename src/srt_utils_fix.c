#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "tsa.h"

int parse_srt_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase, int* pbkeylen) {
    if (!url || !host || !port) return -1;
    *is_listener = 0; *latency = 250; passphrase[0] = '\0'; *pbkeylen = 0;
    
    char tmp[256]; strncpy(tmp, url, 255);
    char* p = strstr(tmp, "://");
    if (!p) return -1;
    p += 3;
    
    if (*p == ':') {
        *is_listener = 1;
        strcpy(host, "0.0.0.0");
        *port = atoi(p + 1);
    } else {
        char* p_port = strchr(p, ':');
        if (p_port) {
            *p_port = '\0';
            strcpy(host, p);
            *port = atoi(p_port + 1);
        } else return -1;
    }
    
    char* q = strchr(p, '?');
    if (q) {
        if (strstr(q, "mode=listener")) *is_listener = 1;
        char* l = strstr(q, "latency=");
        if (l) *latency = atoi(l + 8);
    }
    return 0;
}
