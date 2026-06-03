#ifndef ROBOT_PARAMS_H
#define ROBOT_PARAMS_H

#include "general_6s.h"

// Gripper/tool length offset (mm)
extern int L;

// Initialize DH parameters, encoder parameters, and motor parameters
// on the global g_general_6s object.
void init_robot_params();

#endif // ROBOT_PARAMS_H
