#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsa_conf.h"
#include "tsa_conf_lexer.h"
#include "tsa_units.h"
#include "tsa_log.h"

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
        char key[128];
        strncpy(key, p->lookahead.text, sizeof(key)-1);
        next_token(p);
        
        char val[128];
        strncpy(val, p->lookahead.text, sizeof(val)-1);
        next_token(p);
        match(p, TSA_TOKEN_SEMICOLON);

        if (strcmp(key, "pcr_jitter") == 0) cfg->analysis.pcr_jitter = tsa_units_to_bool(val);
        else if (strcmp(key, "drift_analysis") == 0) cfg->analysis.pcr_ema_alpha = 0.05; 
    }
    match(p, TSA_TOKEN_RBRACE);
}

static void parse_stream(parser_t* p, const char* id) {
    if (p->conf->stream_count >= MAX_STREAMS_IN_CONF) return;
    tsa_stream_conf_t* sc = &p->conf->streams[p->conf->stream_count++];
    strncpy(sc->id, id, sizeof(sc->id)-1);
    
    // Inherit from default vhost initially
    memcpy(&sc->cfg, &p->conf->vhost_default, sizeof(tsa_config_t));

    match(p, TSA_TOKEN_LBRACE);
    while (p->lookahead.type == TSA_TOKEN_WORD) {
        char block[128];
        strncpy(block, p->lookahead.text, sizeof(block)-1);
        next_token(p);

        if (strcmp(block, "input") == 0) {
            strncpy(sc->cfg.url, p->lookahead.text, sizeof(sc->cfg.url)-1);
            next_token(p);
            match(p, TSA_TOKEN_SEMICOLON);
        } else if (strcmp(block, "label") == 0) {
            strncpy(sc->cfg.input_label, p->lookahead.text, sizeof(sc->cfg.input_label)-1);
            next_token(p);
            match(p, TSA_TOKEN_SEMICOLON);
        } else if (strcmp(block, "metrology") == 0) {
            parse_metrology(p, &sc->cfg);
        } else {
            if (p->lookahead.type == TSA_TOKEN_LBRACE) {
                int depth = 1;
                next_token(p);
                while (depth > 0 && p->lookahead.type != TSA_TOKEN_EOF) {
                    if (p->lookahead.type == TSA_TOKEN_LBRACE) depth++;
                    else if (p->lookahead.type == TSA_TOKEN_RBRACE) depth--;
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
    char* buf = malloc(sz + 1);
    if (!buf) { fclose(fp); return -1; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    parser_t p = {0};
    p.conf = conf;
    tsa_lexer_init(&p.lex, buf);
    next_token(&p);

    while (p.lookahead.type != TSA_TOKEN_EOF) {
        if (p.lookahead.type == TSA_TOKEN_WORD) {
            char word[128];
            strncpy(word, p.lookahead.text, sizeof(word)-1);
            next_token(&p);

            if (strcmp(word, "http_listen") == 0) {
                conf->http_listen_port = atoi(p.lookahead.text);
                next_token(&p);
                match(&p, TSA_TOKEN_SEMICOLON);
            } else if (strcmp(word, "stream") == 0) {
                char id[64];
                strncpy(id, p.lookahead.text, sizeof(id)-1);
                next_token(&p);
                parse_stream(&p, id);
            } else {
                if (p.lookahead.type == TSA_TOKEN_LBRACE) {
                    int depth = 1;
                    next_token(&p);
                    while (depth > 0 && p.lookahead.type != TSA_TOKEN_EOF) {
                        if (p.lookahead.type == TSA_TOKEN_LBRACE) depth++;
                        else if (p.lookahead.type == TSA_TOKEN_RBRACE) depth--;
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
