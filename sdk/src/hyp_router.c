/*
 * hyp_router.c  —  HyprAccel CORE-T7
 *
 * Routing Layer
 * Handles dispatching computations to either the software baseline path
 * or the hardware FPGA path, based on configuration from MBD-T4.
 */

#include "hyprccel.h"

/* The ESP32 MBD build copies this generated header beside the sources;
 * standalone SDK builds retain the repository-relative include. */
#if defined(ARDUINO)
#include "hyp_board_config.h"
#else
#include "../../boards/codegen/hyp_board_config.h"
#endif

#include <stdio.h>

/* The RTL simulator is a host-only optional backend.  Embedded targets must
 * never pull Verilator or C++ runtime dependencies into their build. */
#if defined(HYP_ENABLE_VERILATOR_BACKEND)
#include "hyp_cordic_rtl_backend.h"
#endif

/* Internal declarations for software baseline implementations */
extern void hyp_thejas_compute(hyp_op_t op, void *args);
extern void hyp_esp32_compute(hyp_op_t op, void *args);

/* Implemented by the ESP32 Arduino runtime wrapper in mbd/esp32. */
#if defined(ARDUINO) && defined(HYP_BOARD_ARCH_XTENSA_LX6)
extern void hyp_esp32_publish(const char *topic, const void *data, uint32_t size);
extern int hyp_esp32_sensor_read(const char *resource_id, void *out_value, uint32_t value_size);
extern int hyp_esp32_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size);
#endif

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
        /* Host-only RTL simulation.  This path executes cordic_top through
         * Verilator; it is deliberately not an FPGA/SPI implementation. */
#if defined(HYP_ENABLE_VERILATOR_BACKEND)
        if (hyp_cordic_rtl_compute(op, args) != HYP_CORDIC_RTL_SUCCESS) {
            fprintf(stderr, "[ERROR] CORDIC RTL simulation backend failed.\n");
        }
#else
        fprintf(stderr,
                "[ERROR] Hardware target requested, but no FPGA backend is "
                "available in this build (RTL simulation is host-only).\n");
#endif
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
#if defined(ARDUINO) && defined(HYP_BOARD_ARCH_XTENSA_LX6)
    hyp_esp32_publish(topic, data, size);
#else
    /* TODO: Implement telemetry/Gazebo bridge publish (SIM-T3 / PLAT-T1) */
    (void)topic;
    (void)data;
    (void)size;
#endif
}

int hyp_sensor_read(const char *resource_id, void *out_value, uint32_t value_size)
{
#if defined(ARDUINO) && defined(HYP_BOARD_ARCH_XTENSA_LX6)
    return hyp_esp32_sensor_read(resource_id, out_value, value_size);
#else
    (void)resource_id;
    (void)out_value;
    (void)value_size;
    return -1; // Not implemented for this target
#endif
}

int hyp_actuator_write(const char *resource_id, const void *in_value, uint32_t value_size)
{
#if defined(ARDUINO) && defined(HYP_BOARD_ARCH_XTENSA_LX6)
    return hyp_esp32_actuator_write(resource_id, in_value, value_size);
#else
    (void)resource_id;
    (void)in_value;
    (void)value_size;
    return -1; // Not implemented for this target
#endif
}
