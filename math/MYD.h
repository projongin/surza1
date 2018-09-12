/*
 * File: MYD.h
 *
 * Code generated for Simulink model 'MYD'.
 *
 * Model version                  : 1.745
 * Simulink Coder version         : 8.11 (R2016b) 25-Aug-2016
 * C/C++ source code generated on : Wed Sep 12 14:03:53 2018
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: Generic->32-bit x86 compatible
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_MYD_h_
#define RTW_HEADER_MYD_h_
#include <string.h>
#include <stddef.h>
#ifndef MYD_COMMON_INCLUDES_
# define MYD_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* MYD_COMMON_INCLUDES_ */

#include "MYD_types.h"

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
# define rtmGetErrorStatus(rtm)        ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
# define rtmSetErrorStatus(rtm, val)   ((rtm)->errorStatus = (val))
#endif

/* Block states (auto storage) for system '<Root>' */
typedef struct {
  real32_T UnitDelay3_DSTATE[200];     /* '<S1>/Unit Delay3' */
  int32_T UnitDelay2_DSTATE[200];      /* '<S1>/Unit Delay2' */
  boolean_T UnitDelay1_DSTATE[1000];   /* '<S1>/Unit Delay1' */
} DW_MYD_T;

/* External inputs (root inport signals with auto storage) */
typedef struct {
  boolean_T In_Boolean[1000];          /* '<Root>/In_Boolean' */
  int32_T In_Int[200];                 /* '<Root>/In_Int' */
  real32_T In_Real[200];               /* '<Root>/In_Real' */
} ExtU_MYD_T;

/* External outputs (root outports fed by signals with auto storage) */
typedef struct {
  boolean_T Out_Boolean[1000];         /* '<Root>/Out_Boolean' */
  int32_T Out_Int[200];                /* '<Root>/Out_Int' */
  real32_T Out_Real[200];              /* '<Root>/Out_Real' */
} ExtY_MYD_T;

/* Real-time Model Data Structure */
struct tag_RTM_MYD_T {
  const char_T * volatile errorStatus;
};

/* Block states (auto storage) */
extern DW_MYD_T MYD_DW;

/* External inputs (root inport signals with auto storage) */
extern ExtU_MYD_T MYD_U;

/* External outputs (root outports fed by signals with auto storage) */
extern ExtY_MYD_T MYD_Y;

/* Model entry point functions */
extern void MYD_initialize(void);
extern void MYD_step(void);
extern void MYD_terminate(void);

/* Real-time Model object */
extern RT_MODEL_MYD_T *const MYD_M;

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Note that this particular code originates from a subsystem build,
 * and has its own system numbers different from the parent model.
 * Refer to the system hierarchy for this subsystem below, and use the
 * MATLAB hilite_system command to trace the generated code back
 * to the parent model.  For example,
 *
 * hilite_system('SU_in_out/MYD')    - opens subsystem SU_in_out/MYD
 * hilite_system('SU_in_out/MYD/Kp') - opens and selects block Kp
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'SU_in_out'
 * '<S1>'   : 'SU_in_out/MYD'
 */
#endif                                 /* RTW_HEADER_MYD_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
