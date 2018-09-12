/*
 * File: MYD.c
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

#include "MYD.h"
#include "MYD_private.h"

/* Block states (auto storage) */
DW_MYD_T MYD_DW;

/* External inputs (root inport signals with auto storage) */
ExtU_MYD_T MYD_U;

/* External outputs (root outports fed by signals with auto storage) */
ExtY_MYD_T MYD_Y;

/* Real-time model */
RT_MODEL_MYD_T MYD_M_;
RT_MODEL_MYD_T *const MYD_M = &MYD_M_;

/* Model step function */
void MYD_step(void)
{
  /* Outputs for Atomic SubSystem: '<Root>/MYD' */
  /* Outport: '<Root>/Out_Boolean' incorporates:
   *  UnitDelay: '<S1>/Unit Delay1'
   */
  memcpy(&MYD_Y.Out_Boolean[0], &MYD_DW.UnitDelay1_DSTATE[0], 1000U * sizeof
         (boolean_T));

  /* Outport: '<Root>/Out_Int' incorporates:
   *  UnitDelay: '<S1>/Unit Delay2'
   */
  memcpy(&MYD_Y.Out_Int[0], &MYD_DW.UnitDelay2_DSTATE[0], 200U * sizeof(int32_T));

  /* Outport: '<Root>/Out_Real' incorporates:
   *  UnitDelay: '<S1>/Unit Delay3'
   */
  memcpy(&MYD_Y.Out_Real[0], &MYD_DW.UnitDelay3_DSTATE[0], 200U * sizeof
         (real32_T));

  /* Update for UnitDelay: '<S1>/Unit Delay1' incorporates:
   *  Update for Inport: '<Root>/In_Boolean'
   */
  memcpy(&MYD_DW.UnitDelay1_DSTATE[0], &MYD_U.In_Boolean[0], 1000U * sizeof
         (boolean_T));

  /* Update for UnitDelay: '<S1>/Unit Delay2' incorporates:
   *  Update for Inport: '<Root>/In_Int'
   */
  memcpy(&MYD_DW.UnitDelay2_DSTATE[0], &MYD_U.In_Int[0], 200U * sizeof(int32_T));

  /* Update for UnitDelay: '<S1>/Unit Delay3' incorporates:
   *  Update for Inport: '<Root>/In_Real'
   */
  memcpy(&MYD_DW.UnitDelay3_DSTATE[0], &MYD_U.In_Real[0], 200U * sizeof(real32_T));

  /* End of Outputs for SubSystem: '<Root>/MYD' */
}

/* Model initialize function */
void MYD_initialize(void)
{
  /* Registration code */

  /* initialize error status */
  rtmSetErrorStatus(MYD_M, (NULL));

  /* states (dwork) */
  (void) memset((void *)&MYD_DW, 0,
                sizeof(DW_MYD_T));

  /* external inputs */
  (void)memset((void *)&MYD_U, 0, sizeof(ExtU_MYD_T));

  /* external outputs */
  (void) memset((void *)&MYD_Y, 0,
                sizeof(ExtY_MYD_T));
}

/* Model terminate function */
void MYD_terminate(void)
{
  /* (no terminate code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
