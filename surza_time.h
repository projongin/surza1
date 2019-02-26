#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "net_messages.h"
#include "param_tree.h"

//����������� ����� ��� � ����������, ��� � � �������� ��������� 
surza_time_t  time_get();


void time_isr_update();


void time_init();




//--------- steady clock -------------
int steady_clock_get();
void steady_clock_update(int us);
bool steady_clock_expired_now(int32_t start_time, uint32_t timeout);
bool steady_clock_expired(int32_t start_time, int32_t stop_time, uint32_t timeout);
//------------------------------------

//------- time ---------------------
//�������� �� ������� time  nsecs ����������
surza_time_t SurzaTime_add(surza_time_t time, uint64_t nsecs);
//������ �� ������� time  nsecs ����������
surza_time_t SurzaTime_sub(surza_time_t time, uint64_t nsecs);
//----------------------------------


//��������� �������� ������ ���������� (������������ ������ ���� ���� ���� � ��� ������������� ��������� ������� ����������, ����� ���� ������ �� �������)
int __stdcall get_cpu_clks();
