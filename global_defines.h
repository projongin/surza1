#pragma once

//запрет работы вочдога
#define DISABLE_WDT  0



//включение отладки cpu exceptions
#define  CPU_EXCEPTION_DEBUG_



// автоматическое разрешение работы собаки (если не отладочная сборка и нет запрета)
#ifndef MSVS_DEBUG
#if DISABLE_WDT != 1
#define WDT_EN
#endif
#endif

#ifdef CPU_EXCEPTION_DEBUG
#ifdef WDT_EN
#undef WDT_EN
#endif
#endif



// глобальное разрешение распечатывать отладочную информацию в модулях
#define  _ALLOW_DEBUG_PRINT

// включение отправку индикаторов на дельту
#define DELTA_HMI_ENABLE


//плата CPC150
#define _CPC150
