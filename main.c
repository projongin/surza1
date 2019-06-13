
#include <Clock.h>

#include <time.h>
#include <limits.h>
#include <stdint.h>

#include <Rtkflt.h>
#include <Rtk32.h>
#include <Rttarget.h>


#include <stdio.h>
#include <stdlib.h>


#include "buf_pool.h"
#include "net.h"
#include "log.h"
#include "common.h"
#include "logic.h"
#include "surza_time.h"

#ifdef DELTA_HMI_ENABLE
#include "delta_hmi.h"
#endif



int RTKAPI keyboard_irq_handler(void* P) {

//	RTKIRQEnd(1);
	return 0;
}


DWORD _fastcall disable_smi(void *p)
{
	_asm
	{
		push      eax
		push      ecx
		push      edx

		mov       ecx, 00001301h
		RDMSR
		and       al, 11011111b
		WRMSR

		pop       edx
		pop       ecx
		pop       eax
	}
}


#define WATCHDOG_UPDATE()  wdt_update()


//------------------
unsigned msg_cnt = 0;
unsigned msg_cnt_old = 0;
void tmp_net_callback(net_msg_t* msg, uint64_t channel) {
	msg_cnt++;
//	LOG_AND_SCREEN("MSG channel=%016llX   type=%u, subtype=%u,  data_size=%u bytes", channel, msg->type, msg->subtype, msg->size);
//	net_send_msg(msg, NET_PRIORITY_MEDIUM, channel);
}
//------------------


/**********************************/
#include <Rtip.h>
EPACKET dcus[10];
uint8_t dcu_data[10][1600];
volatile int dcus_cnt = 0;

unsigned char test_frame[0x3c] = { 0x90, 0x94, 0xe4, 0x82, 0x12, 0xe9,
                                   0x00, 0x08, 0xb3, 0x01, 0xa1, 0x47,
                                   0x11, 0x22,
                                   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                                   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d
                                 };


unsigned NewSurzaFrame(const void* data_in, unsigned bytes, void* data_out) {

	if (bytes) {

		*(uint8_t*)data_out = *(uint8_t*)data_in + 1;


		dcus_cnt++;

		return 1;
	}

	return 0;
}


/**********************************/




int  main(int argc, char * argv[])
{


#if _MSC_VER >= 1400               // Keep MSC 8.0 run-time library happy
	RTT_Init_MSC8CRT();
#endif

	// конфигураци€ RtKernel

	RTKConfig.DefaultIntStackSize = 1024 * 16;
	RTKConfig.DefaultTaskStackSize = 1024 * 1024;
	RTKConfig.Flags |= RF_PREEMPTIVE;  //включаем вытесн€ющую многозадачность
	RTKernelInit(0);
	//-------------------------------------------------
	

	CLKSetTimerIntVal(2000);          // 2 millisecond tick
	RTKDelay(1);

	RTCallRing0(disable_smi, 0);

	RTKDisableIRQ(1);

	
#ifdef CPU_EXCEPTION_14_DEBUG
	cpu_exception_init();
#endif

	DEBUG_ADD_POINT(1);

	//==============================================
	// инициализаци€ общего системного
	//==============================================
	srand(time(NULL) % UINT_MAX);
	
	
	common_init();  //инициализаци€ базовых общих переменных
	//==============================================


	//========================================================================================
	// инициализаци€ вспомогательного функционала (буферов, номера запуска и т.п.
	//========================================================================================
	LOG_AND_SCREEN("Surza start");	

	DEBUG_ADD_POINT(2);

	bool init_ok;
	do {

		init_ok = buf_pool_init();
		if (!init_ok) {
			LOG_AND_SCREEN("buf_pool_init()  fail!");
			break;
		}

		

	} while (0);

	init_flags.base_init = init_ok;


	//========================================================================================
	// чтение файлов настроек
	//========================================================================================
	DEBUG_ADD_POINT(3);

	if (init_flags.base_init) {
		init_flags.settings_init = (read_settings() >= 0) ? true : false;
		if (init_flags.settings_init) {
			LOG_AND_SCREEN("Read settings file OK");
		}
		else {
			LOG_AND_SCREEN("Settings file ERROR!");
		}
	}

	//========================================================================================
	// инициализаци€ сети
	//========================================================================================
	DEBUG_ADD_POINT(4);
	if (init_flags.base_init) {
		if (NET_ERR_NO_ERROR != net_init(tmp_net_callback, time_net_callback)) {
			LOG_AND_SCREEN("net_init()  fail!");
		}
		else {
			LOG_AND_SCREEN("net_init()  OK");
			init_flags.net_init = true;
		}
	}


	//========================================================================================
	// инициализаци€ логики, в том числе периферии (ADC, DIC, FIU)
	//========================================================================================
	DEBUG_ADD_POINT(5);
	if (init_flags.base_init && init_flags.net_init) {
		if (logic_init() < 0) {
			LOG_AND_SCREEN("Logic init fail!");
		} else {
			LOG_AND_SCREEN("Logic init OK");
			init_flags.logic_init = true;
		}
	}



#if 0
	if (!init_ok) {
		//обновл€ем собаку несколько секунд, чтоб успеть прочитать сообщени€ на экране
		for (int i = 0; i < 10; i++) {
			WATCHDOG_UPDATE();
			RTKDelay(CLKMilliSecsToTicks(10));
		}
		while (true) { RTKDelay(1000); }
	}  else {
		LOG_AND_SCREEN("init()  OK");
	}
#endif

	DEBUG_ADD_POINT(6);

	//собака
	wdt_init();


	//запуск в работу мат€дра и сетевого обмена
	if(init_flags.net_init)
		net_start();

	
	DEBUG_ADD_POINT(7);

	//---------------------------------
	//тесты модулей
	//printf("\n buf_pool_test() = %d\n", buf_pool_test());
	
	
	//extern net_main_queue_test();
	//net_main_queue_test();

	//---------------------------------
	
	RTKTime time, time_prev;
	time = RTKGetTime();
	time_prev = time;

	
	while (true) {
		WATCHDOG_UPDATE();

		    time_cmos_update();

			//============================================================================================================
			// основной цикл работы
			//============================================================================================================
	
		    DEBUG_ADD_POINT(10);
			net_update();

	
			DEBUG_ADD_POINT(12);
			indi_send();

	
			DEBUG_ADD_POINT(14);
			journal_update();


			DEBUG_ADD_POINT(15);
			oscilloscope_update();

	
            #ifdef DELTA_HMI_ENABLE
			DEBUG_ADD_POINT(16);
			delta_hmi_update();
            #endif



			/*****************************/
			time = RTKGetTime();
			if (CLKTicksToSeconds(time) != CLKTicksToSeconds(time_prev)) {
				time_prev = time;

				printf("dcus_cnt = %d\n", dcus_cnt);

			}

			/********************************/


#if 0
		   extern volatile int net_test_heap_bufs;
		   extern void net_test_queues(int* indi);
		   int indi[20];

		    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		     time = RTKGetTime();
			 if (CLKTicksToSeconds(time) != CLKTicksToSeconds(time_prev)) {
				 time_prev = time;

				 printf("buf = %d, n_con=%u, msg/sec=%u, heap_bufs=%d\n", buf_pool_bufs_available(0), net_connections(), msg_cnt-msg_cnt_old, atom_get_state(&net_test_heap_bufs));

				 msg_cnt_old = msg_cnt;

				 net_test_queues(indi);

				 printf("R%d S%u", indi[0], indi[1]);
				 int base = 2;
				 for (int i = 0; i < 5; i++, base+=2) {
					 printf("  r%ds%d", indi[base], indi[base+1]);
				 }
				 printf("\n");

			 }

			//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


		 //RTKDelay(CLKMilliSecsToTicks(50));
#endif


	 //============================================================================================================
	}


	return(0);
}


