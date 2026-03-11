#ifndef TSA_AUTH_H
#define TSA_AUTH_H

#include <stdbool.h>
#include "mongoose.h"

/**
 * @brief Initialize the auth engine with a secret/public key.
 */
void tsa_auth_init(const char* secret);

/**
 * @brief Verify if the incoming HTTP request has a valid JWT.
 * @return true if authorized, false otherwise.
 */
bool tsa_auth_verify_request(struct mg_http_message* hm);

/**
 * @brief Global rate limiter check for a client ID (usually IP).
 * @return true if allowed, false if rate limited (429).
 */
bool tsa_auth_check_ratelimit(const char* client_id);

#endif
