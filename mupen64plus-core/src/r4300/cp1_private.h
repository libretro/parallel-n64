#ifndef M64P_R4300_CP1_PRIVATE_H
#define M64P_R4300_CP1_PRIVATE_H

#include "cp1.h"

#include <stdint.h>

extern float *reg_cop1_simple[32];
extern double *reg_cop1_double[32];
extern uint32_t FCR0, FCR31;
extern int64_t reg_cop1_fgr_64[32];
extern uint32_t rounding_mode;

#endif /* M64P_R4300_CP1_PRIVATE_H */
