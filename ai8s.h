#pragma once

#include <stdint.h>
#include <stdbool.h>


typedef void(*logic_adc_handler)(void);

//инициализация
bool InitAI8S(unsigned adc_num, unsigned adc1_adr, unsigned adc2_adr, unsigned period, logic_adc_handler);

//запуск второго ацп
void ai8s_start_second_adc();

//ожидание измерений второго ацп
bool ai8s_wait_second_adc();

//чтение канала ацп
int32_t ai8s_read_ch(unsigned adc_num, unsigned ch_num);
