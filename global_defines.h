#pragma once

//������ ������ �������
#define DISABLE_WDT  0



//��������� ������� cpu exceptions
#define  CPU_EXCEPTION_DEBUG_



// �������������� ���������� ������ ������ (���� �� ���������� ������ � ��� �������)
#ifndef MSVS_DEBUG
#if DISABLE_WDT != 1
#define WDT_EN
#endif
#endif

#ifdef CPU_EXCEPTION_DEBUG
#ifdef WDT_EN
#undef WDT_EN
#endif
#endif


#ifdef CPU_EXCEPTION_DEBUG
#include "cpu_exception.h"
#define DEBUG_ADD_POINT(x)  debug_add_point((x));
#else
#define DEBUG_ADD_POINT(x)
#endif



// ���������� ���������� ������������� ���������� ���������� � �������
#define  _ALLOW_DEBUG_PRINT

// ��������� �������� ����������� �� ������
#define DELTA_HMI_ENABLE


//����� CPC150
#define CPC150
