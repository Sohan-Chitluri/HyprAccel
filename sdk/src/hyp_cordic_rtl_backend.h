/* Host-only Verilator backend for the canonical HyprAccel CORDIC RTL. */
#ifndef HYP_CORDIC_RTL_BACKEND_H
#define HYP_CORDIC_RTL_BACKEND_H

#include "hyprccel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HYP_CORDIC_RTL_SUCCESS = 0,
    HYP_CORDIC_RTL_UNSUPPORTED_OPERATION = 1,
    HYP_CORDIC_RTL_INVALID_ARGUMENT = 2,
    HYP_CORDIC_RTL_TIMEOUT = 3
} hyp_cordic_rtl_status_t;

/* Executes cordic_top through Verilator. This is RTL simulation, not FPGA I/O. */
hyp_cordic_rtl_status_t hyp_cordic_rtl_compute(hyp_op_t op, void *args);

#ifdef __cplusplus
}
#endif

#endif
