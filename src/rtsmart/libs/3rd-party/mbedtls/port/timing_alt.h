#include <stdint.h>

/**
 * \brief          timer structure
 */
 struct mbedtls_timing_hr_time {
    uint64_t opaque[4];
};

/**
 * \brief          Context for mbedtls_timing_set/get_delay()
 */
typedef struct mbedtls_timing_delay_context {
    struct mbedtls_timing_hr_time   timer;
    uint32_t                        int_ms;
    uint32_t                        fin_ms;
} mbedtls_timing_delay_context;
