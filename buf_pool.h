#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#include "global_defines.h"

#define _BUF_POOL_DEBUG_PRINT



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
void* buf_pool_get_fast(int pool_num);  //������� ������ ��� ��������

//������ �����
//�-��� ���������������� (������ ������)
bool buf_pool_free(int pool_num, void*);
bool buf_pool_free_fast(int pool_num, void*); //������� ������ ��� ��������

//�������� ������ ������, 0 � ������ ������
size_t buf_pool_size(int pool_num);

//�������� ���������� ��������� ������� � ����, <0 ��� ������
int buf_pool_bufs_available(int pool_num);
int buf_pool_bufs_available_fast(int pool_num); //������� ������ ��� ��������

//----------------------
//�������� �������
#ifdef BUF_POOL_DEBUG_PRINT
int buf_pool_test();
#endif
