#ifndef TSA_WEBHOOK_H
#define TSA_WEBHOOK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct tsa_webhook_engine tsa_webhook_engine_t;

/**
 * Initialize the asynchronous webhook engine.
 * @param url The target HTTP/HTTPS URL.
 */
tsa_webhook_engine_t* tsa_webhook_init(const char* url);

/**
 * Shutdown and free the engine.
 */
void tsa_webhook_destroy(tsa_webhook_engine_t* eng);

/**
 * Queue a JSON alert message to be sent asynchronously.
 * @param json_msg The raw JSON string (copied internally).
 */
void tsa_webhook_push(tsa_webhook_engine_t* eng, const char* json_msg);

/**
 * Helper to push a simple error event.
 */
void tsa_webhook_push_event(tsa_webhook_engine_t* eng, const char* stream_id, const char* event_type, const char* message);

#endif
