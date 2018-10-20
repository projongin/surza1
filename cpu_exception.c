
#include "global_defines.h"

#ifdef CPU_EXCEPTION_DEBUG

#include <Windows.h>
#include <stdio.h>

#include "Rttarget.h"





DWORD _fastcall get_cr2(void * P)
{
	__asm
	{
		mov eax, cr2
	}
}




void cpu_exception_handler_install() {


	RTRaiseCPUException(14);

	__try {
		//*((int*)0x00) = 100;
		*((int*)0xa7) = 100;
		
		/*RaiseException(
		EXCEPTION_ACCESS_VIOLATION,                    // exception code
		0,                    // continuable exception
		0, NULL);             // no arguments
		*/
	}
	__except (((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION)
		? EXCEPTION_EXECUTE_HANDLER
		: EXCEPTION_CONTINUE_SEARCH)) {

		unsigned t = RTCallRing0(get_cr2, 0);

		printf_s("\nexception!!!   cr2: %u", t);

		while (1);


	}


}


#endif

