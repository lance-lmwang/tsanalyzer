#include <string.h>
#include <ctype.h>
#include "tsa_conf_lexer.h"

void tsa_lexer_init(tsa_lexer_t* l, const char* input) {
    l->start = input;
    l->pos = input;
    l->line = 1;
}

static void skip_ignored(tsa_lexer_t* l) {
    while (*l->pos) {
        if (isspace(*l->pos)) {
            if (*l->pos == '\n') l->line++;
            l->pos++;
        } else if (*l->pos == '#') {
            while (*l->pos && *l->pos != '\n') l->pos++;
        } else {
            break;
        }
    }
}

tsa_token_t tsa_lexer_next(tsa_lexer_t* l) {
    tsa_token_t tok = {.type = TSA_TOKEN_EOF, .line = l->line};
    skip_ignored(l);

    if (!*l->pos) return tok;

    tok.line = l->line;
    char c = *l->pos;

    if (c == '{') {
        tok.type = TSA_TOKEN_LBRACE;
        tok.text[0] = c; tok.text[1] = '\0';
        l->pos++;
    } else if (c == '}') {
        tok.type = TSA_TOKEN_RBRACE;
        tok.text[0] = c; tok.text[1] = '\0';
        l->pos++;
    } else if (c == ';') {
        tok.type = TSA_TOKEN_SEMICOLON;
        tok.text[0] = c; tok.text[1] = '\0';
        l->pos++;
    } else {
        // Word or Number with units
        int len = 0;
        while (*l->pos && !isspace(*l->pos) && *l->pos != '{' && *l->pos != '}' && *l->pos != ';' && *l->pos != '#') {
            if (len < (int)sizeof(tok.text) - 1) {
                tok.text[len++] = *l->pos;
            }
            l->pos++;
        }
        tok.text[len] = '\0';
        tok.type = len > 0 ? TSA_TOKEN_WORD : TSA_TOKEN_ERROR;
    }

    return tok;
}
