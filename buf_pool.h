#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#include "global_defines.h"

#define BUF_POOL_DEBUG_PRINT



//--- �������������� ���������� ���������� ���������� ��� ���������� �������
#ifndef ALLOW_DEBUG_PRINT
#ifdef BUF_POOL_DEBUG_PRINT
#undef BUF_POOL_DEBUG_PRINT
#endif
#endif




//������������� ���� �������
bool buf_pool_init();

//������� ��� �������, ���������� ���������� ���� ��� <0 ��� ������
//�-��� ������ ��� ������������� �������������
int buf_pool_add_pool(size_t buf_size, unsigned buf_count);


//�������� ����� �� ����,   NULL  � ������ ������
//�-��� ���������������� (������ ������)
void* buf_pool_get(int pool_num);

//������ �����
//�-��� ���������������� (������ ������)
bool buf_pool_free(int pool_num, void*);

//�������� ������ ������, 0 � ������ ������
size_t buf_pool_size(int pool_num);


//----------------------
//�������� �������
#ifdef BUF_POOL_DEBUG_PRINT
int buf_pool_test();
#endif
