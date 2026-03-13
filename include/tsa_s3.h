#ifndef TSA_S3_H
#define TSA_S3_H
#include <stdbool.h>
typedef struct {
    char endpoint[256];
    char bucket[64];
    bool use_ssl;
} tsa_s3_config_t;
int tsa_s3_upload(const tsa_s3_config_t *cfg, const char *local_path, const char *remote_key);
#endif
