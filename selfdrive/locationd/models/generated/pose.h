#pragma once
#include "rednose/helpers/ekf.h"
extern "C" {
void pose_update_4(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea);
void pose_update_10(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea);
void pose_update_13(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea);
void pose_update_14(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea);
void pose_err_fun(double *nom_x, double *delta_x, double *out_4693081389883303657);
void pose_inv_err_fun(double *nom_x, double *true_x, double *out_3043232131163915215);
void pose_H_mod_fun(double *state, double *out_4504589946406190503);
void pose_f_fun(double *state, double dt, double *out_3053812152163561705);
void pose_F_fun(double *state, double dt, double *out_8533568921845714664);
void pose_h_4(double *state, double *unused, double *out_2237381612538606718);
void pose_H_4(double *state, double *unused, double *out_1343491860345768218);
void pose_h_10(double *state, double *unused, double *out_2863719947197642864);
void pose_H_10(double *state, double *unused, double *out_5144280124555213416);
void pose_h_13(double *state, double *unused, double *out_883477765788198364);
void pose_H_13(double *state, double *unused, double *out_1868781964986564583);
void pose_h_14(double *state, double *unused, double *out_8137969777177408347);
void pose_H_14(double *state, double *unused, double *out_2619748995993716311);
void pose_predict(double *in_x, double *in_P, double *in_Q, double dt);
}