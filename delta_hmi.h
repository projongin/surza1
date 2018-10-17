#pragma once

#include <stdint.h>


typedef void (*delta_set_regs_callback)(uint16_t* ptr, uint16_t* start_reg, uint16_t* num);


void delta_hmi_open(delta_set_regs_callback set_reg_callback);

void delta_hmi_update();

void delta_hmi_close();

void delta_hmi_write(const uint16_t* ptr);

extern volatile uint16_t HMI_input_regs[10];

