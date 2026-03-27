#include "hal.h"

#include "internal.h"

// Forward declarations
void hal_init_linux_backend(tsshaper_t* ctx);
void hal_init_mock_backend(tsshaper_t* ctx);
void hal_init_callback_backend(tsshaper_t* ctx, tss_write_cb cb, void* opaque);
uint64_t hal_get_mock_time_ns(void);
uint64_t hal_get_linux_time_ns(void);
void hal_precision_wait_mock(uint64_t target_ns);
void hal_precision_wait_linux(uint64_t target_ns);

static int active_backend_type = 0;  // Default to linux

int hal_init_ops(tsshaper_t* ctx, int backend_type) {
    active_backend_type = backend_type;
    if (backend_type == 0) {  // TSS_BACKEND_REAL_NETWORK
        hal_init_linux_backend(ctx);
        return 0;
    } else if (backend_type == 1) {  // TSS_BACKEND_VIRTUAL_PCAP
        hal_init_mock_backend(ctx);
        return 0;
    } else if (backend_type == 2) {  // TSS_BACKEND_CALLBACK
        // Note: Callback configuration will be handled in tsshaper_create
        // using the extra parameters in the config struct.
        return 0;
    }
    return -1;
}

uint64_t hal_get_time_ns(void) {
    if (active_backend_type == 1) {
        return hal_get_mock_time_ns();
    }
    return hal_get_linux_time_ns();
}

void hal_precision_wait(uint64_t target_ns) {
    if (active_backend_type == 1) {
        hal_precision_wait_mock(target_ns);
    } else {
        hal_precision_wait_linux(target_ns);
    }
}
