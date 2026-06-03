#ifndef ETHERCAT_CONFIG_H
#define ETHERCAT_CONFIG_H

#include "ecrt.h"
#include <cstdint>

// ============================================================================
// EtherCAT Timing Constants
// ============================================================================

#define EC_FREQUENCY 1000
#define EC_CLOCK_TO_USE CLOCK_MONOTONIC
#define EC_NSEC_PER_SEC (1000000000L)
#define EC_PERIOD_NS (EC_NSEC_PER_SEC / EC_FREQUENCY)
#define EC_TIMESPEC2NS(T) ((uint64_t)(T).tv_sec * EC_NSEC_PER_SEC + (T).tv_nsec)

#define CYCLIC_POSITION 8  // CSP mode code

// ============================================================================
// Slave Configuration
// ============================================================================

#define NUM_SERVO_AXES 6
#define NUM_SLAVES 7   // 6 servos + 1 IO module

#define VID_PID_SERVO 0x00000922, 0x00000a01
#define VID_PID_IO    0x00000c6d, 0x00000001

// ============================================================================
// PDO Offset Structure
// ============================================================================

struct EcPdoOffsets {
    unsigned int ctrl_word[6];
    unsigned int operation_mode[6];
    unsigned int target_position[6];
    unsigned int touch_probe_function[6];
    unsigned int status_word[6];
    unsigned int position_actual_value[6];
    unsigned int touch_probe_status[6];
    unsigned int touch_probe_pos1_pos_value[6];
    unsigned int digital_inputs[6];
    unsigned int torque_actual_value[6];
    unsigned int BC[6];
    unsigned int F[6];
    unsigned int io_out;
    unsigned int io_in;
};

// ============================================================================
// EtherCAT Global State (defined in ethercat_config.cpp)
// ============================================================================

extern ec_master_t*             ec_master;
extern ec_master_state_t        ec_master_state;
extern ec_domain_t*             ec_domain;
extern ec_domain_state_t        ec_domain_state;
extern ec_slave_config_t*       ec_sc[NUM_SLAVES];
extern ec_slave_config_state_t  ec_sc_state[NUM_SLAVES];
extern uint8_t*                 ec_domain_pd;
extern EcPdoOffsets             ec_offsets;

extern int  ec_slave_op_flag[NUM_SLAVES];
extern int  ec_power_state_machine;

extern const struct timespec    ec_cycletime;

// ============================================================================
// Simulation Mode
// ============================================================================

extern bool g_sim_mode;

// ============================================================================
// EtherCAT Functions
// ============================================================================

int  ec_init();
int  ec_init_sim();
void ec_check_domain_state();
void ec_check_master_state();
void ec_check_slave_state(int slave_idx);

#endif // ETHERCAT_CONFIG_H
