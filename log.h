#pragma once

#include <stdio.h>


//�������� ������ � ���
void Log(const char* s);

//����� �� ����� � � ���
void LogAndScreen(const char*s);




//����� ������ ��� ������ ������ � ����������� ��� ������������� �������
extern char common_str[1024];
//------------------------------------------------------------------------------



#define BLOCK_LOG_AND_SCREEN_


#ifndef BLOCK_LOG_AND_SCREEN
#define LOG_AND_SCREEN(...)    {sprintf_s(common_str, sizeof(common_str), __VA_ARGS__); \
                                LogAndScreen(common_str);}
#else
#define LOG_AND_SCREEN(...)  ;
#endif


#define LOG(...)    {sprintf_s(common_str, sizeof(common_str), __VA_ARGS__); \
                     Log(common_str);}

