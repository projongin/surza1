#pragma once


int logic_init();


int read_settings();


unsigned SurzaPeriod();  //������ ����� � �������������


#define  INDI_PERIOD_MS   1000            //������� �������� ����������� � �������������

void indi_send();   //��������� � ������� ����� ����������
