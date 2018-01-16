#pragma once


//=============================================
// ��������� ��������
//=============================================

//��������� ������ int
void atom_set_state(volatile int *s, int val);

//������
int atom_get_state(volatile int *s);

//���������
void atom_inc(volatile int *num);

//���������
void atom_dec(volatile int *num);

//�����
int atom_xchg(volatile int *m, int val);

//����������
void atom_add(volatile int *num, int val);

//���������
void atom_sub(volatile int *num, int val);

//------------------------------------------------------------------------------



#include "Rtk32.h"
#include <stdbool.h>

// �������� ������������ �������� (start - ����� ���������, stop - ����� ������������, time - ������� �����)
bool net_timeout_expired(RTKTime start, RTKTime stop, RTKTime time);

