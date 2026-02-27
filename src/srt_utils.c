#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

// Extended SRT URL Parser: srt://host:port?mode=caller&latency=200&passphrase=abc&pbkeylen=16
int parse_srt_url_ext(const char* url, char* host, int* port, int* is_listener, int* latency, char* passphrase, int* pbkeylen) {
    if (strncmp(url, "srt://", 6) != 0) return -1;

    char buf[256];
    strncpy(buf, url + 6, sizeof(buf) - 1);

    char* query = strchr(buf, '?');
    if (query) *query++ = '\0';

    char* colon = strchr(buf, ':');
    if (!colon) return -1;
    *colon++ = '\0';

    strcpy(host, buf);
    *port = atoi(colon);

    // Defaults
    *is_listener = (strlen(host) == 0 || strcmp(host, "0.0.0.0") == 0);
    *latency = 120;
    if (passphrase) passphrase[0] = '\0';
    if (pbkeylen) *pbkeylen = 0;

    if (query) {
        char* token = strtok(query, "&");
        while (token) {
            if (strncmp(token, "mode=", 5) == 0) {
                if (strcmp(token + 5, "listener") == 0)
                    *is_listener = 1;
                else if (strcmp(token + 5, "caller") == 0)
                    *is_listener = 0;
            } else if (strncmp(token, "latency=", 8) == 0) {
                *latency = atoi(token + 8);
            } else if (strncmp(token, "passphrase=", 11) == 0 && passphrase) {
                strcpy(passphrase, token + 11);
            } else if (strncmp(token, "pbkeylen=", 9) == 0 && pbkeylen) {
                *pbkeylen = atoi(token + 9);
            }
            token = strtok(NULL, "&");
        }
    }
    return 0;
}
