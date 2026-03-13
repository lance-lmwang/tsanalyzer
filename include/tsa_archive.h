#ifndef TSA_ARCHIVE_H
#define TSA_ARCHIVE_H
#include <stdint.h>
struct tsa_handle;
typedef struct archive_context archive_context_t;
archive_context_t* tsa_archive_create(struct tsa_handle* h);
void tsa_archive_destroy(archive_context_t* ctx);
void tsa_archive_check_trigger(struct tsa_handle* h);
#endif
