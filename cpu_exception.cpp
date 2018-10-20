
#include <stdio.h>
#include <string.h>

#include "Rttarget.h"
#include "Rtk32.h"

#include "common.h"

//#include <Windows.h>



#define CPU_DEBUG_POINTS_MAX   4096   //количество уникальных точек в программе
#define CPU_DEBUG_BUF_LENGTH   100    //длина записываемого буфера истории хождени€ по точкам


static volatile int buf_ptr = 0;


typedef struct{
	unsigned cs;
	unsigned adr;
} debug_point_t;

static debug_point_t debug_points[CPU_DEBUG_POINTS_MAX];

static volatile unsigned debug_buf[CPU_DEBUG_BUF_LENGTH];
static volatile unsigned debug_buf_ptr = 0;


static const char* debug_spinlock_name = "debug_spinlock";
static RTKSpinlock debug_spinlock;



void RTTAPI debug_handler(int ExitCode) {

	unsigned i = debug_buf_ptr;
	do {

		if(debug_buf[i])
			printf_s(" %u-%X:%X", debug_buf[i], debug_points[debug_buf[i]].cs, debug_points[debug_buf[i]].adr);

		i++;
		if (i == CPU_DEBUG_BUF_LENGTH)
			i = 0;

	} while (i != debug_buf_ptr);

	while (1) { wdt_update(); }  //wdt объ€влен до включени€ cpu_excep
}



volatile unsigned __stdcall get_segment() {
	__asm
	{
		mov eax, 0h
		mov ax, cs
	}
}

static volatile unsigned esp_save;

void __stdcall debug_add_point(unsigned point) {

	__asm
	{
		mov edx, [esp+12]     //esp+12 только если используетс€ компил€тором 'stack frame' (push ebp; mov ebp,esp), иначе esp+8
		lea ebx, esp_save
		mov [ebx], edx
	}
	
	if (!debug_points[point].adr) {  //первое добавление точки, заполн€ю информацию о ней
		debug_points[point].adr = esp_save;
		debug_points[point].cs = get_segment();
	}
	
	DWORD debug_spinlock_IntState = RTKLockSpinlock(debug_spinlock);

	debug_buf[debug_buf_ptr]=point;
	debug_buf_ptr++;
	if (debug_buf_ptr == CPU_DEBUG_BUF_LENGTH)
		debug_buf_ptr = 0;

	RTKReleaseSpinlock(debug_spinlock, debug_spinlock_IntState);

}



void cpu_exception_init() {

	memset((void*)debug_points, 0, sizeof(debug_points));
	memset((void*)debug_buf, 0, sizeof(debug_buf));
	debug_buf_ptr = 0;


	//spinlock init
	int res = RTKCPUs();
	if (!res) res = 1;
	debug_spinlock = RTKCreateSpinlock(res, debug_spinlock_name);


	//----------------------------------------------------------

	RTRaiseCPUException(14);
	RTSetExitHandler(debug_handler);


}
