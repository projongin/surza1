
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



int RTKAPI keyboard_irq_handler(void* P) {

	RTKIRQEnd(1);
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
	LOG_AND_SCREEN("MSG channel=%016llX   type=%u, subtype=%u,  data_size=%u bytes", channel, msg->type, msg->subtype, msg->size);
	net_send_msg(msg, NET_PRIORITY_MEDIUM, channel);
}

void tmp_net_realtime_callback(const void* data, int length) {}

//------------------



int  main(int argc, char * argv[])
{


#if _MSC_VER >= 1400               // Keep MSC 8.0 run-time library happy
	RTT_Init_MSC8CRT();
#endif

	// конфигурация RtKernel

	RTKConfig.DefaultIntStackSize = 1024 * 16;
	RTKConfig.DefaultTaskStackSize = 1024 * 1024;
	RTKConfig.Flags |= RF_PREEMPTIVE;  //включаем преемптивную многозадачность
	RTKernelInit(0);
	//-------------------------------------------------
	

	CLKSetTimerIntVal(2000);          // 2 millisecond tick
	RTKDelay(1);

	RTCallRing0(disable_smi, 0);

	RTKDisableIRQ(1);

	

	//==============================================
	// инициализация общего системного
	//==============================================
	srand(time(NULL) % UINT_MAX);
	
	
	common_init();  //инициализация базовых общих переменных
	//==============================================



	//========================================================================================
	// инициализация вспомогательного функционала (буферов, номера запуска и т.п.
	//========================================================================================
	LOG_AND_SCREEN("Surza start");	

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
	if (init_flags.base_init) {
		init_flags.settings_init = (read_settings() >= 0) ? true : false;
		if (init_flags.settings_init) {
			LOG_AND_SCREEN("Read settings file OK!");
		}
		else {
			LOG_AND_SCREEN("Settings file ERROR!");
		}
	}

	//========================================================================================
	// инициализация сети
	//========================================================================================
	if (init_flags.base_init) {
		if (NET_ERR_NO_ERROR != net_init(tmp_net_callback, tmp_net_realtime_callback)) {
			LOG_AND_SCREEN("net_init()  fail!");
		}
		else {
			LOG_AND_SCREEN("net_init()  ok!");
			init_flags.net_init = true;
		}
	}


	//========================================================================================
	// инициализация логики, в том числе периферии (ADC, DIC, FIU)
	//========================================================================================
	if (init_flags.base_init && init_flags.settings_init && init_flags.net_init) {
		if (logic_init() < 0) {
			LOG_AND_SCREEN("Logic init fail!");
		} else {
			LOG_AND_SCREEN("Logic init OK!");
			init_flags.logic_init = true;
		}
	}



#if 0
	if (!init_ok) {
		//обновляем собаку несколько секунд, чтоб успеть прочитать сообщения на экране
		for (int i = 0; i < 10; i++) {
			WATCHDOG_UPDATE();
			RTKDelay(CLKMilliSecsToTicks(10));
		}
		while (true) { RTKDelay(1000); }
	}  else {
		LOG_AND_SCREEN("init()  ok");
	}
#endif

	//собака
	wdt_init();


	//запуск в работу матядра и сетевого обмена
	if(init_flags.net_init)
		net_start();


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

			//============================================================================================================
			// основной цикл работы
			//============================================================================================================

			net_update();


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
