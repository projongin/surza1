#pragma once



// ���������� ������ ������
#ifndef MSVS_DEBUG
#define WDT_EN
#endif

// ���������� ���������� ������������� ���������� ���������� � �������
#define  _ALLOW_DEBUG_PRINT

// ��������� �������� ����������� �� ������
#define DELTA_HMI_ENABLE


// ��������� ������ ������ �� ������ ��� ��� ����������� ��  (0-���������, 1-��������)
#define SHU_DEBUG_MODE_ONE_CELL     0

// ��������� ������ ������ � ����������� ���������� ��� ����������� ��  (0-���������, 1-��������)
#define SHU_DEBUG_MODE_NO_FEEDBACK  0


//��������� ������� page fault
#define  _CPU_EXCEPTION_14_DEBUG
