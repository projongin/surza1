#pragma once


// !!!! ОПРЕДЕЛЕНИЕ ТИПОВ МЯДА   ВРЕМЕННОЕ, ПОКА НЕ ПОДКЛЮЧЕН САМ МЯД

#define FALSE  0
#define TRUE   1



int logic_init();


int read_settings();


unsigned SurzaPeriod();  //период сурзы в микросекундах


#define  INDI_PERIOD_MS       1000            //частота отправки индикаторов в миллисекундах
#define  JOURNAL_PERIOD_MS    1000            //частота отправки сообщения msg_type_journal_info_t


void indi_send();   //заполнить и послать новые индикаторы

void journal_update();