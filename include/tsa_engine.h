#ifndef TSA_ENGINE_H
#define TSA_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Common interface for all analysis engines in tsanalyzer.
 * Inspired by the modular design of ltntstools.
 */

typedef struct tsa_engine_ops {
    const char* name;
    
    /**
     * Allocate and initialize the engine.
     * @param h The parent TSA handle (for context).
     * @return Pointer to the engine instance.
     */
    void* (*create)(void* h);
    
    /**
     * Destroy the engine and release resources.
     */
    void (*destroy)(void* engine);
    
    /**
     * Process a single TS packet.
     * @param engine The engine instance.
     * @param pkt The 188-byte TS packet.
     * @param res Decode result from the core parser.
     * @param now_ns Current timestamp in nanoseconds.
     */
    void (*process_packet)(void* engine, const uint8_t* pkt, const void* res, uint64_t now_ns);
    
    /**
     * Reset the internal state of the engine.
     */
    void (*reset)(void* engine);
    
    /**
     * Commit/snapshot the current measurements.
     */
    void (*commit)(void* engine, uint64_t now_ns);
} tsa_engine_ops_t;

struct tsa_handle;
void tsa_register_engine(struct tsa_handle* h, struct tsa_engine_ops* ops);
void tsa_destroy_engines(struct tsa_handle* h);

#endif
void tsa_register_tr101290_engine(struct tsa_handle* h);
