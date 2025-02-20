
#include "surza_time.h"

#include <Rtk32.h>
#include <Rttarget.h>
#include <Windows.h>
#include <stdint.h>

#include "global_defines.h"
#include "common.h"
#include "log.h"


static bool surza_time_en = false;

static unsigned isa_pps_adr;
static unsigned period_us;
static unsigned period_ns;



#define NSECS_BILLION   1000000000ull


// ������� ����� �����  (� ������������)

//���� ����� ��� PPS, �� ������ ����� ���������(�������� 5ms)
#define SYNC_NO_PPS_MAX_DEVIATION   10000000

//���� ��� � PPS � ������ ����� � PPS - �� ������������� ��� ������ ��������� ��� �������� �� 100 - 200 ����������� (� ����� ������ ������� ����� ������������� �� ������� PPS � ��������� �� ����� �����)
#define SYNC_PPS_MAX_DEVIATION      200000000

//���������� ���������� �� ������� ��������� ����������� ������������� ����� ������� ����� ��������� �������������� ������������� (������ � ������ ��� PPS)
#define SYNC_FORCED_NSECS           (NSECS_BILLION*1800)

//����������� �������� ��� ���������� �������������� �������������
#define SYNC_FORCED_DEVIATION       1500000


#define PPS_DEFAULT_ADR  0x442

#define PPS_SIGNAL_CODE_PPS     0xA7
#define PPS_SIGNAL_CODE_NO_PPS  0x00


unsigned char get_pps() { return (RTIn(isa_pps_adr) == PPS_SIGNAL_CODE_PPS); }





void time_convert_filetime_to_secs_nsecs(const FILETIME* t, int64_t* secs, uint32_t* nsecs) {
	ULARGE_INTEGER ull;
	ull.LowPart = t->dwLowDateTime;
	ull.HighPart = t->dwHighDateTime;
	*secs = (int64_t)(ull.QuadPart / 10000000ULL - 11644473600ULL);
	*nsecs = (int32_t)((ull.QuadPart%10000000ULL)*100);
}


//���������� ��� ������ ������� �������
static volatile bool hardware_pps_en;       // ������� ����� ��� � ���������� PPS
static volatile surza_time_t current_time;  // ������� ����� �����
static volatile bool pps_en;                // ������� ����� ������ (���� pps / ������ pps)
static volatile bool sync_once;             // ����� ���� ���������������� ������ ���� ���
static volatile bool cmos_update;           // ���� ������������� �������� ����� � CMOS
static volatile int64_t sync_secs;          // ����� ��� �������������
static volatile uint32_t sync_nsecs;        // ����� ��� �������������
static volatile bool sync_time_update;      // ���� ������������� ���������������� ����� 
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

	//��������� ������ PPS � �������
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
		


	//�������� ������� ��� � PPS � ��������� ���������������� �����
	if (hardware_pps_en) {
		uint8_t u8 = RTIn(isa_pps_adr);
		hardware_pps_en = (u8 == PPS_SIGNAL_CODE_PPS || u8 == PPS_SIGNAL_CODE_NO_PPS) ? true : false;
	}

	//������������� ���������� �������
	current_time.steady_nsecs = 0;

	SYSTEMTIME st;
	FILETIME ft;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	surza_time_t non_volatile_t = current_time;
	time_convert_filetime_to_secs_nsecs(&ft, &non_volatile_t.secs, &non_volatile_t.nsecs);

	//������� ����� ������ ��� pps � ����� ������
	pps_en = false;

	//����� ��� �� ���� �� ������������������ �� ������
	sync_once = false;

	cmos_update = false;

	sync_time_update = false;

	surza_time_en = true;
}



//���������� ���������� ��������� �����
void time_isr_update() {

	if (!surza_time_en)
		return;

	//���������� ���������� steady clock ��� ���������
	steady_clock_update(period_us);

	//������������� �������, ���� ����������
	if (sync_time_update) {
		sync_time_update = false;
		current_time.secs = sync_secs;
		if(!pps_en)  //��� ��������� ���������� ��� ������� pps -  � ��� ����� ���������� ������ �������, � ����������� ���� ������ ��� ��� ������������� �� �����
			current_time.nsecs = sync_nsecs;
	}


	bool pps_read = false;
	bool pps_state;

	static int64_t last_system_second = 0;

	//��������� �� ������� ������
	current_time.steady_nsecs += period_ns;
	current_time.nsecs += period_ns;


	//���� ���� ��� � PPS
	if (hardware_pps_en) {

		if (!pps_en) {	   //���� ����� ���  PPS, �� �������� �� ��������� ������� PPS, ���� ��������, �� ������������ �� ����� ������ �� PPS
			pps_state = get_pps();
			pps_read = true;
			if (pps_state)
				pps_en = true;
		}

		//���� ����� � PPS
		if (pps_en) {

			//���������� PPS, ���� ��� �� ��� ������ ����� (��� �������� �� ������ ��� PPS  � ����� � PPS)
			if (!pps_read)
				pps_state = get_pps();

			if (pps_state) {

				current_time.nsecs = 0;
				current_time.secs++;

			} else if(current_time.nsecs > NSECS_BILLION + SYNC_NO_PPS_MAX_DEVIATION) {

				//���� ��� PPS � ��� ��� ��� 1 ������� + SYNC_NO_PPS_MAX_DEVIATION , �� �������� ���� ���������� PPS � �������� � ����� ������  ��� PPS
				pps_en = false;

			}

		}

	}

	//��������� � ������ ��� PPS
	if (!pps_en) {
		if (current_time.nsecs >= NSECS_BILLION) {
			current_time.nsecs -= NSECS_BILLION;
			current_time.secs++;
		}
	}


}


//--------------------------------------------------------------

//������� ������ ����� ����������� ��������� � ������������� (��� �������� �������� �������)
#define SYNC_STAT_HYSTORY  10

//������������ ���������� �� �������� �������� ����� ������������ ��������� ������������� ��� ������� ��������� ����� ��������� ���������� ��� ������������� (� ������������)
// ������� ������� ��� ����� SYNC_NO_PPS_MAX_DEVIATION
#define SYNC_TIME_MAX_DEVIATION  5000000


static uint64_t msg_time_stat[SYNC_STAT_HYSTORY];
static unsigned msg_time_stat_head = 0;
static unsigned msg_time_stat_num = 0;
static uint64_t msg_time_sum = 0;

static uint64_t last_time_sync;

static uint64_t nsecs_since_sync = 0;


//��������� ����� ��������� � ������� �������
void time_net_callback(const void* data, int length) {

	if (!surza_time_en)
		return;

	DEBUG_ADD_POINT(370);

	if (length < sizeof(surza_time_t))
		return;

	uint64_t msg_time = 0;

	surza_time_t local_time = time_get();

	//���������� �������� ��������� ����� �����������
	if (!sync_once) {
		last_time_sync = local_time.steady_nsecs;
		DEBUG_ADD_POINT(371);
	}
	else {

		DEBUG_ADD_POINT(372);

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


	//��������, ��� ��������� ������ ��� ������ �������� (��� �� ������ ���������� �� �������� ��������� ����� �����������)
	if (sync_once) {

		DEBUG_ADD_POINT(373);

		if (msg_time_stat_num != SYNC_STAT_HYSTORY)  //�� ��������� ���� �� ��������� ������� ������������� ����� �������� ���������������� ���������
			return;
		else {

			DEBUG_ADD_POINT(374);

			uint64_t average_time = msg_time_sum / SYNC_STAT_HYSTORY;

			if ((average_time > msg_time && (average_time - msg_time > SYNC_TIME_MAX_DEVIATION))
				|| (average_time < msg_time && (msg_time - average_time > SYNC_TIME_MAX_DEVIATION)))
				return;
		}
	}
	
	DEBUG_ADD_POINT(375);

	//������� ������������� ������������� ������� �����
	surza_time_t* sync_time = (surza_time_t*)data;
	uint64_t max_deviation = (pps_en) ? SYNC_NO_PPS_MAX_DEVIATION : SYNC_NO_PPS_MAX_DEVIATION;
	bool sync_flag = false;

	if (local_time.secs != sync_time->secs) {
		uint64_t nsecs;

		DEBUG_ADD_POINT(376);

		//������� ����������
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

		DEBUG_ADD_POINT(377);

		uint32_t deviation = (local_time.nsecs > sync_time->nsecs) ? local_time.nsecs - sync_time->nsecs : sync_time->nsecs - local_time.nsecs;

		//���� �������� ����� �� ���������� �������� ��� � ��������� ������������� ������ ���������� ������� ��� ���������� �������������� �������������
		if (deviation > max_deviation 
			|| (local_time.steady_nsecs-nsecs_since_sync>SYNC_FORCED_NSECS && deviation>SYNC_FORCED_DEVIATION && !pps_en))
			sync_flag = true;
	}

	DEBUG_ADD_POINT(378);

	if (sync_flag) {

		DEBUG_ADD_POINT(379);

		unsigned ns = (sync_time->nsecs / period_ns + 1) * period_ns;
		global_spinlock_lock();
 		 sync_secs = sync_time->secs;
		 sync_nsecs = ns;
		 sync_time_update = true;
		global_spinlock_unlock();
		nsecs_since_sync = local_time.steady_nsecs;
	}

	DEBUG_ADD_POINT(380);

	//���������� cmos ������� ��� � ���
	static int64_t last_cmos_sync = 0;
	if (!sync_once
		|| sync_time->secs-last_cmos_sync>3600) {

		last_cmos_sync = sync_time->secs;
		cmos_update = true;

		DEBUG_ADD_POINT(381);
	}


	sync_once = true;

	DEBUG_ADD_POINT(382);
}


void time_cmos_update() {
	if (cmos_update) {
		cmos_update = false;
		RTCMOSSetRTC();
	}
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

//�������� �� ������� time  nsecs ����������
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

//������ �� ������� time  nsecs ����������
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





//��������� �������� ������ ���������� (������������ ������ ���� ���� ���� � ��� ������������� ��������� ������� ����������, ����� ���� ������ �� �������)
int __stdcall get_cpu_clks() {
	__asm {
		rdtsc
	}
}


uint64_t __stdcall get_cpu_clks_64() {
	__asm rdtsc
}
