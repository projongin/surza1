#pragma once

#include <stdint.h>
#include <stdbool.h>


typedef void(*logic_adc_handler)(void);

//�������������
bool InitAI8S(unsigned adc_num, unsigned adc1_adr, unsigned adc2_adr, unsigned period, logic_adc_handler);

//������ ������� ���
void ai8s_start_second_adc();

//�������� ��������� ������� ���
bool ai8s_wait_second_adc();

//������ ������ ���
int32_t ai8s_read_ch(unsigned adc_num, unsigned ch_num);
