#pragma once


//=============================================
// атомарные операции
//=============================================

//атомарная запись int
void atom_set_state(volatile int *s, int val);

//чтение
int atom_get_state(volatile int *s);

//инкремент
void atom_inc(volatile int *num);

//декремент
void atom_dec(volatile int *num);

//обмен
int atom_xchg(volatile int *m, int val);

//добавление
void atom_add(volatile int *num, int val);

//вычитание
void atom_sub(volatile int *num, int val);

//------------------------------------------------------------------------------



#include "Rtk32.h"
#include <stdbool.h>
#include <stdint.h>

// проверка сработавшего таймаута (start - время установки, stop - время срабатывания, time - текущее время)
bool net_timeout_expired(RTKTime start, RTKTime stop, RTKTime time);



typedef struct {
	bool base_init;
	bool settings_init;
	bool net_init;
	bool logic_init;
} init_flags_t;

extern volatile init_flags_t init_flags;

void common_init();


void wdt_init();
extern inline void wdt_update();

void reboot();


#include "net_messages.h"

//--------- steady clock -------------
int steady_clock_get();
void steady_clock_update(int us);
bool steady_clock_expired_now(int32_t start_time, uint32_t timeout);
bool steady_clock_expired(int32_t start_time, int32_t stop_time, uint32_t timeout);
//------------------------------------

//------- time ---------------------
//добавить ко времени time  nsecs наносекунд
surza_time_t SurzaTime_add(surza_time_t time, uint64_t nsecs);
//отнять от времени time  nsecs наносекунд
surza_time_t SurzaTime_sub(surza_time_t time, uint64_t nsecs);
//----------------------------------


//-------------------------------
void global_spinlock_lock();
void global_spinlock_unlock();
//-------------------------------

//получение счетчика тактов процессора (использовать только если одно ядро и нет динамического изменения частоты процессора, иначе надо делать по другому)
int __stdcall get_cpu_clks();



#ifdef CPU_EXCEPTION_14_DEBUG
#include "cpu_exception.h"
#define DEBUG_ADD_POINT(x)  debug_add_point((x));
#else
#define DEBUG_ADD_POINT(x)
#endif
