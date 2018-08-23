#pragma once


int logic_init();


int read_settings();


unsigned SurzaPeriod();  //период сурзы в микросекундах


#define  INDI_PERIOD_MS   1000            //частота отправки индикаторов в миллисекундах

void indi_send();   //заполнить и послать новые индикаторы
