#pragma once


// !!!! ����������� ����� ����   ���������, ���� �� ��������� ��� ���

#define FALSE  0
#define TRUE   1



int logic_init();


int read_settings();


unsigned SurzaPeriod();  //������ ����� � �������������


#define  INDI_PERIOD_MS       1000            //������� �������� ����������� � �������������
#define  JOURNAL_PERIOD_MS    1000            //������� �������� ��������� msg_type_journal_info_t


void indi_send();   //��������� � ������� ����� ����������

void journal_update();