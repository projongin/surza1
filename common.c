
#include "Rtk32.h"
#include <stdbool.h>
#include <stdint.h>

#include "global_defines.h"
#include "common.h"



//=============================================
// атомарные операции
//=============================================

//атомарная запись int
void atom_set_state(volatile int *s, int val){
	_asm
	{
		mov     eax, val
		mov     esi, s
		lock    xchg    eax, DWORD PTR[esi]
	}
}

//чтение
int atom_get_state(volatile int *s) {
	int a, b;

	do {
		a = *s;
		b = *s;
	} while (a != b);

	return a;
}

//инкремент
void atom_inc(volatile int *num)
{
	_asm
	{
		mov     esi, num
		lock    inc     DWORD PTR[esi]
	};
}

//декремент
void atom_dec(volatile int *num)
{
	_asm
	{               mov     esi, num
		lock    dec     DWORD PTR[esi]
	};
}

//обмен
int atom_xchg(volatile int *m, int val)
{
	_asm
	{
		mov     eax, val
		mov     esi, m
		lock    xchg    eax, DWORD PTR[esi]
		mov     val, eax
	}
	return val;
}

//добавление
void atom_add(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    add     DWORD PTR[esi], eax
	};
}

//вычитание
void atom_sub(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    sub     DWORD PTR[esi], eax
	};
}

//------------------------------------------------------------------------------


// определение сработал ли таймаут  с учетом природы типа RTKTime в РТОС32 (пока так, потом можно поискать более красивые варианты)
bool net_timeout_expired(RTKTime start, RTKTime stop, RTKTime time) {
	if (start >= 0) {
		if (stop > 0) {
			// start>0, stop>0
			if (time > stop)
				return true;
		}
		else {
			// start>0, stop<0
			if (time<0 && time>stop)
				return true;
		}
	}
	else {
		if (stop >= 0) {
			// start<0, stop>0
			if (time >= 0 && time > stop)
				return true;
		}
		else {
			// start<0, stop<0
			if (time > stop)
				return true;
		}

	}

	return false;
}




//----------------------------------------------------------
// global spinlock
//----------------------------------------------------------

const char* global_spinlock_name = "global_spinlock";
static RTKSpinlock global_spinlock;
static DWORD global_spinlock_IntState;

void global_spinlock_init() {
	int res = RTKCPUs();
	if (!res) res = 1;

	global_spinlock = RTKCreateSpinlock(res, global_spinlock_name);
}



void global_spinlock_lock() {
	global_spinlock_IntState = RTKLockSpinlock(global_spinlock);
}

void global_spinlock_unlock() {
	RTKReleaseSpinlock(global_spinlock, global_spinlock_IntState);
}


//----------------------------------------------------------






//----------------------------------------------------------
volatile init_flags_t init_flags;

void common_init() {

	init_flags.base_init = false;
	init_flags.settings_init = false;
	init_flags.net_init = false;
	init_flags.logic_init = false;

	global_spinlock_init();
}

//----------------------------------------------------------

bool allow_update;

void wdt_init() {
	allow_update = true;
#ifdef WDT_EN
	RTOut(0x20C, 0x01); // WDT On
#endif
}

inline void wdt_update() {
#ifdef WDT_EN
	if(allow_update)
		RTIn(0x20C);
#endif
}


void reboot() {
	allow_update = false;
#ifndef WDT_EN
	RTOut(0x20C, 0x01);
#endif
	while (true);
}




//--------- steady clock -------------
volatile int steady_clock = 0;

int steady_clock_get() {
	return atom_get_state(&steady_clock);
}

void steady_clock_update(int us) {
	atom_add(&steady_clock, us);
}


bool steady_clock_expired(int32_t start_time, int32_t stop_time, uint32_t timeout) {
	return ((uint32_t)stop_time - (uint32_t)start_time >= timeout);
}


bool steady_clock_expired_now(int32_t start_time, uint32_t timeout) {
	return steady_clock_expired(start_time, steady_clock_get(), timeout);
}


//------------------------------------


//получение счетчика тактов процессора (использовать только если одно ядро и нет динамического изменения частоты процессора, иначе надо делать по другому)
int __stdcall get_cpu_clks() {
	__asm {
		rdtsc
	}
}
