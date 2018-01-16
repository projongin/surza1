
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



#include "Rtk32.h"
#include <stdbool.h>


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
