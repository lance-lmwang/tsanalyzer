#include "tsa_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_conf_lexer.h"
#include "tsa_log.h"
#include "tsa_units.h"

typedef struct {
    tsa_lexer_t lex;
    tsa_token_t lookahead;
    tsa_full_conf_t* conf;
} parser_t;

static void next_token(parser_t* p) {
    p->lookahead = tsa_lexer_next(&p->lex);
}

static bool match(parser_t* p, tsa_token_type_t type) {
    if (p->lookahead.type == type) {
        next_token(p);
        return true;
    }
    return false;
}

static void parse_metrology(parser_t* p, tsa_config_t* cfg) {
    match(p, TSA_TOKEN_LBRACE);
    while (p->lookahead.type == TSA_TOKEN_WORD) {
        char key[TSA_ID_MAX];
        snprintf(key, sizeof(key), "%s", p->lookahead.text);
        next_token(p);

        char val[TSA_ID_MAX];
        snprintf(val, sizeof(val), "%s", p->lookahead.text);
        next_token(p);
        match(p, TSA_TOKEN_SEMICOLON);

        if (strcmp(key, "pcr_jitter") == 0)
            cfg->analysis.pcr_jitter = tsa_units_to_bool(val);
        else if (strcmp(key, "drift_analysis") == 0)
            cfg->analysis.pcr_ema_alpha = 0.05;
    }
    match(p, TSA_TOKEN_RBRACE);
}

static void parse_stream(parser_t* p, const char* id) {
    if (p->conf->stream_count >= MAX_STREAMS_IN_CONF) return;
    tsa_stream_conf_t* sc = &p->conf->streams[p->conf->stream_count++];
    snprintf(sc->id, sizeof(sc->id), "%s", id);

    // Inherit from default vhost initially
    memcpy(&sc->cfg, &p->conf->vhost_default, sizeof(tsa_config_t));

    match(p, TSA_TOKEN_LBRACE);
    while (p->lookahead.type == TSA_TOKEN_WORD) {
        char block[TSA_ID_MAX];
        snprintf(block, sizeof(block), "%s", p->lookahead.text);
        next_token(p);

        if (strcmp(block, "input") == 0) {
            snprintf(sc->cfg.url, sizeof(sc->cfg.url), "%s", p->lookahead.text);
            next_token(p);
            match(p, TSA_TOKEN_SEMICOLON);
        } else if (strcmp(block, "label") == 0) {
            snprintf(sc->cfg.input_label, sizeof(sc->cfg.input_label), "%s", p->lookahead.text);
            next_token(p);
            match(p, TSA_TOKEN_SEMICOLON);
        } else if (strcmp(block, "metrology") == 0) {
            parse_metrology(p, &sc->cfg);
        } else {
            if (p->lookahead.type == TSA_TOKEN_LBRACE) {
                int depth = 1;
                next_token(p);
                while (depth > 0 && p->lookahead.type != TSA_TOKEN_EOF) {
                    if (p->lookahead.type == TSA_TOKEN_LBRACE)
                        depth++;
                    else if (p->lookahead.type == TSA_TOKEN_RBRACE)
                        depth--;
                    next_token(p);
                }
            } else {
                next_token(p);
                match(p, TSA_TOKEN_SEMICOLON);
            }
        }
    }
    match(p, TSA_TOKEN_RBRACE);
}

int tsa_conf_load(tsa_full_conf_t* conf, const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Product-level protection: No configuration should be larger than 1MB */
    if (sz <= 0 || sz > 1024 * 1024) {
        tsa_error("CONF", "Configuration file size out of bounds (max 1MB)");
        fclose(fp);
        return -1;
    }

    char* buf = malloc(sz + 1);
    if (!buf) {
        tsa_error("CONF", "Out of memory allocating configuration buffer");
        fclose(fp);
        return -1;
    }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    parser_t p = {0};
    p.conf = conf;
    tsa_lexer_init(&p.lex, buf);
    next_token(&p);

    while (p.lookahead.type != TSA_TOKEN_EOF) {
        if (p.lookahead.type == TSA_TOKEN_WORD) {
            char word[TSA_ID_MAX];
            snprintf(word, sizeof(word), "%s", p.lookahead.text);
            next_token(&p);

            if (strcmp(word, "http_listen") == 0) {
                conf->http_listen_port = atoi(p.lookahead.text);
                next_token(&p);
                match(&p, TSA_TOKEN_SEMICOLON);
            } else if (strcmp(word, "srt_listen") == 0) {
                conf->srt_listen_port = atoi(p.lookahead.text);
                next_token(&p);
                match(&p, TSA_TOKEN_SEMICOLON);
            } else if (strcmp(word, "worker_threads") == 0) {
                conf->worker_threads = atoi(p.lookahead.text);
                next_token(&p);
                match(&p, TSA_TOKEN_SEMICOLON);
            } else if (strcmp(word, "worker_slice_us") == 0) {
                conf->worker_slice_us = atoi(p.lookahead.text);
                next_token(&p);
                match(&p, TSA_TOKEN_SEMICOLON);
            } else if (strcmp(word, "vhost") == 0) {
                char id[TSA_ID_MAX];
                snprintf(id, sizeof(id), "%s", p.lookahead.text);
                next_token(&p);
                parse_stream(&p, id);
            } else {
                if (p.lookahead.type == TSA_TOKEN_LBRACE) {
                    int depth = 1;
                    next_token(&p);
                    while (depth > 0 && p.lookahead.type != TSA_TOKEN_EOF) {
                        if (p.lookahead.type == TSA_TOKEN_LBRACE)
                            depth++;
                        else if (p.lookahead.type == TSA_TOKEN_RBRACE)
                            depth--;
                        next_token(&p);
                    }
                } else {
                    next_token(&p);
                    match(&p, TSA_TOKEN_SEMICOLON);
                }
            }
        } else {
            next_token(&p);
        }
    }

    free(buf);
    return 0;
}
