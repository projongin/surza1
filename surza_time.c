
#include "surza_time.h"

#include <Rtk32.h>
#include <Rttarget.h>
#include <Windows.h>
#include <stdint.h>

#include "common.h"
#include "log.h"


static unsigned isa_pps_adr;
static unsigned period_us;
static unsigned period_ns;



#define NSECS_BILLION   1000000000ull


// ДОПУСКИ УХОДА ЧАСОВ  (в наносекундах)

//если режим без PPS, то допуск ухода маленький(например 5ms)
#define SYNC_NO_PPS_MAX_DEVIATION   10000000

//если ФИУ с PPS и сейчас режим с PPS - то корректировку нет смысла проводить при разбежке до 100 - 200 миллисекунд (в таком режиме секунда будет отсчитываться от сигнала PPS с точностью до такта сурзы)
#define SYNC_PPS_MAX_DEVIATION      200000000

//количество наносекунд со времени последней выполненной синхронизации через которое можно выполнить принудительную синхронизацию (только в режиме без PPS)
#define SYNC_FORCED_NSECS           (NSECS_BILLION*1800)

//минимальная разбежка для выполнения принудительной синхронизации
#define SYNC_FORCED_DEVIATION       1500000


#define PPS_DEFAULT_ADR  0x442

#define PPS_SIGNAL_CODE_PPS     0xA7
#define PPS_SIGNAL_CODE_NO_PPS  0x00


unsigned char get_pps() { return (RTIn(isa_pps_adr) == PPS_SIGNAL_CODE_PPS); }





#if 0
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
#endif


void time_convert_filetime_to_secs_nsecs(const FILETIME* t, int64_t* secs, uint32_t* nsecs) {
	ULARGE_INTEGER ull;
	ull.LowPart = t->dwLowDateTime;
	ull.HighPart = t->dwHighDateTime;
	*secs = (int64_t)(ull.QuadPart / 10000000ULL - 11644473600ULL);
	*nsecs = (int32_t)((ull.QuadPart%10000000ULL)*100);
}

#if 0
void time_convert_secs_nsecs_to_filetime(int64_t secs, uint32_t nsecs, FILETIME* t) {
	ULARGE_INTEGER ull;
	ull.QuadPart = 11644473600ULL + secs*10000000ULL;
	ull.QuadPart += (nsecs / 100);
	t->dwLowDateTime = ull.LowPart;
	t->dwHighDateTime = ull.HighPart;
}



int64_t get_system_second() {
	FILETIME t;
	GetSystemTimeAsFileTime(&t);
	ULARGE_INTEGER ull;
	ull.LowPart = t.dwLowDateTime;
	ull.HighPart = t.dwHighDateTime;
	return (int64_t)(ull.QuadPart / 10000000ULL - 11644473600ULL);
}


void set_system_time(const surza_time_t* sync_time) {

	FILETIME f_time;
	time_convert_secs_nsecs_to_filetime(sync_time->secs, sync_time->nsecs, &f_time);

	SYSTEMTIME sys_time;
	FileTimeToSystemTime(&f_time, &sys_time);

	SetSystemTime(&sys_time);

	/*
	!!!!!!   переделать основное время !!!
    не использовать системное время.   корректировать сразу current_time по получаемой метке от шлюза
	преобразование от системного времени понадобится только один раз  при инициализации
		*/

	/**************/
	surza_time_t eee = time_get();
	LOG_AND_SCREEN("%lld l_sec:%lld l_nsec:%lu s_sec:%lld s_nsec:%lu", get_system_second(), eee.secs, eee.nsecs, sync_time->secs, sync_time->nsecs);
	/***************/
}
#endif




//переменные для работы системы времени
static volatile bool hardware_pps_en;       // наличие платы ФИУ с приемником PPS
static volatile surza_time_t current_time;  // текущее время сурзы
static volatile bool pps_en;                // текущий режим работы (есть pps / пропал pps)
//static volatile int64_t saved_second;       // сохраненная секунда
static volatile bool sync_once;             // время было синхронизировано хотябы один раз
static volatile bool cmos_update;           // флаг необходимости обновить время в CMOS
static volatile int64_t sync_secs;          // время для синхронизации
static volatile uint32_t sync_nsecs;        // время для синхронизации
static volatile bool sync_time_update;      // флаг необходимости синхронизировать время 
//--------------------------------------


surza_time_t  time_get() {

	global_spinlock_lock();

	surza_time_t time = current_time;

	global_spinlock_unlock();

	if (time.nsecs > NSECS_BILLION)
		time.nsecs = NSECS_BILLION - 1;

	return time;
}


void time_init() {

	//получение адреса PPS и периода
	isa_pps_adr = PPS_DEFAULT_ADR;
	period_us = 1;
	period_ns = 1000;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (node) {
		param_tree_node_t* item;

		item = ParamTree_Find(node, "PERIOD", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value)
			sscanf_s(item->value, "%u", &period_us);
		period_ns = period_us*1000;


		hardware_pps_en = false;
		item = ParamTree_Find(node, "PPS_ADR", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			sscanf_s(item->value, "%u", &isa_pps_adr);
			hardware_pps_en = true;
		}

	}
		


	//проверка наличия ФИУ с PPS и установка соответствующего флага
	if (hardware_pps_en) {
		uint8_t u8 = RTIn(isa_pps_adr);
		hardware_pps_en = (u8 == PPS_SIGNAL_CODE_PPS || u8 == PPS_SIGNAL_CODE_NO_PPS) ? true : false;
	}

	//инициализация начального времени
	current_time.steady_nsecs = 0;

	SYSTEMTIME st;
	FILETIME ft;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	surza_time_t non_volatile_t = current_time;
	time_convert_filetime_to_secs_nsecs(&ft, &non_volatile_t.secs, &non_volatile_t.nsecs);

	//сначала режим работы без pps в любом случае
	pps_en = false;

	//время еще ни разу не синхронизировалось со шлюзом
	sync_once = false;

	cmos_update = false;

	sync_time_update = false;
}



/*************/
bool upd_f = false;
surza_time_t ttt;

uint64_t ddd_dif;
bool ddd_dif_new = false;
bool ddd_dif_update = false;
bool ddd_pps = false;

bool ddd_new_msg = false;
uint64_t ddd_avg;
uint64_t ddd_msg_time;

surza_time_t ddd_local, ddd_sync;

unsigned ddd_pps_period = 0;

bool pps_block = false;

void time_net_callback_(const void* data, int length) {
	memcpy(&ttt, data, sizeof(surza_time_t));
	upd_f = true;
}
/**************/


//обновление внутренней временной метки
void time_isr_update() {

	//обновление отдельного steady clock для счетчиков
	steady_clock_update(period_us);

	//синхронизация времени, если необходимо
	if (sync_time_update) {
		sync_time_update = false;
		current_time.secs = sync_secs;
		if(!pps_en)  //нет коррекции наносекунд при рабочем pps -  с ним могут отличаться только секунды, а наносекунды идут точнее чем при синхронизации от шлюза
			current_time.nsecs = sync_nsecs;
	}


	bool pps_read = false;
	bool pps_state;

	static int64_t last_system_second = 0;

	//инкремент на текущий период
	current_time.steady_nsecs += period_ns;
	current_time.nsecs += period_ns;


	//если есть ФИУ с PPS
	if (hardware_pps_en) {

		if (!pps_en) {	   //если режим без  PPS, то проверка на появление сигнала PPS, если появился, то переключение на режим работы по PPS
			pps_state = get_pps();
			pps_read = true;
			/******************/
			/*
			if (pps_block)
				pps_state = false;
			*/
			/******************/
			if (pps_state)
				pps_en = true;
		}

		//если режим с PPS
		if (pps_en) {

			//считывание PPS, если уже не был считан ранее (при переходе из работы без PPS  в режим с PPS)
			if (!pps_read)
				pps_state = get_pps();


			/*************/
			/*
			if (pps_block) {
  			    pps_state = false;
			}
			*/
			/************/

			if (pps_state) {

				/************/
				ddd_pps_period = current_time.nsecs;
				ddd_pps = true;
				/************/

				current_time.nsecs = 0;
				current_time.secs++;

			} else if(current_time.nsecs > NSECS_BILLION + SYNC_NO_PPS_MAX_DEVIATION) {

				//если нет PPS и его нет уже 1 секунда + SYNC_NO_PPS_MAX_DEVIATION , то поднимаю флаг отсутствия PPS и перехожу в режим работы  без PPS
				pps_en = false;

			}

		}

	}

	//коррекция в режиме без PPS
	if (!pps_en) {
		if (current_time.nsecs >= NSECS_BILLION) {
			current_time.nsecs -= NSECS_BILLION;
			current_time.secs++;
		}
	}


}


//--------------------------------------------------------------

//история времен между получениями сообщений о синхронизации (для подсчета среднего времени)
#define SYNC_STAT_HYSTORY  10

//максимальное отклонение от среднего значения между поступлением сообщений синхронизации при котором сообщение будет считаться допустимым для синхронизации (в наносекундах)
// логично сделать его менее SYNC_NO_PPS_MAX_DEVIATION
#define SYNC_TIME_MAX_DEVIATION  5000000


static uint64_t msg_time_stat[SYNC_STAT_HYSTORY];
static unsigned msg_time_stat_head = 0;
static unsigned msg_time_stat_num = 0;
static uint64_t msg_time_sum = 0;

static uint64_t last_time_sync;

static uint64_t nsecs_since_sync = 0;


//получение новых сообщений с метками времени
void time_net_callback(const void* data, int length) {

	if (length < sizeof(surza_time_t))
		return;

	uint64_t msg_time = 0;

	surza_time_t local_time = time_get();

	//сохранение текущего интервала между сообщениями
	if (!sync_once) {
		last_time_sync = local_time.steady_nsecs;
	}
	else {
		msg_time = local_time.steady_nsecs - last_time_sync;
		last_time_sync = local_time.steady_nsecs;

		if (msg_time_stat_num >= SYNC_STAT_HYSTORY)
			msg_time_sum -= msg_time_stat[msg_time_stat_head];
		else
			msg_time_stat_num++;

		msg_time_stat[msg_time_stat_head] = msg_time;
		msg_time_sum += msg_time;

		msg_time_stat_head++;
		if (msg_time_stat_head >= SYNC_STAT_HYSTORY)
			msg_time_stat_head = 0;

	}

	//проверка, что сообщение пришло без особых задержек (оно не сильно отличается от среднего интервала между сообщениями)
	if (sync_once) {

		if (msg_time_stat_num != SYNC_STAT_HYSTORY)  //не обновлять пока не накопится история длительностей между приходом синхронизирующих сообщений
			return;
		else {

			uint64_t average_time = msg_time_sum / SYNC_STAT_HYSTORY;

			/***************/
			ddd_new_msg = true;
			ddd_avg = average_time;
			ddd_msg_time = msg_time;
			/******************/

			if ((average_time > msg_time && (average_time - msg_time > SYNC_TIME_MAX_DEVIATION))
				|| (average_time < msg_time && (msg_time - average_time > SYNC_TIME_MAX_DEVIATION)))
				return;
		}
	}
	
	

	//подсчет необходимости синхронизации времени сурзы
	surza_time_t* sync_time = (surza_time_t*)data;
	uint64_t max_deviation = (pps_en) ? SYNC_NO_PPS_MAX_DEVIATION : SYNC_NO_PPS_MAX_DEVIATION;
	bool sync_flag = false;

	/**********/
	uint64_t ddd_l = local_time.secs * NSECS_BILLION + local_time.nsecs;
	uint64_t ddd_s = sync_time->secs * NSECS_BILLION + sync_time->nsecs;
	ddd_dif = (ddd_l > ddd_s) ? ddd_l - ddd_s : ddd_s - ddd_l;
	ddd_dif_new = true;
	/**********/

	if (local_time.secs != sync_time->secs) {
		uint64_t nsecs;

		//секунды отличаются
		if (local_time.secs > sync_time->secs) {
			nsecs = (local_time.secs - sync_time->secs)*NSECS_BILLION;
			nsecs += local_time.nsecs;
			nsecs -= sync_time->nsecs;
		} else {
			nsecs = (sync_time->secs - local_time.secs)*NSECS_BILLION;
			nsecs += sync_time->nsecs;
			nsecs -= local_time.nsecs;
		}

		if(nsecs>max_deviation)
			sync_flag = true;

	} else {
		uint32_t deviation = (local_time.nsecs > sync_time->nsecs) ? local_time.nsecs - sync_time->nsecs : sync_time->nsecs - local_time.nsecs;

		//если разбежка вышла за допустимый диапазон или с последней синхронизации прошло достаточно времени для выполнения принудительной синхронизации
		if (deviation > max_deviation 
			|| (local_time.steady_nsecs-nsecs_since_sync>SYNC_FORCED_NSECS && deviation>SYNC_FORCED_DEVIATION && !pps_en))
			sync_flag = true;
	}

	
	if (sync_flag) {
		unsigned ns = (sync_time->nsecs / period_ns + 1) * period_ns;
		global_spinlock_lock();
 		 sync_secs = sync_time->secs;
		 sync_nsecs = ns;
		 sync_time_update = true;
		global_spinlock_unlock();
		nsecs_since_sync = local_time.steady_nsecs;
	}
	
	/*************/
	if (sync_flag) {
		ddd_dif_update = true;
		ddd_local = local_time;
		ddd_sync = *sync_time;
	}
	/**************/

	//обновление cmos времени раз в час
	static int64_t last_cmos_sync = 0;
	if (!sync_once
		|| sync_time->secs-last_cmos_sync>3600) {

		last_cmos_sync = sync_time->secs;
		cmos_update = true;
	}


	sync_once = true;

}


void time_cmos_update() {
	if (cmos_update) {
		cmos_update = false;
		RTCMOSSetRTC();
	}


	/*************************/
	if (upd_f) {
		upd_f = false;
		time_net_callback_(&ttt, sizeof(surza_time_t));
	}
	/**************************/
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

