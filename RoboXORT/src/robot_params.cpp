#include "robot_params.h"
#include <cmath>
#include <cstdio>

extern General_6S* g_general_6s;

int L = 160;

double csp_vel_limit_dps[6] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
int    csp_max_inc_per_cycle[6] = {};

void init_robot_params() {
    DH_param dh;
    dh.a[0] = 0.0408; dh.a[1] = 450.342; dh.a[2] = 99.107;
    dh.a[3] = 0.0;    dh.a[4] = 0.0;     dh.a[5] = 0.0;

    dh.alpha[0] = M_PI * 90 / 180;   dh.alpha[1] = 0.0;
    dh.alpha[2] = M_PI * 90 / 180;   dh.alpha[3] = M_PI * 90 / 180;
    dh.alpha[4] = M_PI * (-90) / 180; dh.alpha[5] = 0.0;

    dh.d[0] = 390;     dh.d[1] = 0.4997; dh.d[2] = 0.0;
    dh.d[3] = 470.557; dh.d[4] = 0.0;    dh.d[5] = 123 + L;

    dh.theta[0] = 0.0;              dh.theta[1] = M_PI * 90 / 180;
    dh.theta[2] = 0.0;              dh.theta[3] = 0.0;
    dh.theta[4] = M_PI * 90 / 180;  dh.theta[5] = 0.0;

    Decare_Para decare;
    decare.maxacc = 5; decare.maxdec = -5; decare.maxjerk = 10000; decare.maxvel = 3000;

    Motor_Param motor_pa;
    motor_pa.encoder.reducRatio[0] = 80.007;  motor_pa.encoder.reducRatio[1] = 109.837;
    motor_pa.encoder.reducRatio[2] = 100.024; motor_pa.encoder.reducRatio[3] = 118.996;
    motor_pa.encoder.reducRatio[4] = 80.007;  motor_pa.encoder.reducRatio[5] = 79.977;

    motor_pa.encoder.singleTurnEncoder[0] = 237.172852;
    motor_pa.encoder.singleTurnEncoder[1] = 207.078552;
    motor_pa.encoder.singleTurnEncoder[2] = 131.119080;
    motor_pa.encoder.singleTurnEncoder[3] = 238.971863;
    motor_pa.encoder.singleTurnEncoder[4] = 31.110535;
    motor_pa.encoder.singleTurnEncoder[5] = 100.274963;

    motor_pa.encoder.direction[0] = -1; motor_pa.encoder.direction[1] =  1;
    motor_pa.encoder.direction[2] =  1; motor_pa.encoder.direction[3] = -1;
    motor_pa.encoder.direction[4] =  1; motor_pa.encoder.direction[5] = -1;

    motor_pa.RatedVel_rpm[0] = 450; motor_pa.RatedVel_rpm[1] = 350;
    motor_pa.RatedVel_rpm[2] = 450; motor_pa.RatedVel_rpm[3] = 350;
    motor_pa.RatedVel_rpm[4] = 450; motor_pa.RatedVel_rpm[5] = 450;

    for (int i = 0; i < 6; i++) {
        motor_pa.encoder.deviation[i] = 0;
        motor_pa.encoder.encoderResolution[i] = 23;
        motor_pa.maxAcc[i] = 5.0;
        motor_pa.maxDecel[i] = -5.0;
        motor_pa.maxRotSpeed[i] = 5000;
        motor_pa.RatedVel[i] = motor_pa.RatedVel_rpm[i] * 6.0 / motor_pa.encoder.reducRatio[i];
        motor_pa.DeRatedVel[i] = -motor_pa.RatedVel[i];
    }

    g_general_6s->set_param(motor_pa.encoder, motor_pa, dh, decare);
}

void compute_csp_limits() {
    Encoder_Param ep = g_general_6s->get_encoder_param();
    for (int i = 0; i < 6; i++) {
        double deg_per_cycle = csp_vel_limit_dps[i] / 1000.0;
        double inc_per_deg = ep.reducRatio[i] * (1 << ep.encoderResolution[i]) / 360.0;
        csp_max_inc_per_cycle[i] = static_cast<int>(deg_per_cycle * inc_per_deg + 0.5);
    }
}
