#include "ethercat_config.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

// ============================================================================
// EtherCAT Global State
// ============================================================================

ec_master_t*             ec_master = nullptr;
ec_master_state_t        ec_master_state = {};
ec_domain_t*             ec_domain = nullptr;
ec_domain_state_t        ec_domain_state = {};
ec_slave_config_t*       ec_sc[NUM_SLAVES] = {};
ec_slave_config_state_t  ec_sc_state[NUM_SLAVES] = {};
uint8_t*                 ec_domain_pd = nullptr;
EcPdoOffsets             ec_offsets = {};

int  ec_slave_op_flag[NUM_SLAVES] = {};
int  ec_power_state_machine = 0;

const struct timespec ec_cycletime = {0, EC_PERIOD_NS};

// ============================================================================
// Slave Addresses
// ============================================================================

static uint16_t slave_alias[NUM_SLAVES] = {0};
static uint16_t slave_position[NUM_SLAVES] = {0, 1, 2, 3, 4, 5, 6};

// ============================================================================
// PDO Entry Registration Table
// ============================================================================

// Macro helpers for slave address
#define S(idx) slave_alias[idx], slave_position[idx]

static ec_pdo_entry_reg_t build_domain_regs[NUM_SERVO_AXES * 12 + 2 + 1]; // filled in ec_init

// ============================================================================
// Servo PDO Configuration
// ============================================================================

static ec_pdo_entry_info_t servo_pdo_entries[] = {
    // RxPDO (master -> slave)
    {0x6040, 0x00, 16},  // control word
    {0x607a, 0x00, 32},  // target position
    {0x60b8, 0x00, 16},  // touch probe function
    {0x6060, 0x00, 8},   // operation mode
    // TxPDO (slave -> master)
    {0x6041, 0x00, 16},  // status word
    {0x6064, 0x00, 32},  // position actual value
    {0x60b9, 0x00, 16},  // touch probe status
    {0x60ba, 0x00, 32},  // touch probe pos1 value
    {0x60bc, 0x00, 32},  // BC
    {0x603f, 0x00, 16},  // error code
    {0x60fd, 0x00, 32},  // digital inputs
    {0x6077, 0x00, 16},  // torque actual value
};

static ec_pdo_info_t servo_pdos[] = {
    {0x1600, 4, servo_pdo_entries + 0},  // RxPDO
    {0x1A00, 8, servo_pdo_entries + 4},  // TxPDO
};

static ec_sync_info_t servo_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, servo_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT,  1, servo_pdos + 1, EC_WD_DISABLE},
    {0xFF}
};

// ============================================================================
// IO Module PDO Configuration
// ============================================================================

static ec_pdo_entry_info_t io_pdo_entries[] = {
    {0x7000, 0x00, 16},  // digital output
    {0x6000, 0x00, 16},  // digital input
};

static ec_pdo_info_t io_pdos[] = {
    {0x1600, 1, io_pdo_entries + 0},
    {0x1A00, 1, io_pdo_entries + 1},
};

static ec_sync_info_t io_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, io_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT,  1, io_pdos + 1, EC_WD_DISABLE},
    {0xFF}
};

// ============================================================================
// Build Domain Registration Table
// ============================================================================

static void build_pdo_reg_table() {
    int idx = 0;
    for (int ax = 0; ax < NUM_SERVO_AXES; ax++) {
        uint16_t al = slave_alias[ax];
        uint16_t pos = slave_position[ax];
        // clang-format off
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x6040, 0, &ec_offsets.ctrl_word[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x607A, 0, &ec_offsets.target_position[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x60B8, 0, &ec_offsets.touch_probe_function[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x6060, 0, &ec_offsets.operation_mode[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x6041, 0, &ec_offsets.status_word[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x6064, 0, &ec_offsets.position_actual_value[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x60B9, 0, &ec_offsets.touch_probe_status[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x60BA, 0, &ec_offsets.touch_probe_pos1_pos_value[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x60BC, 0, &ec_offsets.BC[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x603F, 0, &ec_offsets.F[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x60FD, 0, &ec_offsets.digital_inputs[ax]};
        build_domain_regs[idx++] = {al, pos, 0x00000922, 0x00000a01, 0x6077, 0, &ec_offsets.torque_actual_value[ax]};
        // clang-format on
    }
    // IO module
    build_domain_regs[idx++] = {slave_alias[6], slave_position[6], 0x00000c6d, 0x00000001, 0x7000, 0, &ec_offsets.io_out};
    build_domain_regs[idx++] = {slave_alias[6], slave_position[6], 0x00000c6d, 0x00000001, 0x6000, 0, &ec_offsets.io_in};
    build_domain_regs[idx] = {}; // terminator
}

// ============================================================================
// Public Functions
// ============================================================================

int ec_init() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return -1;
    }

    ec_master = ecrt_request_master(0);
    if (!ec_master) return -1;

    ec_domain = ecrt_master_create_domain(ec_master);
    if (!ec_domain) return -1;

    for (int i = 0; i < NUM_SLAVES; i++) {
        if (i < NUM_SERVO_AXES) {
            ec_sc[i] = ecrt_master_slave_config(ec_master, slave_alias[i], slave_position[i], VID_PID_SERVO);
            if (!ec_sc[i]) {
                fprintf(stderr, "获取伺服从站 %d 配置失败.\n", i);
                return -1;
            }
            if (ecrt_slave_config_pdos(ec_sc[i], EC_END, servo_syncs)) {
                fprintf(stderr, "配置伺服从站 %d PDO 失败.\n", i);
                return -1;
            }
        } else {
            ec_sc[i] = ecrt_master_slave_config(ec_master, slave_alias[i], slave_position[i], VID_PID_IO);
            if (!ec_sc[i]) {
                fprintf(stderr, "获取IO从站 %d 配置失败.\n", i);
                return -1;
            }
            if (ecrt_slave_config_pdos(ec_sc[i], EC_END, io_syncs)) {
                fprintf(stderr, "配置IO从站 %d PDO 失败.\n", i);
                return -1;
            }
        }
    }
    printf("成功配置从站 PDO.\n");

    build_pdo_reg_table();
    if (ecrt_domain_reg_pdo_entry_list(ec_domain, build_domain_regs)) {
        fprintf(stderr, "PDO 条目注册失败!\n");
        return -1;
    }

    for (int i = 0; i < NUM_SERVO_AXES; i++) {
        ecrt_slave_config_dc(ec_sc[i], 0x0300, EC_PERIOD_NS, EC_PERIOD_NS / 2, 0, 0);
    }

    printf("激活 EtherCAT 主站...\n");
    if (ecrt_master_activate(ec_master)) return -1;

    ec_domain_pd = ecrt_domain_data(ec_domain);
    if (!ec_domain_pd) return -1;

    return 0;
}

void ec_check_domain_state() {
    ec_domain_state_t ds;
    ecrt_domain_state(ec_domain, &ds);
    if (ds.working_counter != ec_domain_state.working_counter)
        printf("Domain1: WC %u.\n", ds.working_counter);
    if (ds.wc_state != ec_domain_state.wc_state)
        printf("Domain1: State %u.\n", ds.wc_state);
    ec_domain_state = ds;
}

void ec_check_master_state() {
    ec_master_state_t ms;
    ecrt_master_state(ec_master, &ms);
    if (ms.slaves_responding != ec_master_state.slaves_responding)
        printf("%u slave(s).\n", ms.slaves_responding);
    if (ms.al_states != ec_master_state.al_states)
        printf("AL states: 0x%02X.\n", ms.al_states);
    if (ms.link_up != ec_master_state.link_up)
        printf("Link is %s.\n", ms.link_up ? "up" : "down");
    ec_master_state = ms;
}

void ec_check_slave_state(int slave_idx) {
    ec_slave_config_state_t s;
    ecrt_slave_config_state(ec_sc[slave_idx], &s);
    if (s.operational == 1) ec_slave_op_flag[slave_idx] = 1;
    ec_sc_state[slave_idx] = s;
}
