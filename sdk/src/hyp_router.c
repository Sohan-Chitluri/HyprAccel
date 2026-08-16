/*
 * hyp_router.c  —  HyprAccel CORE-T7
 *
 * Routing Layer
 * Handles dispatching computations to either the software baseline path
 * or the hardware FPGA path, based on configuration from MBD-T4.
 */

#include "hyprccel.h"

/* Include the generated board configuration */
#include "../../boards/codegen/hyp_board_config.h"

#include <stdio.h>

/* Internal declarations for software baseline implementations */
extern void hyp_thejas_compute(hyp_op_t op, void *args);
extern void hyp_esp32_compute(hyp_op_t op, void *args);

#define MAX_OPS 8

/* State mapping operations to their configured targets */
static hyp_target_t op_routes[MAX_OPS];

int hyp_init(void) 
{
    /* Initialize default routing to software for safety */
    for (int i = 0; i < MAX_OPS; i++) {
        op_routes[i] = HYP_TARGET_SOFTWARE;
    }
    
    /* TODO: Initialize SPI/UART transport for FPGA communication (RTL-T4) */
    
    return 0;
}

void hyp_route(hyp_op_t op, hyp_target_t target) 
{
    if (op < MAX_OPS) {
        op_routes[op] = target;
    }
}

void hyp_compute(hyp_op_t op, void *args) 
{
    if (op >= MAX_OPS) return;

    hyp_target_t target = op_routes[op];

    if (target == HYP_TARGET_HARDWARE) {
        /* TODO: RTL-T4 hardware branch */
        /* Send input registers over SPI to FPGA, wait/poll, read results back */
        printf("[WARN] Hardware path (RTL-T4) not yet implemented. Stubbed.\n");
        return;
    }

    /* Software routing: Dispatches to the board-specific baseline implementation */
#if defined(HYP_BOARD_ARCH_RISCV32)
    hyp_thejas_compute(op, args);
#elif defined(HYP_BOARD_ARCH_XTENSA_LX6)
    hyp_esp32_compute(op, args);
#else
    #error "Unsupported or undefined board architecture in hyp_board_config.h"
#endif
}

void hyp_publish(const char *topic, const void *data, uint32_t size) 
{
    /* TODO: Implement telemetry/Gazebo bridge publish (SIM-T3 / PLAT-T1) */
    (void)topic;
    (void)data;
    (void)size;
}
