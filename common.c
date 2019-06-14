
#include "Rtk32.h"
#include <stdbool.h>
#include <stdint.h>

#include "global_defines.h"
#include "common.h"



//=============================================
// ��������� ��������
//=============================================

//��������� ������ int
void atom_set_state(volatile int *s, int val){
	_asm
	{
		mov     eax, val
		mov     esi, s
		lock    xchg    eax, DWORD PTR[esi]
	}
}

//������
int atom_get_state(volatile int *s) {
	int a, b;

	do {
		a = *s;
		b = *s;
	} while (a != b);

	return a;
}

//���������
void atom_inc(volatile int *num)
{
	_asm
	{
		mov     esi, num
		lock    inc     DWORD PTR[esi]
	};
}

//���������
void atom_dec(volatile int *num)
{
	_asm
	{               mov     esi, num
		lock    dec     DWORD PTR[esi]
	};
}

//�����
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

//����������
void atom_add(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    add     DWORD PTR[esi], eax
	};
}

//���������
void atom_sub(volatile int *num, int val)
{
	_asm
	{               mov     esi, num
		mov     eax, val
		lock    sub     DWORD PTR[esi], eax
	};
}

//------------------------------------------------------------------------------


// ����������� �������� �� �������  � ������ ������� ���� RTKTime � ����32 (���� ���, ����� ����� �������� ����� �������� ��������)
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

static volatile bool allow_update;

#ifdef CPC150
void wdt_init() {
	allow_update = true;
#ifdef WDT_EN
	RTOut(0x24f, 0x03); // WDT On
#endif
}

inline void wdt_update() {
#ifdef WDT_EN
	static bool edge = false;
	edge = edge ? false : true;

	if (allow_update)
		RTOut(0x24f, edge?0x01:0x03);
#endif
}


void reboot() {
	allow_update = false;
	RTOut(0x24f, 0x03); //������������� �������� ������
	while (true);
}
#else 
void wdt_init() {
	allow_update = true;
#ifdef WDT_EN
	RTOut(0x20C, 0x01); // WDT On
#endif
}

inline void wdt_update() {
#ifdef WDT_EN
	if (allow_update)
		RTIn(0x20C);
#endif
}


void reboot() {
	allow_update = false;
	RTOut(0x20C, 0x01); //������������� �������� ������
	while (true);
}
#endif

