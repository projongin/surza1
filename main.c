
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
unsigned msg_cnt = 0;
unsigned msg_cnt_old = 0;
void tmp_net_callback(net_msg_t* msg, uint64_t channel) {
	msg_cnt++;
	net_send_msg(msg, NET_PRIORITY_MEDIUM, channel);
}

void tmp_net_realtime_callback(const void* data, int length) {}

//------------------



int  main(int argc, char * argv[])
{


#if _MSC_VER >= 1400               // Keep MSC 8.0 run-time library happy
	RTT_Init_MSC8CRT();
#endif

	// ������������ RtKernel
	RTKConfig.DefaultIntStackSize = 1024 * 16;
	RTKConfig.DefaultTaskStackSize = 1024*1024;
	RTKConfig.Flags |= RF_PREEMPTIVE;  //�������� ������������ ���������������
	RTKernelInit(0);
	//-------------------------------------------------


	CLKSetTimerIntVal(2000);          // 2 millisecond tick
	RTKDelay(1);

	RTCallRing0(disable_smi, 0);

	RTKDisableIRQ(1);



	//==============================================
	// ������������� ������ ����������
	//==============================================
	srand(time(NULL) % UINT_MAX);
	
	

	//==============================================

	
	//==============================================
	// ������������� �������� ������, ������� � �.�.
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
		//��������� ������ ��������� ������, ���� ������ ��������� ��������� �� ������
		for (int i = 0; i < 10; i++) {
			WATCHDOG_UPDATE
			RTKDelay(CLKMilliSecsToTicks(10));
		}
		while (true) { RTKDelay(1000); }
	}  else {
		LOG_AND_SCREEN("init()  ok");
	}



	//������ � ������ ������� � �������� ������

	net_start();


	//---------------------------------
	//����� �������
	//printf("\n buf_pool_test() = %d\n", buf_pool_test());
	
	
	//extern net_main_queue_test();
	//net_main_queue_test();

	//---------------------------------
	
	RTKTime time, time_prev;
	time = RTKGetTime();
	time_prev = time;

	
	while (true) {
		WATCHDOG_UPDATE

			//============================================================================================================
			// �������� ���� ������
			//============================================================================================================

			net_update();



		   extern volatile int net_test_heap_bufs;


		    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		     time = RTKGetTime();
			 if (CLKTicksToSeconds(time) != CLKTicksToSeconds(time_prev)) {
				 time_prev = time;

				 printf("buf = %d, n_con=%u, msg/sec=%u, heap_bufs=%d\n", buf_pool_bufs_available(0), net_connections(), msg_cnt-msg_cnt_old, atom_get_state(&net_test_heap_bufs));

				 msg_cnt_old = msg_cnt;

			 }

			//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


		 RTKDelay(CLKMilliSecsToTicks(50));


	 //============================================================================================================
	}


	return(0);
}
