#ifndef TSA_CONF_LEXER_H
#define TSA_CONF_LEXER_H

#include <stddef.h>

#ifndef TSA_ID_MAX
#define TSA_ID_MAX 256
#endif

typedef enum {
    TSA_TOKEN_EOF = 0,
    TSA_TOKEN_WORD,       // e.g. "metrology", "on", "15Mbps"
    TSA_TOKEN_LBRACE,     // '{'
    TSA_TOKEN_RBRACE,     // '}'
    TSA_TOKEN_SEMICOLON,  // ';'
    TSA_TOKEN_ERROR
} tsa_token_type_t;

typedef struct {
    tsa_token_type_t type;
    char text[TSA_ID_MAX];
    int line;
} tsa_token_t;

typedef struct {
    const char* pos;
    const char* start;
    int line;
} tsa_lexer_t;

void tsa_lexer_init(tsa_lexer_t* l, const char* input);
tsa_token_t tsa_lexer_next(tsa_lexer_t* l);

#endif
