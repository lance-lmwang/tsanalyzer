#ifndef TSA_JOB_MANAGER_H
#define TSA_JOB_MANAGER_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    TSA_JOB_STATUS_INSTANTIATED,
    TSA_JOB_STATUS_RUNNING,
    TSA_JOB_STATUS_PAUSED,
    TSA_JOB_STATUS_SUCCEEDED,
    TSA_JOB_STATUS_FAILED,
    TSA_JOB_STATUS_FAILING
} tsa_job_status_t;
typedef struct {
    char job_id[64];
    uint64_t start_time_ms;
    uint64_t end_time_ms;
    tsa_job_status_t status;
    float current_load_pps;
    pthread_mutex_t lock;
} tsa_job_instance_t;
tsa_job_instance_t *tsa_job_create(const char *job_id);
int tsa_job_set_status(tsa_job_instance_t *job, tsa_job_status_t new_status);
void tsa_job_destroy(tsa_job_instance_t *job);
const char *tsa_job_status_to_string(tsa_job_status_t status);
#endif
