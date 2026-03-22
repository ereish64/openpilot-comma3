#include "pose.h"

namespace {
#define DIM 18
#define EDIM 18
#define MEDIM 18
typedef void (*Hfun)(double *, double *, double *);
const static double MAHA_THRESH_4 = 7.814727903251177;
const static double MAHA_THRESH_10 = 7.814727903251177;
const static double MAHA_THRESH_13 = 7.814727903251177;
const static double MAHA_THRESH_14 = 7.814727903251177;

/******************************************************************************
 *                      Code generated with SymPy 1.14.0                      *
 *                                                                            *
 *              See http://www.sympy.org/ for more information.               *
 *                                                                            *
 *                         This file is part of 'ekf'                         *
 ******************************************************************************/
void err_fun(double *nom_x, double *delta_x, double *out_4693081389883303657) {
   out_4693081389883303657[0] = delta_x[0] + nom_x[0];
   out_4693081389883303657[1] = delta_x[1] + nom_x[1];
   out_4693081389883303657[2] = delta_x[2] + nom_x[2];
   out_4693081389883303657[3] = delta_x[3] + nom_x[3];
   out_4693081389883303657[4] = delta_x[4] + nom_x[4];
   out_4693081389883303657[5] = delta_x[5] + nom_x[5];
   out_4693081389883303657[6] = delta_x[6] + nom_x[6];
   out_4693081389883303657[7] = delta_x[7] + nom_x[7];
   out_4693081389883303657[8] = delta_x[8] + nom_x[8];
   out_4693081389883303657[9] = delta_x[9] + nom_x[9];
   out_4693081389883303657[10] = delta_x[10] + nom_x[10];
   out_4693081389883303657[11] = delta_x[11] + nom_x[11];
   out_4693081389883303657[12] = delta_x[12] + nom_x[12];
   out_4693081389883303657[13] = delta_x[13] + nom_x[13];
   out_4693081389883303657[14] = delta_x[14] + nom_x[14];
   out_4693081389883303657[15] = delta_x[15] + nom_x[15];
   out_4693081389883303657[16] = delta_x[16] + nom_x[16];
   out_4693081389883303657[17] = delta_x[17] + nom_x[17];
}
void inv_err_fun(double *nom_x, double *true_x, double *out_3043232131163915215) {
   out_3043232131163915215[0] = -nom_x[0] + true_x[0];
   out_3043232131163915215[1] = -nom_x[1] + true_x[1];
   out_3043232131163915215[2] = -nom_x[2] + true_x[2];
   out_3043232131163915215[3] = -nom_x[3] + true_x[3];
   out_3043232131163915215[4] = -nom_x[4] + true_x[4];
   out_3043232131163915215[5] = -nom_x[5] + true_x[5];
   out_3043232131163915215[6] = -nom_x[6] + true_x[6];
   out_3043232131163915215[7] = -nom_x[7] + true_x[7];
   out_3043232131163915215[8] = -nom_x[8] + true_x[8];
   out_3043232131163915215[9] = -nom_x[9] + true_x[9];
   out_3043232131163915215[10] = -nom_x[10] + true_x[10];
   out_3043232131163915215[11] = -nom_x[11] + true_x[11];
   out_3043232131163915215[12] = -nom_x[12] + true_x[12];
   out_3043232131163915215[13] = -nom_x[13] + true_x[13];
   out_3043232131163915215[14] = -nom_x[14] + true_x[14];
   out_3043232131163915215[15] = -nom_x[15] + true_x[15];
   out_3043232131163915215[16] = -nom_x[16] + true_x[16];
   out_3043232131163915215[17] = -nom_x[17] + true_x[17];
}
void H_mod_fun(double *state, double *out_4504589946406190503) {
   out_4504589946406190503[0] = 1.0;
   out_4504589946406190503[1] = 0.0;
   out_4504589946406190503[2] = 0.0;
   out_4504589946406190503[3] = 0.0;
   out_4504589946406190503[4] = 0.0;
   out_4504589946406190503[5] = 0.0;
   out_4504589946406190503[6] = 0.0;
   out_4504589946406190503[7] = 0.0;
   out_4504589946406190503[8] = 0.0;
   out_4504589946406190503[9] = 0.0;
   out_4504589946406190503[10] = 0.0;
   out_4504589946406190503[11] = 0.0;
   out_4504589946406190503[12] = 0.0;
   out_4504589946406190503[13] = 0.0;
   out_4504589946406190503[14] = 0.0;
   out_4504589946406190503[15] = 0.0;
   out_4504589946406190503[16] = 0.0;
   out_4504589946406190503[17] = 0.0;
   out_4504589946406190503[18] = 0.0;
   out_4504589946406190503[19] = 1.0;
   out_4504589946406190503[20] = 0.0;
   out_4504589946406190503[21] = 0.0;
   out_4504589946406190503[22] = 0.0;
   out_4504589946406190503[23] = 0.0;
   out_4504589946406190503[24] = 0.0;
   out_4504589946406190503[25] = 0.0;
   out_4504589946406190503[26] = 0.0;
   out_4504589946406190503[27] = 0.0;
   out_4504589946406190503[28] = 0.0;
   out_4504589946406190503[29] = 0.0;
   out_4504589946406190503[30] = 0.0;
   out_4504589946406190503[31] = 0.0;
   out_4504589946406190503[32] = 0.0;
   out_4504589946406190503[33] = 0.0;
   out_4504589946406190503[34] = 0.0;
   out_4504589946406190503[35] = 0.0;
   out_4504589946406190503[36] = 0.0;
   out_4504589946406190503[37] = 0.0;
   out_4504589946406190503[38] = 1.0;
   out_4504589946406190503[39] = 0.0;
   out_4504589946406190503[40] = 0.0;
   out_4504589946406190503[41] = 0.0;
   out_4504589946406190503[42] = 0.0;
   out_4504589946406190503[43] = 0.0;
   out_4504589946406190503[44] = 0.0;
   out_4504589946406190503[45] = 0.0;
   out_4504589946406190503[46] = 0.0;
   out_4504589946406190503[47] = 0.0;
   out_4504589946406190503[48] = 0.0;
   out_4504589946406190503[49] = 0.0;
   out_4504589946406190503[50] = 0.0;
   out_4504589946406190503[51] = 0.0;
   out_4504589946406190503[52] = 0.0;
   out_4504589946406190503[53] = 0.0;
   out_4504589946406190503[54] = 0.0;
   out_4504589946406190503[55] = 0.0;
   out_4504589946406190503[56] = 0.0;
   out_4504589946406190503[57] = 1.0;
   out_4504589946406190503[58] = 0.0;
   out_4504589946406190503[59] = 0.0;
   out_4504589946406190503[60] = 0.0;
   out_4504589946406190503[61] = 0.0;
   out_4504589946406190503[62] = 0.0;
   out_4504589946406190503[63] = 0.0;
   out_4504589946406190503[64] = 0.0;
   out_4504589946406190503[65] = 0.0;
   out_4504589946406190503[66] = 0.0;
   out_4504589946406190503[67] = 0.0;
   out_4504589946406190503[68] = 0.0;
   out_4504589946406190503[69] = 0.0;
   out_4504589946406190503[70] = 0.0;
   out_4504589946406190503[71] = 0.0;
   out_4504589946406190503[72] = 0.0;
   out_4504589946406190503[73] = 0.0;
   out_4504589946406190503[74] = 0.0;
   out_4504589946406190503[75] = 0.0;
   out_4504589946406190503[76] = 1.0;
   out_4504589946406190503[77] = 0.0;
   out_4504589946406190503[78] = 0.0;
   out_4504589946406190503[79] = 0.0;
   out_4504589946406190503[80] = 0.0;
   out_4504589946406190503[81] = 0.0;
   out_4504589946406190503[82] = 0.0;
   out_4504589946406190503[83] = 0.0;
   out_4504589946406190503[84] = 0.0;
   out_4504589946406190503[85] = 0.0;
   out_4504589946406190503[86] = 0.0;
   out_4504589946406190503[87] = 0.0;
   out_4504589946406190503[88] = 0.0;
   out_4504589946406190503[89] = 0.0;
   out_4504589946406190503[90] = 0.0;
   out_4504589946406190503[91] = 0.0;
   out_4504589946406190503[92] = 0.0;
   out_4504589946406190503[93] = 0.0;
   out_4504589946406190503[94] = 0.0;
   out_4504589946406190503[95] = 1.0;
   out_4504589946406190503[96] = 0.0;
   out_4504589946406190503[97] = 0.0;
   out_4504589946406190503[98] = 0.0;
   out_4504589946406190503[99] = 0.0;
   out_4504589946406190503[100] = 0.0;
   out_4504589946406190503[101] = 0.0;
   out_4504589946406190503[102] = 0.0;
   out_4504589946406190503[103] = 0.0;
   out_4504589946406190503[104] = 0.0;
   out_4504589946406190503[105] = 0.0;
   out_4504589946406190503[106] = 0.0;
   out_4504589946406190503[107] = 0.0;
   out_4504589946406190503[108] = 0.0;
   out_4504589946406190503[109] = 0.0;
   out_4504589946406190503[110] = 0.0;
   out_4504589946406190503[111] = 0.0;
   out_4504589946406190503[112] = 0.0;
   out_4504589946406190503[113] = 0.0;
   out_4504589946406190503[114] = 1.0;
   out_4504589946406190503[115] = 0.0;
   out_4504589946406190503[116] = 0.0;
   out_4504589946406190503[117] = 0.0;
   out_4504589946406190503[118] = 0.0;
   out_4504589946406190503[119] = 0.0;
   out_4504589946406190503[120] = 0.0;
   out_4504589946406190503[121] = 0.0;
   out_4504589946406190503[122] = 0.0;
   out_4504589946406190503[123] = 0.0;
   out_4504589946406190503[124] = 0.0;
   out_4504589946406190503[125] = 0.0;
   out_4504589946406190503[126] = 0.0;
   out_4504589946406190503[127] = 0.0;
   out_4504589946406190503[128] = 0.0;
   out_4504589946406190503[129] = 0.0;
   out_4504589946406190503[130] = 0.0;
   out_4504589946406190503[131] = 0.0;
   out_4504589946406190503[132] = 0.0;
   out_4504589946406190503[133] = 1.0;
   out_4504589946406190503[134] = 0.0;
   out_4504589946406190503[135] = 0.0;
   out_4504589946406190503[136] = 0.0;
   out_4504589946406190503[137] = 0.0;
   out_4504589946406190503[138] = 0.0;
   out_4504589946406190503[139] = 0.0;
   out_4504589946406190503[140] = 0.0;
   out_4504589946406190503[141] = 0.0;
   out_4504589946406190503[142] = 0.0;
   out_4504589946406190503[143] = 0.0;
   out_4504589946406190503[144] = 0.0;
   out_4504589946406190503[145] = 0.0;
   out_4504589946406190503[146] = 0.0;
   out_4504589946406190503[147] = 0.0;
   out_4504589946406190503[148] = 0.0;
   out_4504589946406190503[149] = 0.0;
   out_4504589946406190503[150] = 0.0;
   out_4504589946406190503[151] = 0.0;
   out_4504589946406190503[152] = 1.0;
   out_4504589946406190503[153] = 0.0;
   out_4504589946406190503[154] = 0.0;
   out_4504589946406190503[155] = 0.0;
   out_4504589946406190503[156] = 0.0;
   out_4504589946406190503[157] = 0.0;
   out_4504589946406190503[158] = 0.0;
   out_4504589946406190503[159] = 0.0;
   out_4504589946406190503[160] = 0.0;
   out_4504589946406190503[161] = 0.0;
   out_4504589946406190503[162] = 0.0;
   out_4504589946406190503[163] = 0.0;
   out_4504589946406190503[164] = 0.0;
   out_4504589946406190503[165] = 0.0;
   out_4504589946406190503[166] = 0.0;
   out_4504589946406190503[167] = 0.0;
   out_4504589946406190503[168] = 0.0;
   out_4504589946406190503[169] = 0.0;
   out_4504589946406190503[170] = 0.0;
   out_4504589946406190503[171] = 1.0;
   out_4504589946406190503[172] = 0.0;
   out_4504589946406190503[173] = 0.0;
   out_4504589946406190503[174] = 0.0;
   out_4504589946406190503[175] = 0.0;
   out_4504589946406190503[176] = 0.0;
   out_4504589946406190503[177] = 0.0;
   out_4504589946406190503[178] = 0.0;
   out_4504589946406190503[179] = 0.0;
   out_4504589946406190503[180] = 0.0;
   out_4504589946406190503[181] = 0.0;
   out_4504589946406190503[182] = 0.0;
   out_4504589946406190503[183] = 0.0;
   out_4504589946406190503[184] = 0.0;
   out_4504589946406190503[185] = 0.0;
   out_4504589946406190503[186] = 0.0;
   out_4504589946406190503[187] = 0.0;
   out_4504589946406190503[188] = 0.0;
   out_4504589946406190503[189] = 0.0;
   out_4504589946406190503[190] = 1.0;
   out_4504589946406190503[191] = 0.0;
   out_4504589946406190503[192] = 0.0;
   out_4504589946406190503[193] = 0.0;
   out_4504589946406190503[194] = 0.0;
   out_4504589946406190503[195] = 0.0;
   out_4504589946406190503[196] = 0.0;
   out_4504589946406190503[197] = 0.0;
   out_4504589946406190503[198] = 0.0;
   out_4504589946406190503[199] = 0.0;
   out_4504589946406190503[200] = 0.0;
   out_4504589946406190503[201] = 0.0;
   out_4504589946406190503[202] = 0.0;
   out_4504589946406190503[203] = 0.0;
   out_4504589946406190503[204] = 0.0;
   out_4504589946406190503[205] = 0.0;
   out_4504589946406190503[206] = 0.0;
   out_4504589946406190503[207] = 0.0;
   out_4504589946406190503[208] = 0.0;
   out_4504589946406190503[209] = 1.0;
   out_4504589946406190503[210] = 0.0;
   out_4504589946406190503[211] = 0.0;
   out_4504589946406190503[212] = 0.0;
   out_4504589946406190503[213] = 0.0;
   out_4504589946406190503[214] = 0.0;
   out_4504589946406190503[215] = 0.0;
   out_4504589946406190503[216] = 0.0;
   out_4504589946406190503[217] = 0.0;
   out_4504589946406190503[218] = 0.0;
   out_4504589946406190503[219] = 0.0;
   out_4504589946406190503[220] = 0.0;
   out_4504589946406190503[221] = 0.0;
   out_4504589946406190503[222] = 0.0;
   out_4504589946406190503[223] = 0.0;
   out_4504589946406190503[224] = 0.0;
   out_4504589946406190503[225] = 0.0;
   out_4504589946406190503[226] = 0.0;
   out_4504589946406190503[227] = 0.0;
   out_4504589946406190503[228] = 1.0;
   out_4504589946406190503[229] = 0.0;
   out_4504589946406190503[230] = 0.0;
   out_4504589946406190503[231] = 0.0;
   out_4504589946406190503[232] = 0.0;
   out_4504589946406190503[233] = 0.0;
   out_4504589946406190503[234] = 0.0;
   out_4504589946406190503[235] = 0.0;
   out_4504589946406190503[236] = 0.0;
   out_4504589946406190503[237] = 0.0;
   out_4504589946406190503[238] = 0.0;
   out_4504589946406190503[239] = 0.0;
   out_4504589946406190503[240] = 0.0;
   out_4504589946406190503[241] = 0.0;
   out_4504589946406190503[242] = 0.0;
   out_4504589946406190503[243] = 0.0;
   out_4504589946406190503[244] = 0.0;
   out_4504589946406190503[245] = 0.0;
   out_4504589946406190503[246] = 0.0;
   out_4504589946406190503[247] = 1.0;
   out_4504589946406190503[248] = 0.0;
   out_4504589946406190503[249] = 0.0;
   out_4504589946406190503[250] = 0.0;
   out_4504589946406190503[251] = 0.0;
   out_4504589946406190503[252] = 0.0;
   out_4504589946406190503[253] = 0.0;
   out_4504589946406190503[254] = 0.0;
   out_4504589946406190503[255] = 0.0;
   out_4504589946406190503[256] = 0.0;
   out_4504589946406190503[257] = 0.0;
   out_4504589946406190503[258] = 0.0;
   out_4504589946406190503[259] = 0.0;
   out_4504589946406190503[260] = 0.0;
   out_4504589946406190503[261] = 0.0;
   out_4504589946406190503[262] = 0.0;
   out_4504589946406190503[263] = 0.0;
   out_4504589946406190503[264] = 0.0;
   out_4504589946406190503[265] = 0.0;
   out_4504589946406190503[266] = 1.0;
   out_4504589946406190503[267] = 0.0;
   out_4504589946406190503[268] = 0.0;
   out_4504589946406190503[269] = 0.0;
   out_4504589946406190503[270] = 0.0;
   out_4504589946406190503[271] = 0.0;
   out_4504589946406190503[272] = 0.0;
   out_4504589946406190503[273] = 0.0;
   out_4504589946406190503[274] = 0.0;
   out_4504589946406190503[275] = 0.0;
   out_4504589946406190503[276] = 0.0;
   out_4504589946406190503[277] = 0.0;
   out_4504589946406190503[278] = 0.0;
   out_4504589946406190503[279] = 0.0;
   out_4504589946406190503[280] = 0.0;
   out_4504589946406190503[281] = 0.0;
   out_4504589946406190503[282] = 0.0;
   out_4504589946406190503[283] = 0.0;
   out_4504589946406190503[284] = 0.0;
   out_4504589946406190503[285] = 1.0;
   out_4504589946406190503[286] = 0.0;
   out_4504589946406190503[287] = 0.0;
   out_4504589946406190503[288] = 0.0;
   out_4504589946406190503[289] = 0.0;
   out_4504589946406190503[290] = 0.0;
   out_4504589946406190503[291] = 0.0;
   out_4504589946406190503[292] = 0.0;
   out_4504589946406190503[293] = 0.0;
   out_4504589946406190503[294] = 0.0;
   out_4504589946406190503[295] = 0.0;
   out_4504589946406190503[296] = 0.0;
   out_4504589946406190503[297] = 0.0;
   out_4504589946406190503[298] = 0.0;
   out_4504589946406190503[299] = 0.0;
   out_4504589946406190503[300] = 0.0;
   out_4504589946406190503[301] = 0.0;
   out_4504589946406190503[302] = 0.0;
   out_4504589946406190503[303] = 0.0;
   out_4504589946406190503[304] = 1.0;
   out_4504589946406190503[305] = 0.0;
   out_4504589946406190503[306] = 0.0;
   out_4504589946406190503[307] = 0.0;
   out_4504589946406190503[308] = 0.0;
   out_4504589946406190503[309] = 0.0;
   out_4504589946406190503[310] = 0.0;
   out_4504589946406190503[311] = 0.0;
   out_4504589946406190503[312] = 0.0;
   out_4504589946406190503[313] = 0.0;
   out_4504589946406190503[314] = 0.0;
   out_4504589946406190503[315] = 0.0;
   out_4504589946406190503[316] = 0.0;
   out_4504589946406190503[317] = 0.0;
   out_4504589946406190503[318] = 0.0;
   out_4504589946406190503[319] = 0.0;
   out_4504589946406190503[320] = 0.0;
   out_4504589946406190503[321] = 0.0;
   out_4504589946406190503[322] = 0.0;
   out_4504589946406190503[323] = 1.0;
}
void f_fun(double *state, double dt, double *out_3053812152163561705) {
   out_3053812152163561705[0] = atan2((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), -(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]));
   out_3053812152163561705[1] = asin(sin(dt*state[7])*cos(state[0])*cos(state[1]) - sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) + sin(state[1])*cos(dt*state[7])*cos(dt*state[8]));
   out_3053812152163561705[2] = atan2(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), -(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]));
   out_3053812152163561705[3] = dt*state[12] + state[3];
   out_3053812152163561705[4] = dt*state[13] + state[4];
   out_3053812152163561705[5] = dt*state[14] + state[5];
   out_3053812152163561705[6] = state[6];
   out_3053812152163561705[7] = state[7];
   out_3053812152163561705[8] = state[8];
   out_3053812152163561705[9] = state[9];
   out_3053812152163561705[10] = state[10];
   out_3053812152163561705[11] = state[11];
   out_3053812152163561705[12] = state[12];
   out_3053812152163561705[13] = state[13];
   out_3053812152163561705[14] = state[14];
   out_3053812152163561705[15] = state[15];
   out_3053812152163561705[16] = state[16];
   out_3053812152163561705[17] = state[17];
}
void F_fun(double *state, double dt, double *out_8533568921845714664) {
   out_8533568921845714664[0] = ((-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*cos(state[0])*cos(state[1]) - sin(state[0])*cos(dt*state[6])*cos(dt*state[7])*cos(state[1]))*(-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) - sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2)) + ((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*cos(state[0])*cos(state[1]) - sin(dt*state[6])*sin(state[0])*cos(dt*state[7])*cos(state[1]))*(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2));
   out_8533568921845714664[1] = ((-sin(dt*state[6])*sin(dt*state[8]) - sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*cos(state[1]) - (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*sin(state[1]) - sin(state[1])*cos(dt*state[6])*cos(dt*state[7])*cos(state[0]))*(-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) - sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2)) + (-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))*(-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*sin(state[1]) + (-sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) + sin(dt*state[8])*cos(dt*state[6]))*cos(state[1]) - sin(dt*state[6])*sin(state[1])*cos(dt*state[7])*cos(state[0]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2));
   out_8533568921845714664[2] = 0;
   out_8533568921845714664[3] = 0;
   out_8533568921845714664[4] = 0;
   out_8533568921845714664[5] = 0;
   out_8533568921845714664[6] = (-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))*(dt*cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]) + (-dt*sin(dt*state[6])*sin(dt*state[8]) - dt*sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-dt*sin(dt*state[6])*cos(dt*state[8]) + dt*sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2)) + (-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) - sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))*(-dt*sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]) + (-dt*sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) - dt*cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (dt*sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - dt*sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2));
   out_8533568921845714664[7] = (-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))*(-dt*sin(dt*state[6])*sin(dt*state[7])*cos(state[0])*cos(state[1]) + dt*sin(dt*state[6])*sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) - dt*sin(dt*state[6])*sin(state[1])*cos(dt*state[7])*cos(dt*state[8]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2)) + (-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) - sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))*(-dt*sin(dt*state[7])*cos(dt*state[6])*cos(state[0])*cos(state[1]) + dt*sin(dt*state[8])*sin(state[0])*cos(dt*state[6])*cos(dt*state[7])*cos(state[1]) - dt*sin(state[1])*cos(dt*state[6])*cos(dt*state[7])*cos(dt*state[8]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2));
   out_8533568921845714664[8] = ((dt*sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + dt*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (dt*sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - dt*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]))*(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2)) + ((dt*sin(dt*state[6])*sin(dt*state[8]) + dt*sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (-dt*sin(dt*state[6])*cos(dt*state[8]) + dt*sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]))*(-(sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) + (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) - sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/(pow(-(sin(dt*state[6])*sin(dt*state[8]) + sin(dt*state[7])*cos(dt*state[6])*cos(dt*state[8]))*sin(state[1]) + (-sin(dt*state[6])*cos(dt*state[8]) + sin(dt*state[7])*sin(dt*state[8])*cos(dt*state[6]))*sin(state[0])*cos(state[1]) + cos(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2) + pow((sin(dt*state[6])*sin(dt*state[7])*sin(dt*state[8]) + cos(dt*state[6])*cos(dt*state[8]))*sin(state[0])*cos(state[1]) - (sin(dt*state[6])*sin(dt*state[7])*cos(dt*state[8]) - sin(dt*state[8])*cos(dt*state[6]))*sin(state[1]) + sin(dt*state[6])*cos(dt*state[7])*cos(state[0])*cos(state[1]), 2));
   out_8533568921845714664[9] = 0;
   out_8533568921845714664[10] = 0;
   out_8533568921845714664[11] = 0;
   out_8533568921845714664[12] = 0;
   out_8533568921845714664[13] = 0;
   out_8533568921845714664[14] = 0;
   out_8533568921845714664[15] = 0;
   out_8533568921845714664[16] = 0;
   out_8533568921845714664[17] = 0;
   out_8533568921845714664[18] = (-sin(dt*state[7])*sin(state[0])*cos(state[1]) - sin(dt*state[8])*cos(dt*state[7])*cos(state[0])*cos(state[1]))/sqrt(1 - pow(sin(dt*state[7])*cos(state[0])*cos(state[1]) - sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) + sin(state[1])*cos(dt*state[7])*cos(dt*state[8]), 2));
   out_8533568921845714664[19] = (-sin(dt*state[7])*sin(state[1])*cos(state[0]) + sin(dt*state[8])*sin(state[0])*sin(state[1])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))/sqrt(1 - pow(sin(dt*state[7])*cos(state[0])*cos(state[1]) - sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) + sin(state[1])*cos(dt*state[7])*cos(dt*state[8]), 2));
   out_8533568921845714664[20] = 0;
   out_8533568921845714664[21] = 0;
   out_8533568921845714664[22] = 0;
   out_8533568921845714664[23] = 0;
   out_8533568921845714664[24] = 0;
   out_8533568921845714664[25] = (dt*sin(dt*state[7])*sin(dt*state[8])*sin(state[0])*cos(state[1]) - dt*sin(dt*state[7])*sin(state[1])*cos(dt*state[8]) + dt*cos(dt*state[7])*cos(state[0])*cos(state[1]))/sqrt(1 - pow(sin(dt*state[7])*cos(state[0])*cos(state[1]) - sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) + sin(state[1])*cos(dt*state[7])*cos(dt*state[8]), 2));
   out_8533568921845714664[26] = (-dt*sin(dt*state[8])*sin(state[1])*cos(dt*state[7]) - dt*sin(state[0])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))/sqrt(1 - pow(sin(dt*state[7])*cos(state[0])*cos(state[1]) - sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1]) + sin(state[1])*cos(dt*state[7])*cos(dt*state[8]), 2));
   out_8533568921845714664[27] = 0;
   out_8533568921845714664[28] = 0;
   out_8533568921845714664[29] = 0;
   out_8533568921845714664[30] = 0;
   out_8533568921845714664[31] = 0;
   out_8533568921845714664[32] = 0;
   out_8533568921845714664[33] = 0;
   out_8533568921845714664[34] = 0;
   out_8533568921845714664[35] = 0;
   out_8533568921845714664[36] = ((sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[7]))*((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) - (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2)) + ((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[7]))*(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2));
   out_8533568921845714664[37] = (-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))*(-sin(dt*state[7])*sin(state[2])*cos(state[0])*cos(state[1]) + sin(dt*state[8])*sin(state[0])*sin(state[2])*cos(dt*state[7])*cos(state[1]) - sin(state[1])*sin(state[2])*cos(dt*state[7])*cos(dt*state[8]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2)) + ((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) - (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))*(-sin(dt*state[7])*cos(state[0])*cos(state[1])*cos(state[2]) + sin(dt*state[8])*sin(state[0])*cos(dt*state[7])*cos(state[1])*cos(state[2]) - sin(state[1])*cos(dt*state[7])*cos(dt*state[8])*cos(state[2]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2));
   out_8533568921845714664[38] = ((-sin(state[0])*sin(state[2]) - sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))*(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2)) + ((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (-sin(state[0])*sin(state[1])*sin(state[2]) - cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))*((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) - (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2));
   out_8533568921845714664[39] = 0;
   out_8533568921845714664[40] = 0;
   out_8533568921845714664[41] = 0;
   out_8533568921845714664[42] = 0;
   out_8533568921845714664[43] = (-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))*(dt*(sin(state[0])*cos(state[2]) - sin(state[1])*sin(state[2])*cos(state[0]))*cos(dt*state[7]) - dt*(sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[7])*sin(dt*state[8]) - dt*sin(dt*state[7])*sin(state[2])*cos(dt*state[8])*cos(state[1]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2)) + ((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) - (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))*(dt*(-sin(state[0])*sin(state[2]) - sin(state[1])*cos(state[0])*cos(state[2]))*cos(dt*state[7]) - dt*(sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[7])*sin(dt*state[8]) - dt*sin(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2));
   out_8533568921845714664[44] = (dt*(sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*cos(dt*state[7])*cos(dt*state[8]) - dt*sin(dt*state[8])*sin(state[2])*cos(dt*state[7])*cos(state[1]))*(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2)) + (dt*(sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*cos(dt*state[7])*cos(dt*state[8]) - dt*sin(dt*state[8])*cos(dt*state[7])*cos(state[1])*cos(state[2]))*((-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) - (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) - sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]))/(pow(-(sin(state[0])*sin(state[2]) + sin(state[1])*cos(state[0])*cos(state[2]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*cos(state[2]) - sin(state[2])*cos(state[0]))*sin(dt*state[8])*cos(dt*state[7]) + cos(dt*state[7])*cos(dt*state[8])*cos(state[1])*cos(state[2]), 2) + pow(-(-sin(state[0])*cos(state[2]) + sin(state[1])*sin(state[2])*cos(state[0]))*sin(dt*state[7]) + (sin(state[0])*sin(state[1])*sin(state[2]) + cos(state[0])*cos(state[2]))*sin(dt*state[8])*cos(dt*state[7]) + sin(state[2])*cos(dt*state[7])*cos(dt*state[8])*cos(state[1]), 2));
   out_8533568921845714664[45] = 0;
   out_8533568921845714664[46] = 0;
   out_8533568921845714664[47] = 0;
   out_8533568921845714664[48] = 0;
   out_8533568921845714664[49] = 0;
   out_8533568921845714664[50] = 0;
   out_8533568921845714664[51] = 0;
   out_8533568921845714664[52] = 0;
   out_8533568921845714664[53] = 0;
   out_8533568921845714664[54] = 0;
   out_8533568921845714664[55] = 0;
   out_8533568921845714664[56] = 0;
   out_8533568921845714664[57] = 1;
   out_8533568921845714664[58] = 0;
   out_8533568921845714664[59] = 0;
   out_8533568921845714664[60] = 0;
   out_8533568921845714664[61] = 0;
   out_8533568921845714664[62] = 0;
   out_8533568921845714664[63] = 0;
   out_8533568921845714664[64] = 0;
   out_8533568921845714664[65] = 0;
   out_8533568921845714664[66] = dt;
   out_8533568921845714664[67] = 0;
   out_8533568921845714664[68] = 0;
   out_8533568921845714664[69] = 0;
   out_8533568921845714664[70] = 0;
   out_8533568921845714664[71] = 0;
   out_8533568921845714664[72] = 0;
   out_8533568921845714664[73] = 0;
   out_8533568921845714664[74] = 0;
   out_8533568921845714664[75] = 0;
   out_8533568921845714664[76] = 1;
   out_8533568921845714664[77] = 0;
   out_8533568921845714664[78] = 0;
   out_8533568921845714664[79] = 0;
   out_8533568921845714664[80] = 0;
   out_8533568921845714664[81] = 0;
   out_8533568921845714664[82] = 0;
   out_8533568921845714664[83] = 0;
   out_8533568921845714664[84] = 0;
   out_8533568921845714664[85] = dt;
   out_8533568921845714664[86] = 0;
   out_8533568921845714664[87] = 0;
   out_8533568921845714664[88] = 0;
   out_8533568921845714664[89] = 0;
   out_8533568921845714664[90] = 0;
   out_8533568921845714664[91] = 0;
   out_8533568921845714664[92] = 0;
   out_8533568921845714664[93] = 0;
   out_8533568921845714664[94] = 0;
   out_8533568921845714664[95] = 1;
   out_8533568921845714664[96] = 0;
   out_8533568921845714664[97] = 0;
   out_8533568921845714664[98] = 0;
   out_8533568921845714664[99] = 0;
   out_8533568921845714664[100] = 0;
   out_8533568921845714664[101] = 0;
   out_8533568921845714664[102] = 0;
   out_8533568921845714664[103] = 0;
   out_8533568921845714664[104] = dt;
   out_8533568921845714664[105] = 0;
   out_8533568921845714664[106] = 0;
   out_8533568921845714664[107] = 0;
   out_8533568921845714664[108] = 0;
   out_8533568921845714664[109] = 0;
   out_8533568921845714664[110] = 0;
   out_8533568921845714664[111] = 0;
   out_8533568921845714664[112] = 0;
   out_8533568921845714664[113] = 0;
   out_8533568921845714664[114] = 1;
   out_8533568921845714664[115] = 0;
   out_8533568921845714664[116] = 0;
   out_8533568921845714664[117] = 0;
   out_8533568921845714664[118] = 0;
   out_8533568921845714664[119] = 0;
   out_8533568921845714664[120] = 0;
   out_8533568921845714664[121] = 0;
   out_8533568921845714664[122] = 0;
   out_8533568921845714664[123] = 0;
   out_8533568921845714664[124] = 0;
   out_8533568921845714664[125] = 0;
   out_8533568921845714664[126] = 0;
   out_8533568921845714664[127] = 0;
   out_8533568921845714664[128] = 0;
   out_8533568921845714664[129] = 0;
   out_8533568921845714664[130] = 0;
   out_8533568921845714664[131] = 0;
   out_8533568921845714664[132] = 0;
   out_8533568921845714664[133] = 1;
   out_8533568921845714664[134] = 0;
   out_8533568921845714664[135] = 0;
   out_8533568921845714664[136] = 0;
   out_8533568921845714664[137] = 0;
   out_8533568921845714664[138] = 0;
   out_8533568921845714664[139] = 0;
   out_8533568921845714664[140] = 0;
   out_8533568921845714664[141] = 0;
   out_8533568921845714664[142] = 0;
   out_8533568921845714664[143] = 0;
   out_8533568921845714664[144] = 0;
   out_8533568921845714664[145] = 0;
   out_8533568921845714664[146] = 0;
   out_8533568921845714664[147] = 0;
   out_8533568921845714664[148] = 0;
   out_8533568921845714664[149] = 0;
   out_8533568921845714664[150] = 0;
   out_8533568921845714664[151] = 0;
   out_8533568921845714664[152] = 1;
   out_8533568921845714664[153] = 0;
   out_8533568921845714664[154] = 0;
   out_8533568921845714664[155] = 0;
   out_8533568921845714664[156] = 0;
   out_8533568921845714664[157] = 0;
   out_8533568921845714664[158] = 0;
   out_8533568921845714664[159] = 0;
   out_8533568921845714664[160] = 0;
   out_8533568921845714664[161] = 0;
   out_8533568921845714664[162] = 0;
   out_8533568921845714664[163] = 0;
   out_8533568921845714664[164] = 0;
   out_8533568921845714664[165] = 0;
   out_8533568921845714664[166] = 0;
   out_8533568921845714664[167] = 0;
   out_8533568921845714664[168] = 0;
   out_8533568921845714664[169] = 0;
   out_8533568921845714664[170] = 0;
   out_8533568921845714664[171] = 1;
   out_8533568921845714664[172] = 0;
   out_8533568921845714664[173] = 0;
   out_8533568921845714664[174] = 0;
   out_8533568921845714664[175] = 0;
   out_8533568921845714664[176] = 0;
   out_8533568921845714664[177] = 0;
   out_8533568921845714664[178] = 0;
   out_8533568921845714664[179] = 0;
   out_8533568921845714664[180] = 0;
   out_8533568921845714664[181] = 0;
   out_8533568921845714664[182] = 0;
   out_8533568921845714664[183] = 0;
   out_8533568921845714664[184] = 0;
   out_8533568921845714664[185] = 0;
   out_8533568921845714664[186] = 0;
   out_8533568921845714664[187] = 0;
   out_8533568921845714664[188] = 0;
   out_8533568921845714664[189] = 0;
   out_8533568921845714664[190] = 1;
   out_8533568921845714664[191] = 0;
   out_8533568921845714664[192] = 0;
   out_8533568921845714664[193] = 0;
   out_8533568921845714664[194] = 0;
   out_8533568921845714664[195] = 0;
   out_8533568921845714664[196] = 0;
   out_8533568921845714664[197] = 0;
   out_8533568921845714664[198] = 0;
   out_8533568921845714664[199] = 0;
   out_8533568921845714664[200] = 0;
   out_8533568921845714664[201] = 0;
   out_8533568921845714664[202] = 0;
   out_8533568921845714664[203] = 0;
   out_8533568921845714664[204] = 0;
   out_8533568921845714664[205] = 0;
   out_8533568921845714664[206] = 0;
   out_8533568921845714664[207] = 0;
   out_8533568921845714664[208] = 0;
   out_8533568921845714664[209] = 1;
   out_8533568921845714664[210] = 0;
   out_8533568921845714664[211] = 0;
   out_8533568921845714664[212] = 0;
   out_8533568921845714664[213] = 0;
   out_8533568921845714664[214] = 0;
   out_8533568921845714664[215] = 0;
   out_8533568921845714664[216] = 0;
   out_8533568921845714664[217] = 0;
   out_8533568921845714664[218] = 0;
   out_8533568921845714664[219] = 0;
   out_8533568921845714664[220] = 0;
   out_8533568921845714664[221] = 0;
   out_8533568921845714664[222] = 0;
   out_8533568921845714664[223] = 0;
   out_8533568921845714664[224] = 0;
   out_8533568921845714664[225] = 0;
   out_8533568921845714664[226] = 0;
   out_8533568921845714664[227] = 0;
   out_8533568921845714664[228] = 1;
   out_8533568921845714664[229] = 0;
   out_8533568921845714664[230] = 0;
   out_8533568921845714664[231] = 0;
   out_8533568921845714664[232] = 0;
   out_8533568921845714664[233] = 0;
   out_8533568921845714664[234] = 0;
   out_8533568921845714664[235] = 0;
   out_8533568921845714664[236] = 0;
   out_8533568921845714664[237] = 0;
   out_8533568921845714664[238] = 0;
   out_8533568921845714664[239] = 0;
   out_8533568921845714664[240] = 0;
   out_8533568921845714664[241] = 0;
   out_8533568921845714664[242] = 0;
   out_8533568921845714664[243] = 0;
   out_8533568921845714664[244] = 0;
   out_8533568921845714664[245] = 0;
   out_8533568921845714664[246] = 0;
   out_8533568921845714664[247] = 1;
   out_8533568921845714664[248] = 0;
   out_8533568921845714664[249] = 0;
   out_8533568921845714664[250] = 0;
   out_8533568921845714664[251] = 0;
   out_8533568921845714664[252] = 0;
   out_8533568921845714664[253] = 0;
   out_8533568921845714664[254] = 0;
   out_8533568921845714664[255] = 0;
   out_8533568921845714664[256] = 0;
   out_8533568921845714664[257] = 0;
   out_8533568921845714664[258] = 0;
   out_8533568921845714664[259] = 0;
   out_8533568921845714664[260] = 0;
   out_8533568921845714664[261] = 0;
   out_8533568921845714664[262] = 0;
   out_8533568921845714664[263] = 0;
   out_8533568921845714664[264] = 0;
   out_8533568921845714664[265] = 0;
   out_8533568921845714664[266] = 1;
   out_8533568921845714664[267] = 0;
   out_8533568921845714664[268] = 0;
   out_8533568921845714664[269] = 0;
   out_8533568921845714664[270] = 0;
   out_8533568921845714664[271] = 0;
   out_8533568921845714664[272] = 0;
   out_8533568921845714664[273] = 0;
   out_8533568921845714664[274] = 0;
   out_8533568921845714664[275] = 0;
   out_8533568921845714664[276] = 0;
   out_8533568921845714664[277] = 0;
   out_8533568921845714664[278] = 0;
   out_8533568921845714664[279] = 0;
   out_8533568921845714664[280] = 0;
   out_8533568921845714664[281] = 0;
   out_8533568921845714664[282] = 0;
   out_8533568921845714664[283] = 0;
   out_8533568921845714664[284] = 0;
   out_8533568921845714664[285] = 1;
   out_8533568921845714664[286] = 0;
   out_8533568921845714664[287] = 0;
   out_8533568921845714664[288] = 0;
   out_8533568921845714664[289] = 0;
   out_8533568921845714664[290] = 0;
   out_8533568921845714664[291] = 0;
   out_8533568921845714664[292] = 0;
   out_8533568921845714664[293] = 0;
   out_8533568921845714664[294] = 0;
   out_8533568921845714664[295] = 0;
   out_8533568921845714664[296] = 0;
   out_8533568921845714664[297] = 0;
   out_8533568921845714664[298] = 0;
   out_8533568921845714664[299] = 0;
   out_8533568921845714664[300] = 0;
   out_8533568921845714664[301] = 0;
   out_8533568921845714664[302] = 0;
   out_8533568921845714664[303] = 0;
   out_8533568921845714664[304] = 1;
   out_8533568921845714664[305] = 0;
   out_8533568921845714664[306] = 0;
   out_8533568921845714664[307] = 0;
   out_8533568921845714664[308] = 0;
   out_8533568921845714664[309] = 0;
   out_8533568921845714664[310] = 0;
   out_8533568921845714664[311] = 0;
   out_8533568921845714664[312] = 0;
   out_8533568921845714664[313] = 0;
   out_8533568921845714664[314] = 0;
   out_8533568921845714664[315] = 0;
   out_8533568921845714664[316] = 0;
   out_8533568921845714664[317] = 0;
   out_8533568921845714664[318] = 0;
   out_8533568921845714664[319] = 0;
   out_8533568921845714664[320] = 0;
   out_8533568921845714664[321] = 0;
   out_8533568921845714664[322] = 0;
   out_8533568921845714664[323] = 1;
}
void h_4(double *state, double *unused, double *out_2237381612538606718) {
   out_2237381612538606718[0] = state[6] + state[9];
   out_2237381612538606718[1] = state[7] + state[10];
   out_2237381612538606718[2] = state[8] + state[11];
}
void H_4(double *state, double *unused, double *out_1343491860345768218) {
   out_1343491860345768218[0] = 0;
   out_1343491860345768218[1] = 0;
   out_1343491860345768218[2] = 0;
   out_1343491860345768218[3] = 0;
   out_1343491860345768218[4] = 0;
   out_1343491860345768218[5] = 0;
   out_1343491860345768218[6] = 1;
   out_1343491860345768218[7] = 0;
   out_1343491860345768218[8] = 0;
   out_1343491860345768218[9] = 1;
   out_1343491860345768218[10] = 0;
   out_1343491860345768218[11] = 0;
   out_1343491860345768218[12] = 0;
   out_1343491860345768218[13] = 0;
   out_1343491860345768218[14] = 0;
   out_1343491860345768218[15] = 0;
   out_1343491860345768218[16] = 0;
   out_1343491860345768218[17] = 0;
   out_1343491860345768218[18] = 0;
   out_1343491860345768218[19] = 0;
   out_1343491860345768218[20] = 0;
   out_1343491860345768218[21] = 0;
   out_1343491860345768218[22] = 0;
   out_1343491860345768218[23] = 0;
   out_1343491860345768218[24] = 0;
   out_1343491860345768218[25] = 1;
   out_1343491860345768218[26] = 0;
   out_1343491860345768218[27] = 0;
   out_1343491860345768218[28] = 1;
   out_1343491860345768218[29] = 0;
   out_1343491860345768218[30] = 0;
   out_1343491860345768218[31] = 0;
   out_1343491860345768218[32] = 0;
   out_1343491860345768218[33] = 0;
   out_1343491860345768218[34] = 0;
   out_1343491860345768218[35] = 0;
   out_1343491860345768218[36] = 0;
   out_1343491860345768218[37] = 0;
   out_1343491860345768218[38] = 0;
   out_1343491860345768218[39] = 0;
   out_1343491860345768218[40] = 0;
   out_1343491860345768218[41] = 0;
   out_1343491860345768218[42] = 0;
   out_1343491860345768218[43] = 0;
   out_1343491860345768218[44] = 1;
   out_1343491860345768218[45] = 0;
   out_1343491860345768218[46] = 0;
   out_1343491860345768218[47] = 1;
   out_1343491860345768218[48] = 0;
   out_1343491860345768218[49] = 0;
   out_1343491860345768218[50] = 0;
   out_1343491860345768218[51] = 0;
   out_1343491860345768218[52] = 0;
   out_1343491860345768218[53] = 0;
}
void h_10(double *state, double *unused, double *out_2863719947197642864) {
   out_2863719947197642864[0] = 9.8100000000000005*sin(state[1]) - state[4]*state[8] + state[5]*state[7] + state[12] + state[15];
   out_2863719947197642864[1] = -9.8100000000000005*sin(state[0])*cos(state[1]) + state[3]*state[8] - state[5]*state[6] + state[13] + state[16];
   out_2863719947197642864[2] = -9.8100000000000005*cos(state[0])*cos(state[1]) - state[3]*state[7] + state[4]*state[6] + state[14] + state[17];
}
void H_10(double *state, double *unused, double *out_5144280124555213416) {
   out_5144280124555213416[0] = 0;
   out_5144280124555213416[1] = 9.8100000000000005*cos(state[1]);
   out_5144280124555213416[2] = 0;
   out_5144280124555213416[3] = 0;
   out_5144280124555213416[4] = -state[8];
   out_5144280124555213416[5] = state[7];
   out_5144280124555213416[6] = 0;
   out_5144280124555213416[7] = state[5];
   out_5144280124555213416[8] = -state[4];
   out_5144280124555213416[9] = 0;
   out_5144280124555213416[10] = 0;
   out_5144280124555213416[11] = 0;
   out_5144280124555213416[12] = 1;
   out_5144280124555213416[13] = 0;
   out_5144280124555213416[14] = 0;
   out_5144280124555213416[15] = 1;
   out_5144280124555213416[16] = 0;
   out_5144280124555213416[17] = 0;
   out_5144280124555213416[18] = -9.8100000000000005*cos(state[0])*cos(state[1]);
   out_5144280124555213416[19] = 9.8100000000000005*sin(state[0])*sin(state[1]);
   out_5144280124555213416[20] = 0;
   out_5144280124555213416[21] = state[8];
   out_5144280124555213416[22] = 0;
   out_5144280124555213416[23] = -state[6];
   out_5144280124555213416[24] = -state[5];
   out_5144280124555213416[25] = 0;
   out_5144280124555213416[26] = state[3];
   out_5144280124555213416[27] = 0;
   out_5144280124555213416[28] = 0;
   out_5144280124555213416[29] = 0;
   out_5144280124555213416[30] = 0;
   out_5144280124555213416[31] = 1;
   out_5144280124555213416[32] = 0;
   out_5144280124555213416[33] = 0;
   out_5144280124555213416[34] = 1;
   out_5144280124555213416[35] = 0;
   out_5144280124555213416[36] = 9.8100000000000005*sin(state[0])*cos(state[1]);
   out_5144280124555213416[37] = 9.8100000000000005*sin(state[1])*cos(state[0]);
   out_5144280124555213416[38] = 0;
   out_5144280124555213416[39] = -state[7];
   out_5144280124555213416[40] = state[6];
   out_5144280124555213416[41] = 0;
   out_5144280124555213416[42] = state[4];
   out_5144280124555213416[43] = -state[3];
   out_5144280124555213416[44] = 0;
   out_5144280124555213416[45] = 0;
   out_5144280124555213416[46] = 0;
   out_5144280124555213416[47] = 0;
   out_5144280124555213416[48] = 0;
   out_5144280124555213416[49] = 0;
   out_5144280124555213416[50] = 1;
   out_5144280124555213416[51] = 0;
   out_5144280124555213416[52] = 0;
   out_5144280124555213416[53] = 1;
}
void h_13(double *state, double *unused, double *out_883477765788198364) {
   out_883477765788198364[0] = state[3];
   out_883477765788198364[1] = state[4];
   out_883477765788198364[2] = state[5];
}
void H_13(double *state, double *unused, double *out_1868781964986564583) {
   out_1868781964986564583[0] = 0;
   out_1868781964986564583[1] = 0;
   out_1868781964986564583[2] = 0;
   out_1868781964986564583[3] = 1;
   out_1868781964986564583[4] = 0;
   out_1868781964986564583[5] = 0;
   out_1868781964986564583[6] = 0;
   out_1868781964986564583[7] = 0;
   out_1868781964986564583[8] = 0;
   out_1868781964986564583[9] = 0;
   out_1868781964986564583[10] = 0;
   out_1868781964986564583[11] = 0;
   out_1868781964986564583[12] = 0;
   out_1868781964986564583[13] = 0;
   out_1868781964986564583[14] = 0;
   out_1868781964986564583[15] = 0;
   out_1868781964986564583[16] = 0;
   out_1868781964986564583[17] = 0;
   out_1868781964986564583[18] = 0;
   out_1868781964986564583[19] = 0;
   out_1868781964986564583[20] = 0;
   out_1868781964986564583[21] = 0;
   out_1868781964986564583[22] = 1;
   out_1868781964986564583[23] = 0;
   out_1868781964986564583[24] = 0;
   out_1868781964986564583[25] = 0;
   out_1868781964986564583[26] = 0;
   out_1868781964986564583[27] = 0;
   out_1868781964986564583[28] = 0;
   out_1868781964986564583[29] = 0;
   out_1868781964986564583[30] = 0;
   out_1868781964986564583[31] = 0;
   out_1868781964986564583[32] = 0;
   out_1868781964986564583[33] = 0;
   out_1868781964986564583[34] = 0;
   out_1868781964986564583[35] = 0;
   out_1868781964986564583[36] = 0;
   out_1868781964986564583[37] = 0;
   out_1868781964986564583[38] = 0;
   out_1868781964986564583[39] = 0;
   out_1868781964986564583[40] = 0;
   out_1868781964986564583[41] = 1;
   out_1868781964986564583[42] = 0;
   out_1868781964986564583[43] = 0;
   out_1868781964986564583[44] = 0;
   out_1868781964986564583[45] = 0;
   out_1868781964986564583[46] = 0;
   out_1868781964986564583[47] = 0;
   out_1868781964986564583[48] = 0;
   out_1868781964986564583[49] = 0;
   out_1868781964986564583[50] = 0;
   out_1868781964986564583[51] = 0;
   out_1868781964986564583[52] = 0;
   out_1868781964986564583[53] = 0;
}
void h_14(double *state, double *unused, double *out_8137969777177408347) {
   out_8137969777177408347[0] = state[6];
   out_8137969777177408347[1] = state[7];
   out_8137969777177408347[2] = state[8];
}
void H_14(double *state, double *unused, double *out_2619748995993716311) {
   out_2619748995993716311[0] = 0;
   out_2619748995993716311[1] = 0;
   out_2619748995993716311[2] = 0;
   out_2619748995993716311[3] = 0;
   out_2619748995993716311[4] = 0;
   out_2619748995993716311[5] = 0;
   out_2619748995993716311[6] = 1;
   out_2619748995993716311[7] = 0;
   out_2619748995993716311[8] = 0;
   out_2619748995993716311[9] = 0;
   out_2619748995993716311[10] = 0;
   out_2619748995993716311[11] = 0;
   out_2619748995993716311[12] = 0;
   out_2619748995993716311[13] = 0;
   out_2619748995993716311[14] = 0;
   out_2619748995993716311[15] = 0;
   out_2619748995993716311[16] = 0;
   out_2619748995993716311[17] = 0;
   out_2619748995993716311[18] = 0;
   out_2619748995993716311[19] = 0;
   out_2619748995993716311[20] = 0;
   out_2619748995993716311[21] = 0;
   out_2619748995993716311[22] = 0;
   out_2619748995993716311[23] = 0;
   out_2619748995993716311[24] = 0;
   out_2619748995993716311[25] = 1;
   out_2619748995993716311[26] = 0;
   out_2619748995993716311[27] = 0;
   out_2619748995993716311[28] = 0;
   out_2619748995993716311[29] = 0;
   out_2619748995993716311[30] = 0;
   out_2619748995993716311[31] = 0;
   out_2619748995993716311[32] = 0;
   out_2619748995993716311[33] = 0;
   out_2619748995993716311[34] = 0;
   out_2619748995993716311[35] = 0;
   out_2619748995993716311[36] = 0;
   out_2619748995993716311[37] = 0;
   out_2619748995993716311[38] = 0;
   out_2619748995993716311[39] = 0;
   out_2619748995993716311[40] = 0;
   out_2619748995993716311[41] = 0;
   out_2619748995993716311[42] = 0;
   out_2619748995993716311[43] = 0;
   out_2619748995993716311[44] = 1;
   out_2619748995993716311[45] = 0;
   out_2619748995993716311[46] = 0;
   out_2619748995993716311[47] = 0;
   out_2619748995993716311[48] = 0;
   out_2619748995993716311[49] = 0;
   out_2619748995993716311[50] = 0;
   out_2619748995993716311[51] = 0;
   out_2619748995993716311[52] = 0;
   out_2619748995993716311[53] = 0;
}
#include <eigen3/Eigen/Dense>
#include <iostream>

typedef Eigen::Matrix<double, DIM, DIM, Eigen::RowMajor> DDM;
typedef Eigen::Matrix<double, EDIM, EDIM, Eigen::RowMajor> EEM;
typedef Eigen::Matrix<double, DIM, EDIM, Eigen::RowMajor> DEM;

void predict(double *in_x, double *in_P, double *in_Q, double dt) {
  typedef Eigen::Matrix<double, MEDIM, MEDIM, Eigen::RowMajor> RRM;

  double nx[DIM] = {0};
  double in_F[EDIM*EDIM] = {0};

  // functions from sympy
  f_fun(in_x, dt, nx);
  F_fun(in_x, dt, in_F);


  EEM F(in_F);
  EEM P(in_P);
  EEM Q(in_Q);

  RRM F_main = F.topLeftCorner(MEDIM, MEDIM);
  P.topLeftCorner(MEDIM, MEDIM) = (F_main * P.topLeftCorner(MEDIM, MEDIM)) * F_main.transpose();
  P.topRightCorner(MEDIM, EDIM - MEDIM) = F_main * P.topRightCorner(MEDIM, EDIM - MEDIM);
  P.bottomLeftCorner(EDIM - MEDIM, MEDIM) = P.bottomLeftCorner(EDIM - MEDIM, MEDIM) * F_main.transpose();

  P = P + dt*Q;

  // copy out state
  memcpy(in_x, nx, DIM * sizeof(double));
  memcpy(in_P, P.data(), EDIM * EDIM * sizeof(double));
}

// note: extra_args dim only correct when null space projecting
// otherwise 1
template <int ZDIM, int EADIM, bool MAHA_TEST>
void update(double *in_x, double *in_P, Hfun h_fun, Hfun H_fun, Hfun Hea_fun, double *in_z, double *in_R, double *in_ea, double MAHA_THRESHOLD) {
  typedef Eigen::Matrix<double, ZDIM, ZDIM, Eigen::RowMajor> ZZM;
  typedef Eigen::Matrix<double, ZDIM, DIM, Eigen::RowMajor> ZDM;
  typedef Eigen::Matrix<double, Eigen::Dynamic, EDIM, Eigen::RowMajor> XEM;
  //typedef Eigen::Matrix<double, EDIM, ZDIM, Eigen::RowMajor> EZM;
  typedef Eigen::Matrix<double, Eigen::Dynamic, 1> X1M;
  typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> XXM;

  double in_hx[ZDIM] = {0};
  double in_H[ZDIM * DIM] = {0};
  double in_H_mod[EDIM * DIM] = {0};
  double delta_x[EDIM] = {0};
  double x_new[DIM] = {0};


  // state x, P
  Eigen::Matrix<double, ZDIM, 1> z(in_z);
  EEM P(in_P);
  ZZM pre_R(in_R);

  // functions from sympy
  h_fun(in_x, in_ea, in_hx);
  H_fun(in_x, in_ea, in_H);
  ZDM pre_H(in_H);

  // get y (y = z - hx)
  Eigen::Matrix<double, ZDIM, 1> pre_y(in_hx); pre_y = z - pre_y;
  X1M y; XXM H; XXM R;
  if (Hea_fun){
    typedef Eigen::Matrix<double, ZDIM, EADIM, Eigen::RowMajor> ZAM;
    double in_Hea[ZDIM * EADIM] = {0};
    Hea_fun(in_x, in_ea, in_Hea);
    ZAM Hea(in_Hea);
    XXM A = Hea.transpose().fullPivLu().kernel();


    y = A.transpose() * pre_y;
    H = A.transpose() * pre_H;
    R = A.transpose() * pre_R * A;
  } else {
    y = pre_y;
    H = pre_H;
    R = pre_R;
  }
  // get modified H
  H_mod_fun(in_x, in_H_mod);
  DEM H_mod(in_H_mod);
  XEM H_err = H * H_mod;

  // Do mahalobis distance test
  if (MAHA_TEST){
    XXM a = (H_err * P * H_err.transpose() + R).inverse();
    double maha_dist = y.transpose() * a * y;
    if (maha_dist > MAHA_THRESHOLD){
      R = 1.0e16 * R;
    }
  }

  // Outlier resilient weighting
  double weight = 1;//(1.5)/(1 + y.squaredNorm()/R.sum());

  // kalman gains and I_KH
  XXM S = ((H_err * P) * H_err.transpose()) + R/weight;
  XEM KT = S.fullPivLu().solve(H_err * P.transpose());
  //EZM K = KT.transpose(); TODO: WHY DOES THIS NOT COMPILE?
  //EZM K = S.fullPivLu().solve(H_err * P.transpose()).transpose();
  //std::cout << "Here is the matrix rot:\n" << K << std::endl;
  EEM I_KH = Eigen::Matrix<double, EDIM, EDIM>::Identity() - (KT.transpose() * H_err);

  // update state by injecting dx
  Eigen::Matrix<double, EDIM, 1> dx(delta_x);
  dx  = (KT.transpose() * y);
  memcpy(delta_x, dx.data(), EDIM * sizeof(double));
  err_fun(in_x, delta_x, x_new);
  Eigen::Matrix<double, DIM, 1> x(x_new);

  // update cov
  P = ((I_KH * P) * I_KH.transpose()) + ((KT.transpose() * R) * KT);

  // copy out state
  memcpy(in_x, x.data(), DIM * sizeof(double));
  memcpy(in_P, P.data(), EDIM * EDIM * sizeof(double));
  memcpy(in_z, y.data(), y.rows() * sizeof(double));
}




}
extern "C" {

void pose_update_4(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea) {
  update<3, 3, 0>(in_x, in_P, h_4, H_4, NULL, in_z, in_R, in_ea, MAHA_THRESH_4);
}
void pose_update_10(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea) {
  update<3, 3, 0>(in_x, in_P, h_10, H_10, NULL, in_z, in_R, in_ea, MAHA_THRESH_10);
}
void pose_update_13(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea) {
  update<3, 3, 0>(in_x, in_P, h_13, H_13, NULL, in_z, in_R, in_ea, MAHA_THRESH_13);
}
void pose_update_14(double *in_x, double *in_P, double *in_z, double *in_R, double *in_ea) {
  update<3, 3, 0>(in_x, in_P, h_14, H_14, NULL, in_z, in_R, in_ea, MAHA_THRESH_14);
}
void pose_err_fun(double *nom_x, double *delta_x, double *out_4693081389883303657) {
  err_fun(nom_x, delta_x, out_4693081389883303657);
}
void pose_inv_err_fun(double *nom_x, double *true_x, double *out_3043232131163915215) {
  inv_err_fun(nom_x, true_x, out_3043232131163915215);
}
void pose_H_mod_fun(double *state, double *out_4504589946406190503) {
  H_mod_fun(state, out_4504589946406190503);
}
void pose_f_fun(double *state, double dt, double *out_3053812152163561705) {
  f_fun(state,  dt, out_3053812152163561705);
}
void pose_F_fun(double *state, double dt, double *out_8533568921845714664) {
  F_fun(state,  dt, out_8533568921845714664);
}
void pose_h_4(double *state, double *unused, double *out_2237381612538606718) {
  h_4(state, unused, out_2237381612538606718);
}
void pose_H_4(double *state, double *unused, double *out_1343491860345768218) {
  H_4(state, unused, out_1343491860345768218);
}
void pose_h_10(double *state, double *unused, double *out_2863719947197642864) {
  h_10(state, unused, out_2863719947197642864);
}
void pose_H_10(double *state, double *unused, double *out_5144280124555213416) {
  H_10(state, unused, out_5144280124555213416);
}
void pose_h_13(double *state, double *unused, double *out_883477765788198364) {
  h_13(state, unused, out_883477765788198364);
}
void pose_H_13(double *state, double *unused, double *out_1868781964986564583) {
  H_13(state, unused, out_1868781964986564583);
}
void pose_h_14(double *state, double *unused, double *out_8137969777177408347) {
  h_14(state, unused, out_8137969777177408347);
}
void pose_H_14(double *state, double *unused, double *out_2619748995993716311) {
  H_14(state, unused, out_2619748995993716311);
}
void pose_predict(double *in_x, double *in_P, double *in_Q, double dt) {
  predict(in_x, in_P, in_Q, dt);
}
}

const EKF pose = {
  .name = "pose",
  .kinds = { 4, 10, 13, 14 },
  .feature_kinds = {  },
  .f_fun = pose_f_fun,
  .F_fun = pose_F_fun,
  .err_fun = pose_err_fun,
  .inv_err_fun = pose_inv_err_fun,
  .H_mod_fun = pose_H_mod_fun,
  .predict = pose_predict,
  .hs = {
    { 4, pose_h_4 },
    { 10, pose_h_10 },
    { 13, pose_h_13 },
    { 14, pose_h_14 },
  },
  .Hs = {
    { 4, pose_H_4 },
    { 10, pose_H_10 },
    { 13, pose_H_13 },
    { 14, pose_H_14 },
  },
  .updates = {
    { 4, pose_update_4 },
    { 10, pose_update_10 },
    { 13, pose_update_13 },
    { 14, pose_update_14 },
  },
  .Hes = {
  },
  .sets = {
  },
  .extra_routines = {
  },
};

ekf_lib_init(pose)
