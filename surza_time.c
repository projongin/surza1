
#include "surza_time.h"

#include <Rtk32.h>
#include <Rttarget.h>
#include <Windows.h>
#include <stdint.h>

#include "common.h"
#include "log.h"


static unsigned isa_pps_adr;
static unsigned period;




#define PPS_DEFAULT_ADR  0x442

#define PPS_SIGNAL_CODE_PPS     0xA7
#define PPS_SIGNAL_CODE_NO_PPS  0x00


bool get_pps() { return (RTIn(isa_pps_adr) == PPS_SIGNAL_CODE_PPS); }


#define NSECS_BILLION   1000000000ull



int64_t time_convert_systemtime_to_time_t(const SYSTEMTIME* t) {

	struct tm  tm_time;

	tm_time.tm_sec = t->wSecond;
	tm_time.tm_min = t->wMinute;
	tm_time.tm_hour = t->wHour;
	tm_time.tm_mday = t->wDay;
	tm_time.tm_mon = t->wMonth - 1;
	tm_time.tm_year = t->wYear - 1900;
	tm_time.tm_isdst = -1;

	return (int64_t) mktime(&tm_time);
}


int64_t time_convert_filetime_to_time_t(const FILETIME* t) {
	ULARGE_INTEGER ull;
	ull.LowPart = t->dwLowDateTime;
	ull.HighPart = t->dwHighDateTime;
	return ull.QuadPart / 10000000ULL - 11644473600ULL;
}


void time_convert_filetime_to_secs_nsecs(const FILETIME* t, int64_t* secs, uint32_t* nsecs) {
	ULARGE_INTEGER ull;
	ull.LowPart = t->dwLowDateTime;
	ull.HighPart = t->dwHighDateTime;
	*secs = (int64_t)(ull.QuadPart / 10000000ULL - 11644473600ULL);
	*nsecs = (int32_t)(ull.QuadPart%NSECS_BILLION);
}




surza_time_t  time_get() {
	surza_time_t time;

	time.secs = 0;
	time.nsecs = 0;

	return time;
}





//переменные для работы системы времени
static bool hardware_pps_en;       // наличие платы ФИУ с приемником PPS
static surza_time_t current_time;  // текущее время сурзы
static bool pps_en;                // текущий режим работы (есть pps / пропал pps)
//--------------------------------------


void time_init() {

	//получение адреса PPS и периода
	isa_pps_adr = PPS_DEFAULT_ADR;
	period = 1;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (node) {
		param_tree_node_t* item;

		item = ParamTree_Find(node, "PERIOD", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value)
			sscanf_s(item->value, "%u", &period);


		item = ParamTree_Find(node, "PPS_ADR", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value)
			sscanf_s(item->value, "%u", &isa_pps_adr);	

	}
		


	//проверка наличия ФИУ с PPS и установка соответствующего флага
	uint8_t u8 = RTIn(isa_pps_adr);
	hardware_pps_en = (u8 == PPS_SIGNAL_CODE_PPS || u8 == PPS_SIGNAL_CODE_NO_PPS) ? true : false;

	//инициализация начального времени
	current_time.steady_nsecs = 0;

	FILETIME t;
	GetSystemTimeAsFileTime(&t);
	time_convert_filetime_to_secs_nsecs(&t, &current_time.secs, &current_time.nsecs);

	//сначала режим работы без pps в любом случае
	pps_en = false;

}


//обновление внутренней временной метки
void time_isr_update() {


	/**********************/
	//ВРЕМЕННО!!!!  пока не переделал все на новое время   
	steady_clock_update(period);
	/**********************/


	/**********************************/
	static unsigned cnt = 0;

	if (get_pps()) {
		;
	//	LOG_AND_SCREEN("new sec!  cnt=%u", cnt);
	}

	cnt++;
	/***********************************/




	//инкремент surza_time_t.steady_nsecs на текущий период
	current_time.steady_nsecs += period;


    //если есть ФИУ с PPS
	if(hardware_pps_en)	   //если режим без  PPS, то проверка на появление сигнала PPS, если появился, то переключение на режим работы по PPS, сохраненная секунда приравнивается текущей системной
		if (!pps_en) {
			;
		}



    //если есть ФИУ с PPS

	    //если режим с PPS

	        //если с последней секунды прошло полсекунды - сохраняю текущую системную секунду

	        //считывание PPS, если уже не был считан ранее (при переходе из работы без PPS  в режим с PPS)

		        //если нет PPS и его нет уже 1.1 секунды, то поднимаю флаг отсутствия PPS и перехожу в режим работы  без PPS (по системным часам)

	            //если нет PPS, то функция инкремента наносекунд

  	            //если есть PPS, то зануление surza_time_t.nsec,  surza_time_t.nsec равна сохраненной системной метке+1



	// если режим без PPS

	     //считывание системного времени

	         //если новая секунда, то зануление surza_time_t.nsec,  surza_time_t.nsec считанной системной метке

	         //если нет новой секунды, то функция инкремента наносекунд





	//-----------------------------------------------------------------------------


	//функция инкремента наносекунд
	     
	       //инкремент surza_time_t.nsec на период

           //если уже более 999999999 наносекунд - surza_time_t.nsec принудительно приравнивается 999999999



}



//получение новых сообщений с метками времени
void time_net_callback(const void* data, int length) {

	//сохранение текущего интервала между сообщениями


	//проверка, что сообщение пришло без особых задержек (если оно не сильно отличается от среднего интервала между сообщениями)


	//корректировка системных часов
	  // если режим без PPS, то допуск ухода маленький (например 5ms)
	  // если ФИУ с PPS и сейчас режим с PPS - то корректировку нет смысла проводить при разбежке до 100-200 миллисекунд

	

	//обновление cmos времени раз в час



}


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//  переделать на новое единое время, так как оно уже включает в себя steady_clock



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






//------- time ---------------------

//добавить ко времени time  nsecs наносекунд
surza_time_t SurzaTime_add(surza_time_t time, uint64_t nsecs) {
	surza_time_t new_time = time;

	new_time.secs += (nsecs / NSECS_BILLION);
	new_time.nsecs += (nsecs % NSECS_BILLION);
	if (new_time.nsecs >= NSECS_BILLION) {
		new_time.nsecs -= NSECS_BILLION;
		new_time.secs += 1;
	}

	return new_time;
}

//отнять от времени time  nsecs наносекунд
surza_time_t SurzaTime_sub(surza_time_t time, uint64_t nsecs) {
	surza_time_t new_time;

	new_time.steady_nsecs = (time.steady_nsecs > nsecs) ? time.steady_nsecs - nsecs : 0;

	new_time.secs = time.secs;
	new_time.nsecs = time.nsecs;

	uint64_t tmp_secs = nsecs / NSECS_BILLION;
	uint32_t tmp_nsecs = nsecs % NSECS_BILLION;

	new_time.secs -= (int64_t)tmp_secs;

	if (tmp_nsecs > new_time.nsecs) {
		new_time.secs -= 1;
		new_time.nsecs += NSECS_BILLION;
		new_time.nsecs -= tmp_nsecs;
	}
	else
		new_time.nsecs -= tmp_nsecs;

	return new_time;
}
//----------------------------------





//получение счетчика тактов процессора (использовать только если одно ядро и нет динамического изменения частоты процессора, иначе надо делать по другому)
int __stdcall get_cpu_clks() {
	__asm {
		rdtsc
	}
}

