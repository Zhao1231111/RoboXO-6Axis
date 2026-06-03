#ifndef ROBOT_PARAMS_H
#define ROBOT_PARAMS_H

#include "general_6s.h"

// Gripper/tool length offset (mm)
extern int L;

// Per-axis CSP velocity limit (deg/s) and corresponding max inc per 1ms cycle
extern double csp_vel_limit_dps[6];
extern int    csp_max_inc_per_cycle[6];

// Initialize DH parameters, encoder parameters, and motor parameters
// on the global g_general_6s object.
void init_robot_params();

// Compute csp_max_inc_per_cycle from csp_vel_limit_dps and encoder params.
// Must be called after init_robot_params().
void compute_csp_limits();

#endif // ROBOT_PARAMS_H
