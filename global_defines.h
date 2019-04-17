#pragma once



// разрешение работы собаки
#ifndef MSVS_DEBUG
#define WDT_EN
#endif

// глобальное разрешение распечатывать отладочную информацию в модулях
#define  _ALLOW_DEBUG_PRINT

// включение отправку индикаторов на дельту
#define DELTA_HMI_ENABLE


// включение режима работы по одному ДКЯ для встроенного ШУ  (0-выключено, 1-включено)
#define SHU_DEBUG_MODE_ONE_CELL     0

// включение режима работы с безусловным включением для встроенного ШУ  (0-выключено, 1-включено)
#define SHU_DEBUG_MODE_NO_FEEDBACK  0


//включение отладки page fault
#define  _CPU_EXCEPTION_14_DEBUG
