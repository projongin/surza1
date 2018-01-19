
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


#define WATCHDOG_UPDATE  ;


//------------------
void tmp_net_callback(net_msg_t* msg, uint64_t channel) {
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
	RTKConfig.DefaultTaskStackSize = 1024*1024;
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
	
	

	//==============================================

	
	//==============================================
	// инициализация сетевого обмена, буферов и т.п.
	//==============================================
	LOG_AND_SCREEN("Surza start");
	bool init_ok;
	do {

		init_ok = buf_pool_init();
		if (!init_ok) {
			LOG_AND_SCREEN("buf_pool_init()  fail!");
			break;
		}

		
		if (NET_ERR_NO_ERROR != net_init(tmp_net_callback, tmp_net_realtime_callback)) {
			LOG_AND_SCREEN("net_init()  fail!");
			init_ok = false;
			break;
		}
		

	} while (0);



	if (!init_ok) {
		//обновляем собаку несколько секунд, чтоб успеть прочитать сообщения на экране
		for (int i = 0; i < 10; i++) {
			WATCHDOG_UPDATE
			RTKDelay(CLKMilliSecsToTicks(10));
		}
		while (true) { RTKDelay(1000); }
	}  else {
		LOG_AND_SCREEN("init()  ok");
	}



	//запуск в работу матядра и сетевого обмена

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

	//---------------------

	while (true) {
		WATCHDOG_UPDATE

			//============================================================================================================
			// основной цикл работы
			//============================================================================================================

			net_update();





		    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		     time = RTKGetTime();
			 if (CLKTicksToSeconds(time) != CLKTicksToSeconds(time_prev)) {
				 time_prev = time;

				 printf("buf_pool available buf = %d\n", buf_pool_bufs_available(0));
			 }

			//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


			// RTKDelay(CLKMilliSecsToTicks(50));

		//RTKDelay(CLKMilliSecsToTicks(10));



	 //============================================================================================================
	}


	return(0);
}
