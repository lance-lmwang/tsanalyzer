#ifndef TSA_DESCRIPTORS_H
#define TSA_DESCRIPTORS_H

#include <stdint.h>

struct tsa_handle;

/**
 * @brief Descriptor handler callback
 *
 * @param h TSA handle
 * @param pid PID of the stream
 * @param tag Descriptor tag
 * @param data Descriptor data (excluding tag and length)
 * @param len Descriptor length
 * @param stream_type Pointer to the current stream type, can be modified by the handler
 */
typedef void (*tsa_descriptor_handler_t)(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data,
                                         uint8_t len, uint8_t *stream_type);

/**
 * @brief Register a descriptor handler
 *
 * @param tag Descriptor tag
 * @param handler Handler function
 */
void tsa_descriptors_register_handler(uint8_t tag, tsa_descriptor_handler_t handler);

/**
 * @brief Process a descriptor
 *
 * @param h TSA handle
 * @param pid PID of the stream
 * @param data Pointer to the descriptor (starts with tag and length)
 * @param stream_type Pointer to the current stream type, can be modified by the handlers
 */
void tsa_descriptors_process(struct tsa_handle *h, uint16_t pid, const uint8_t *data, uint8_t *stream_type);

/**
 * @brief Initialize the descriptor factory with default handlers
 */
void tsa_descriptors_init(void);

#endif  // TSA_DESCRIPTORS_H
