#pragma once



// разрешение работы собаки
#ifndef MSVS_DEBUG
#define WDT_EN
#endif

// глобальное разрешение распечатывать отладочную информацию в модулях
#define  _ALLOW_DEBUG_PRINT

// включение отправку индикаторов на дельту
#define DELTA_HMI_ENABLE



//включение отладки page fault
#define  _CPU_EXCEPTION_14_DEBUG

//плата CPC150
#define CPC150
