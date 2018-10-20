
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#include <Rtk32.h>

#include "global_defines.h"

#include "logic.h"
#include "net.h"
#include "net_messages.h"
#include "log.h"
#include "filesystem.h"
#include "crc32.h"
#include "common.h"
#include "param_tree.h"
#include "ai8s.h"
#include "surza_time.h"
#include "launchnum.h"

#ifdef DELTA_HMI_ENABLE
#include "delta_hmi.h"

void delta_HMI_init_regs();
void delta_HMI_copy_indi(const net_msg_t*);
void delta_HMI_set_regs(uint16_t* ptr, uint16_t* start_reg, uint16_t* num);
#endif


#include "math\MYD.h" 
#include "math\rtwtypes.h"


#define SETTINGS_FILENAME  "settings.bin"
#define FIRMWARE_FILENAME  "Surza.RTA"
#define PARAMS_FILENAME    "params.dat"


#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint32_t size;            //размер данных
	uint32_t crc32;           //контрольная сумма по data[size]
	uint8_t  hash[16];        //MD5 хэш по data[size]
	// data[size]
} settings_file_header_t;
#pragma pack(pop)


// указатели на описание настроек и строк
static settings_file_header_t* settings_header = NULL;   
static settings_file_header_t* strings_header = NULL;

//указатели на выделенную память под хранение настроек
static char* settings_data_ptr = NULL;



static bool check_settings_msg(msg_type_settings_t* msg, unsigned size) {


	if (size < sizeof(msg_type_settings_t) || size < msg->data_offset + msg->bytes) {
		LOG_AND_SCREEN("Settings file incorrect data!");
		return false;
	}

	if (!crc32_check((char*)(&msg->crc32+1), msg->data_offset + msg->bytes - 4, msg->crc32)) {
		LOG_AND_SCREEN("Settings file bad crc32!");
		return false;
	}


	return true;
}




void settings_recv_callback(net_msg_t* msg, uint64_t channel) {
	DEBUG_ADD_POINT(309);

	LOG_AND_SCREEN("New settings message received");

	if (msg->size < sizeof(msg_type_settings_t))
		return;

	msg_type_settings_t* s_msg  = (msg_type_settings_t*) &msg->data[0];
	if (!check_settings_msg(s_msg, msg->size))
		return;


	settings_file_header_t header;
	filesystem_fragment_t fragments[2];

	header.size = s_msg->bytes;
	header.crc32 = crc32((char*)s_msg+s_msg->data_offset, s_msg->bytes);
	memcpy(&header.hash, &s_msg->md5_hash, 16);
	
	fragments[0].pointer = (char*) &header;
	fragments[0].size = sizeof(header);
	fragments[1].pointer = (char*)s_msg + s_msg->data_offset;
	fragments[1].size = s_msg->bytes;

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR)
		return;

	if (filesystem_write_file_fragments(SETTINGS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR)
		return;

    
	LOG_AND_SCREEN("Reboot...");
	reboot();
}


void settings_request_callback(net_msg_t* msg, uint64_t channel) {
	DEBUG_ADD_POINT(310);

	if (msg->size < sizeof(msg_type_settings_request_t))
		return;

	msg_type_settings_t* s_msg;

	msg_type_settings_request_t* request = (msg_type_settings_request_t*)&msg->data[0];

	bool any = true;
	for (int i = 0; i<16; i++)
		if (request->md5_hash[i]) {
			any = false;
			break;
		}

	if (!init_flags.settings_init || !settings_header
		|| (!any && memcmp(request->md5_hash, settings_header->hash, 16))) {

		
		if (net_msg_buf_get_available_space(msg) < sizeof(msg_type_settings_t)) {
			msg = net_get_msg_buf(sizeof(msg_type_settings_t));
			if (!msg)
				return;
		}

		s_msg = (msg_type_settings_t*) &msg->data[0];
		s_msg->bytes = 0;
		s_msg->data_offset = sizeof(msg_type_settings_t);
		memset(s_msg->md5_hash, 0, 16);
		s_msg->crc32 = crc32((char*)&s_msg->crc32 + 4, sizeof(msg_type_settings_t) - 4);

		msg->size = sizeof(msg_type_settings_t);
		msg->subtype = 0;
		msg->type = (uint8_t)NET_MSG_SURZA_SETTINGS;

		net_send_msg(msg, NET_PRIORITY_HIGH, channel);

		return;
	}


	//чтение файла настроек

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) return;
	
	filesystem_fragment_t fragments[2];

	fragments[0].size = sizeof(settings_file_header_t);
	fragments[1].size = 0;

	if (filesystem_read_file_fragments(SETTINGS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR) return;

	settings_file_header_t* s_header = (settings_file_header_t*)fragments[0].pointer;
	char* s_data_ptr = fragments[1].pointer;

	//проверка считанных данных
	if (!s_data_ptr 
		|| !s_header
		|| s_header->size != fragments[1].size
		|| !crc32_check(s_data_ptr, s_header->size, s_header->crc32)) {
		if(s_data_ptr)
			free(s_data_ptr);
		if(s_header)
			free(s_header);
		return;
	}


	//заполняю сообщение им отправляю

	unsigned size = sizeof(msg_type_settings_t) + s_header->size;

	while (true) {

		if (net_msg_buf_get_available_space(msg) < size) {
			msg = net_get_msg_buf(size);
			if (!msg)
				break;
		}

		s_msg = (msg_type_settings_t*) &msg->data;
		s_msg->bytes = s_header->size;
		s_msg->data_offset = sizeof(msg_type_settings_t);
		memcpy(s_msg->md5_hash, s_header->hash, 16);
		memcpy(s_msg + 1, s_data_ptr, s_header->size);
		s_msg->crc32 = crc32((char*)&s_msg->crc32 + 4, size - 4);

		msg->size = size;
		msg->subtype = 0;
		msg->type = (uint8_t)NET_MSG_SURZA_SETTINGS;

		net_send_msg(msg, NET_PRIORITY_HIGH, channel);

		break;
	}

	free(s_header);
	free(s_data_ptr);

	return;

}



void new_firmware_callback(net_msg_t* msg, uint64_t channel) {
	DEBUG_ADD_POINT(311);
	LOG_AND_SCREEN("New firmware received");

	msg_type_firmware_t* f_msg;
	bool ok = false;

	while (true) {

		if (msg->size < sizeof(msg_type_firmware_t)) break;

		f_msg = (msg_type_firmware_t*)&msg->data[0];

		if (f_msg->data_offset + f_msg->bytes != msg->size) break;

		if (!crc32_check((char*)f_msg + 4, f_msg->data_offset + f_msg->bytes - 4, f_msg->crc32)) break;

		ok = true;
		break;
	}

	if (!ok) {
		LOG_AND_SCREEN("Error! New firmware corrupted!");
		return;
	}

    //запись файла на диск

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Filesystem i/o error!");
		return;
	}

	//удалить атрибут "только чтение"
	int ttt = RTFSetAttributes(FIRMWARE_FILENAME, 0);
	if (ttt < 0) {
		LOG_AND_SCREEN("RTFSetAttributes return %d", ttt);
	}

	if (filesystem_write_file(FIRMWARE_FILENAME, ((char*)f_msg)+f_msg->data_offset,f_msg->bytes) != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Error! Write file \"%s\" failed!", FIRMWARE_FILENAME);
		return;
	}

	RTFSetAttributes(FIRMWARE_FILENAME, RTF_ATTR_READ_ONLY);

	LOG_AND_SCREEN("Reboot...");
	reboot();
}



static bool init_math();
static bool init_adc();
static bool init_indi();
static bool init_dic();
static bool init_fiu();
static bool init_params();
static bool init_journal();


int logic_init() {

	bool ok = false;

	if (init_flags.settings_init) {

		//init logic
		while (true) {

			launchnum_init();

			DEBUG_ADD_POINT(300);
			if (!init_math()) {
				LOG_AND_SCREEN("Logic main i/o tables init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(301);
			if (!init_adc()) {
				LOG_AND_SCREEN("ADC init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(302);
			if (!init_dic()) {
				LOG_AND_SCREEN("DIC init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(303);
			if (!init_fiu()) {
				LOG_AND_SCREEN("FIU init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(304);
			if (!init_indi()) {
				LOG_AND_SCREEN("INDI init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(305);
			if (!init_params()) {
				LOG_AND_SCREEN("PARAMS init failed!");
				break;
			}

			DEBUG_ADD_POINT(306);
			if (!init_journal()) {
				LOG_AND_SCREEN("JOURNAL init failed!");
				break;
			}
			
			DEBUG_ADD_POINT(307);

			//дельта опционально
            #ifdef DELTA_HMI_ENABLE
			delta_HMI_init_regs();
			delta_hmi_open(delta_HMI_set_regs);
            #endif
			
			DEBUG_ADD_POINT(308);

			ok = true;
			break;
		}
		
	}

	//прием новых конфигурационных файлов включаем в любом случае
	net_add_dispatcher((uint8_t)NET_MSG_SURZA_SETTINGS, settings_recv_callback);

	//прием запросов на получение текущего файла настройки
	net_add_dispatcher((uint8_t)NET_MSG_SETTINGS_REQUEST, settings_request_callback);

	//прием файла новой прошивки
	net_add_dispatcher((uint8_t)NET_MSG_SURZA_FIRMWARE, new_firmware_callback);

	DEBUG_ADD_POINT(312);

	return ok ? 0 : (-1);
}




int read_settings() {

	LOG_AND_SCREEN("Load settings...");

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Filesystem i/o error!");
		return -1;
	}
	
	filesystem_fragment_t fragments[2];

	fragments[0].size = sizeof(settings_file_header_t);
	fragments[1].size = 0;

	//чтение файла настроек
	if (filesystem_read_file_fragments(SETTINGS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Load settings file error!");
		return -1;
	}

	settings_header = (settings_file_header_t*) fragments[0].pointer;
	settings_data_ptr = fragments[1].pointer;

	//проверка считанных данных
	if (!settings_data_ptr
		|| settings_header->size != fragments[1].size
		|| !crc32_check(settings_data_ptr, settings_header->size, settings_header->crc32) ) {
		LOG_AND_SCREEN("Settings file data corrupted!");
		if(settings_data_ptr)
			free(settings_data_ptr);
		return -1;
	}

	LOG_AND_SCREEN("Settings load OK");
	LOG_AND_SCREEN("Building param tree...");

	
	if (ParamTree_Make(settings_data_ptr, settings_header->size) < 0) {
		LOG_AND_SCREEN("Param tree build failed!");
		free(settings_data_ptr);
		return -1;
	}

	LOG_AND_SCREEN("Param tree built");
	LOG_AND_SCREEN("Read settings OK");

	return 0;
}



static void MAIN_LOGIC_PERIOD_FUNC();


static float* math_real_in;
static int32_t* math_int_in;
static uint8_t* math_bool_in;
static float* math_real_out;
static int32_t* math_int_out;
static uint8_t* math_bool_out;

static unsigned math_real_in_num;
static unsigned math_int_in_num;
static unsigned math_bool_in_num;
static unsigned math_real_out_num;
static unsigned math_int_out_num;
static unsigned math_bool_out_num;




//------------------------------------------------------------------------
//  Основные таблицы
//------------------------------------------------------------------------

#define MATH_IO_REAL_IN   MYD_U.In_Real
#define MATH_IO_INT_IN    MYD_U.In_Int
#define MATH_IO_BOOL_IN   MYD_U.In_Boolean
#define MATH_IO_REAL_OUT  MYD_Y.Out_Real
#define MATH_IO_INT_OUT   MYD_Y.Out_Int
#define MATH_IO_BOOL_OUT  MYD_Y.Out_Boolean

#define LOGIC_TYPE_REAL_IN    0
#define LOGIC_TYPE_INT_IN     1
#define LOGIC_TYPE_BOOL_IN    2
#define LOGIC_TYPE_REAL_OUT   3
#define LOGIC_TYPE_INT_OUT    4
#define LOGIC_TYPE_BOOL_OUT   5


static bool init_math() {

	math_real_in = NULL;
	math_int_in = NULL;
	math_bool_in = NULL;
	math_real_out = NULL;
	math_int_out = NULL;
	math_bool_out = NULL;

	math_real_in_num = 0;
	math_int_in_num = 0;
	math_bool_in_num = 0;
	math_real_out_num = 0;
	math_int_out_num = 0;
	math_bool_out_num = 0;


	param_tree_node_t* common_node = ParamTree_Find(ParamTree_MainNode(), "MAIN_TABLES", PARAM_TREE_SEARCH_NODE);
	if (!common_node)
		return false;

	param_tree_node_t* node;
	unsigned cnt;


	node = ParamTree_Find(common_node, "IN_REAL", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_REAL_IN) / sizeof(MATH_IO_REAL_IN[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of REAL inputs < Logic REAL inputs!");
			return false;
		}
		math_real_in = &MATH_IO_REAL_IN[0];
		math_real_in_num = cnt;
	}

	node = ParamTree_Find(common_node, "IN_INT", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_INT_IN) / sizeof(MATH_IO_INT_IN[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of INT inputs < Logic INT inputs!");
			return false;
		}
		math_int_in = &MATH_IO_INT_IN[0];
		math_int_in_num = cnt;
	}

	node = ParamTree_Find(common_node, "IN_BOOL", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_BOOL_IN) / sizeof(MATH_IO_BOOL_IN[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of BOOL inputs < Logic BOOL inputs!");
			return false;
		}
		math_bool_in = &MATH_IO_BOOL_IN[0];
		math_bool_in_num = cnt;
	}

	node = ParamTree_Find(common_node, "OUT_REAL", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_REAL_OUT) / sizeof(MATH_IO_REAL_OUT[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of REAL outputs < Logic REAL outputs!");
			return false;
		}
		math_real_out = &MATH_IO_REAL_OUT[0];
		math_real_out_num = cnt;
	}

	node = ParamTree_Find(common_node, "OUT_INT", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_INT_OUT) / sizeof(MATH_IO_INT_OUT[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of INT outputs < Logic INT outputs!");
			return false;
		}
		math_int_out = &MATH_IO_INT_OUT[0];
		math_int_out_num = cnt;
	}

	node = ParamTree_Find(common_node, "OUT_BOOL", PARAM_TREE_SEARCH_NODE);
	if (node) {
		cnt = ParamTree_ItemsNum(node);
		if (cnt > (sizeof(MATH_IO_BOOL_OUT) / sizeof(MATH_IO_BOOL_OUT[0]))) {
			LOG_AND_SCREEN("ERROR!!! MYD N of BOOL outputs < Logic BOOL outputs!");
			return false;
		}
		math_bool_out = &MATH_IO_BOOL_OUT[0];
		math_bool_out_num = cnt;
	}


	//инициализация МЯДа
	MYD_initialize();

	return true;
}

//------------------------------------------------------------------------
bool indi_init_ok = false;
static msg_type_indi_t* indi_msg = NULL;
static volatile int indi_msg_fill_new_data;

struct indi_step_t {
	void*     src_ptr;
	unsigned  dst_offset;
	unsigned  bytes;
};

static unsigned indi_steps;
static struct indi_step_t* indi_step_pointers = NULL;

static unsigned base_offset[6];   //смещения начала данных по типам от начала сообщения (msg_type_indi_t)

static unsigned indi_msg_size;    //размер заполненного сообщения (msg_type_indi_t + данные)


//максимально допустимое количество копируемых байт индикаторов в одном прерывании (такте сурзы)
//задавать обязательно только кратно 4 !!! (иначе могут портится данные, так как одно значение может писаться частями из разных тактов)
#define INDI_PART_SIZE  64


static bool init_indi() {

	indi_init_ok = false;
	indi_msg = NULL;
	indi_msg_fill_new_data = 0;


	unsigned bytes_num[6];  //количество данных для копирования по типам в байтах
	bytes_num[0] = math_real_in_num * 4;
	bytes_num[1] = math_int_in_num * 4;
	bytes_num[2] = math_bool_in_num;
	bytes_num[3] = math_real_out_num * 4;
	bytes_num[4] = math_int_out_num * 4;
	bytes_num[5] = math_bool_out_num;

	void* src_ptr[6];    //указатели на источники данных
	src_ptr[0] = math_real_in;
	src_ptr[1] = math_int_in;
	src_ptr[2] = math_bool_in;
	src_ptr[3] = math_real_out;
	src_ptr[4] = math_int_out;
	src_ptr[5] = math_bool_out;
	
	//смещения начала данных разных типов от начала сообщения
	base_offset[0] = sizeof(msg_type_indi_t);
	for (int i = 1; i < 6; i++)
		base_offset[i] = base_offset[i - 1] + bytes_num[i-1];

	indi_msg_size = base_offset[5] + bytes_num[5];

	//определения кол-ва частей для копирования для каждого типа
	unsigned parts[6];
	for (int i = 0; i < 6; i++)
		parts[i] = (bytes_num[i] / INDI_PART_SIZE) + ((bytes_num[i] % INDI_PART_SIZE) ? 1 : 0);

	indi_steps = 0;
	unsigned n;

	for (n = 0, indi_steps = 0; n < 6; n++)
		indi_steps += parts[n];

	//выделение памяти для таблицы
	indi_step_pointers = (struct indi_step_t*) malloc(sizeof(struct indi_step_t)*indi_steps);
	if (!indi_step_pointers)
		return false;


	//заполнение таблицы
	n = 0;
	unsigned p;
	unsigned offset;


	for(int i=0; i<6; i++)
		for (p = 0, offset = base_offset[i]; p < parts[i]; p++, n++) {
			indi_step_pointers[n].src_ptr = (char*)(src_ptr[i]) + p * INDI_PART_SIZE;
			indi_step_pointers[n].bytes = (p + 1 == parts[i]) ? ( (bytes_num[i] % INDI_PART_SIZE) ? (bytes_num[i] % INDI_PART_SIZE ) : INDI_PART_SIZE ) : INDI_PART_SIZE;
			indi_step_pointers[n].dst_offset = offset;
			
			offset += indi_step_pointers[n].bytes;
		}

	indi_init_ok = true;

	return true;
}




static void indi_copy() {

	static unsigned step = 0;

	if (atom_get_state(&indi_msg_fill_new_data)) {

		memcpy((char*)indi_msg + indi_step_pointers[step].dst_offset, indi_step_pointers[step].src_ptr, indi_step_pointers[step].bytes);
		step++;
		if (step == indi_steps) {
			step = 0;
			atom_set_state(&indi_msg_fill_new_data, 0);
		}
	}

}



void indi_send() {

	if (!indi_init_ok)
		return;

	static int last_time = 0;

	static bool update = false;

	int new_time = steady_clock_get();

	if (steady_clock_expired(last_time, new_time, INDI_PERIOD_MS*1000)) {
		last_time = new_time;
		update = true;
	}

	DEBUG_ADD_POINT(200);

	static int state = 0;
	static net_msg_t* msg = NULL;


	switch (state) {
	case 0:   //создать и заполнить новое сообщение
		if (update) {
			update = false;

			DEBUG_ADD_POINT(201);

			if (atom_get_state(&indi_msg_fill_new_data))
				break;


			msg  = net_get_msg_buf(indi_msg_size);
			if (!msg)
				break;

			msg->type = (uint8_t)NET_MSG_INDI;
			msg->subtype = 0;
			indi_msg = (msg_type_indi_t*) &msg->data[0];

			//заполенение заголовка сообщения
			indi_msg->header_size = sizeof(msg_type_indi_t);

			memcpy(indi_msg->md5_hash, settings_header->hash, 16);

			indi_msg->in_real_num = math_real_in_num;
			indi_msg->in_int_num = math_int_in_num;
			indi_msg->in_bool_num = math_bool_in_num;
			indi_msg->out_real_num = math_real_out_num;
			indi_msg->out_int_num = math_int_out_num;
			indi_msg->out_bool_num = math_bool_out_num;

			indi_msg->in_real_offset = base_offset[0];
			indi_msg->in_int_offset = base_offset[1];
			indi_msg->in_bool_offset = base_offset[2];
			indi_msg->out_real_offset = base_offset[3];
			indi_msg->out_int_offset = base_offset[4];
			indi_msg->out_bool_offset = base_offset[5];

			indi_msg->time = get_time();
			indi_msg->launch_num = launchnum_get();

			//флаг необходимости заполнить данные
			atom_set_state(&indi_msg_fill_new_data, 1);
			state = 1;
		}

		break;

	case 1:   //ожидание заполнения данных

		if (!atom_get_state(&indi_msg_fill_new_data)) {
			//данные сообщения заполнены, отправка сообщения

			DEBUG_ADD_POINT(202);

            #ifdef DELTA_HMI_ENABLE
			  delta_HMI_copy_indi(msg);  //копируем индикаторы для дельты
            #endif

			DEBUG_ADD_POINT(203);

			net_send_msg(msg, NET_PRIORITY_HIGH, NET_BROADCAST_CHANNEL);
			state = 0;
		}

		break;
	}


}




//------------------------------------------------------------------------
//  ADC
//------------------------------------------------------------------------

static unsigned adc_num;
static unsigned adc_ch_num[2];

static int32_t* adc_table[2][8];

static unsigned adc1_adr, adc2_adr, period;

unsigned SurzaPeriod() { return period; }

void adc_irq_handler(void);


bool init_adc() {

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (!node)
		return false;

	param_tree_node_t* item;

	//считывание адресов плат и периода сурзы

	item = ParamTree_Find(node, "ADC1", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &adc1_adr) <= 0)
		return false;

	item = ParamTree_Find(node, "ADC2", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &adc2_adr) <= 0)
		return false;

	item = ParamTree_Find(node, "PERIOD", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &period) <= 0)
		return false;


	item = ParamTree_Find(ParamTree_MainNode(), "ADC", PARAM_TREE_SEARCH_NODE);
	if (!item)
		return false;


	//зануление всего
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 8; j++)
			adc_table[i][j] = NULL;
		adc_ch_num[i] = 0;
	}
	adc_num = 1;


	//считывание каналов (если заданы)
	item = ParamTree_Child(item);
	if (item) {

		unsigned data[16];
		for (int i = 0; i < 16; i++)
			data[i] = math_int_in_num;

		unsigned num=0;
		unsigned n;

		item = ParamTree_FirstItem(item);
		while (item && num<16) {
			
			if (sscanf_s(item->value, "%u", &n) <= 0 || n>=16)
				return false;
			if (sscanf_s(item->name, "%u", &data[n]) <= 0 || data[n] >= math_int_in_num)
				return false;

			num++;

			item = ParamTree_NextItem(item);
		}


		//заполнение таблиц ацп

		for (unsigned i = 0; i < 16; i++){
			if (data[i] < math_int_in_num) {
				adc_table[i / 8][i % 8] = math_int_in + data[i];
				adc_ch_num[i / 8]++;
			}
		}

		if (adc_ch_num[1])
			adc_num = 2;

	}

	return InitAI8S(adc_num, adc1_adr, adc2_adr, period, adc_irq_handler);
}





void adc_irq_handler(void){

	DEBUG_ADD_POINT(20);

	//запуск второго ацп
	if(adc_num>1)
		ai8s_start_second_adc();

	//чтение каналов ацп 1
	for (unsigned i = 0; i < 8; i++)
		if(adc_table[0][i])
			*(adc_table[0][i]) = ai8s_read_ch(0, i);

	
	//сброс прерывания чтением 7го канала (если не был прочитан до этого)
	if(!adc_table[0][7])
		(void)ai8s_read_ch(0, 7);


	//ожидание и чтение каналов второго ацп
	if (adc_num > 1) {
		if (ai8s_wait_second_adc()) {
			for (unsigned i = 0; i < 8; i++)
				if (adc_table[1][i])
					*(adc_table[1][i]) = ai8s_read_ch(1, i);
		}
		else {  //в случае отказа второй платы все измерения принудительно зануляются
			for (unsigned i = 0; i < 8; i++)
				if (adc_table[1][i])
					*(adc_table[1][i]) = 0;
		}
	}

	 
	//обновление собаки
	wdt_update();

	//запуск основной функции логики
	if (init_flags.logic_init)
		MAIN_LOGIC_PERIOD_FUNC();

}

//------------------------------------------------------------------------




//------------------------------------------------------------------------
//  DIC
//------------------------------------------------------------------------

static unsigned dic_adr;

static uint8_t dic_dir[12];

static uint8_t* dic_table[96];
//static uint8_t* dic_table_out[96];


#define DIC_DIR_IGNORE  0
#define DIC_DIR_IN      1
#define DIC_DIR_OUT     2

static const uint8_t dic_adr_regs[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};


static bool init_dic() {

	memset(dic_dir, DIC_DIR_IGNORE, 12);

	for (int i = 0; i < 96; i++)
		dic_table[i] = NULL;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (!node)
		return false;

	param_tree_node_t* item;

	//считывание адреса платы DIC

	item = ParamTree_Find(node, "DIC", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &dic_adr) <= 0)
		return false;


	//поиск всех входов и выходов
	param_tree_node_t *in_node, *out_node;

	in_node = ParamTree_Find(ParamTree_MainNode(), "DIC_IN", PARAM_TREE_SEARCH_NODE);
	out_node = ParamTree_Find(ParamTree_MainNode(), "DIC_OUT", PARAM_TREE_SEARCH_NODE);

	if (!in_node && !out_node)
		return true;    //DIC не используется



	//проверка наличия платы DIC
	if (RTIn(dic_adr + 11) != 'g') {
		LOG_AND_SCREEN("DIC:  NO DIC!!! dic adress: 0x%04X", dic_adr);
		return false;
	}


	//считывание входов
	item = NULL;
	if(in_node)
		item = ParamTree_Child(in_node);
	if (item) {

		unsigned pin, reg;

		item = ParamTree_FirstItem(item);
		while (item) {

			if (sscanf_s(item->name, "%u", &reg) <= 0 || reg >= math_bool_in_num)
				return false;
			if (sscanf_s(item->value, "%u", &pin) <= 0 || pin >= 96)
				return false;
			
			dic_dir[pin/8] = DIC_DIR_IN;
			dic_table[pin] = math_bool_in + reg;

			item = ParamTree_NextItem(item);
		}
	}

	//считывание выходов
	item = NULL;
	if (out_node)
		item = ParamTree_Child(out_node);
	if (item) {

		unsigned pin, reg;

		item = ParamTree_FirstItem(item);
		while (item) {

			if (sscanf_s(item->name, "%u", &reg) <= 0 || reg >= math_bool_out_num)
				return false;
			if (sscanf_s(item->value, "%u", &pin) <= 0 || pin >= 96)
				return false;

			dic_dir[pin / 8] = DIC_DIR_OUT;
			dic_table[pin] = math_bool_out + reg;

			item = ParamTree_NextItem(item);
		}
	}


	
	// настройка платы DIC  в соответствии с прочитанной конифгурацией

	//все выходы заранее в отключенное состояние
	for(int i=0; i<12; i++)
		RTOut(dic_adr+dic_adr_regs[i], 0);

	//конфигурирование на вход\выход
	uint8_t mask;

	//XP1
	mask = 0x80;
	mask |= (dic_dir[0] != DIC_DIR_OUT ? 0x10 : 0);
	mask |= (dic_dir[1] != DIC_DIR_OUT ? 0x02 : 0);
	mask |= (dic_dir[2] != DIC_DIR_OUT ? 0x09 : 0);
	RTOut(dic_adr + 3, mask);

	//XP2
	mask = 0x80;
	mask |= (dic_dir[3] != DIC_DIR_OUT ? 0x10 : 0);
	mask |= (dic_dir[4] != DIC_DIR_OUT ? 0x02 : 0);
	mask |= (dic_dir[5] != DIC_DIR_OUT ? 0x09 : 0);
	RTOut(dic_adr + 7, mask);

	//XP5
	mask = 0x80;
	mask |= (dic_dir[6] != DIC_DIR_OUT ? 0x10 : 0);
	mask |= (dic_dir[7] != DIC_DIR_OUT ? 0x02 : 0);
	mask |= (dic_dir[8] != DIC_DIR_OUT ? 0x09 : 0);
	RTOut(dic_adr + 11, mask);

	//XP6
	mask = 0x80;
	mask |= (dic_dir[9] != DIC_DIR_OUT ? 0x10 : 0);
	mask |= (dic_dir[10] != DIC_DIR_OUT ? 0x02 : 0);
	mask |= (dic_dir[11] != DIC_DIR_OUT ? 0x09 : 0);
	RTOut(dic_adr + 15, mask);

	//все выходы заранее в отключенное состояние (второй раз на всяк случай)
	for (int i = 0; i<12; i++)
		RTOut(dic_adr + dic_adr_regs[i], 0);
	

	//установка аппаратного антидребезга
	RTOut(dic_adr + 15, 0x01);
	RTOut(dic_adr + 0, 0x08);  //320нс
	RTOut(dic_adr + 15, 0x00);

	

	return true;
}


void dic_read() {

	uint8_t u8;
	unsigned cnt;
	unsigned adr;

	for (unsigned i = 0; i < 12; i++)
		if (dic_dir[i] == DIC_DIR_IN) {
			u8 = RTIn(dic_adr+dic_adr_regs[i]);
			cnt = (i << 3);
			for (adr = cnt; adr < cnt + 8; adr++, u8 >>= 1)
				if (dic_table[adr])
					*(dic_table[adr]) = (u8 & 0x01) ? TRUE : FALSE;
		}

}

void dic_write() {

	uint8_t u8;
	unsigned cnt;
	unsigned adr;

	for (unsigned i = 0; i < 12; i++)
		if (dic_dir[i] == DIC_DIR_OUT) {
			cnt = (i << 3);
			for (adr = cnt, u8 = 0; adr < cnt + 8; adr++) {
				u8 >>= 1;
				u8 |= (dic_table[adr] ? (*(dic_table[adr]) ? 0x00 : 0x80) : 0x80);
			}
			RTOut(dic_adr+dic_adr_regs[i], u8);
		}

}

//-----------------------------------------------------------------


//------------------------------------------------------------------------
//   FIU
//------------------------------------------------------------------------
#define FIU_CODE  120

#define FIU_SETTINGS_OFFSET  (0x48)

static unsigned fiu1_adr, fiu2_adr;

typedef struct {
	uint8_t* cmd;
	int32_t* setpoint;
	unsigned adr_to_write;
} fiu_table_t;

fiu_table_t fiu_table[24];

unsigned fiu_table_size;


static bool init_fiu() {

	fiu_table_size = 0;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (!node)
		return false;

	param_tree_node_t* item;
	param_tree_node_t* fiu_node;

	//считывание адресов плат ФИУ

	item = ParamTree_Find(node, "FIU1", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &fiu1_adr) <= 0)
		return false;

	item = ParamTree_Find(node, "FIU2", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &fiu2_adr) <= 0)
		return false;


	fiu_node = ParamTree_Find(ParamTree_MainNode(), "FIU", PARAM_TREE_SEARCH_NODE);
	if (!fiu_node)
		return true;   // ФИУ не используется
	

	int num;
	bool err = false;
	unsigned reg, board, ch;
	bool board_en[2];

	board_en[0] = false;
	board_en[1] = false;


	node = ParamTree_Child(fiu_node);
	for (num = 0, node = ParamTree_Child(fiu_node); (num < 24) && node && !node->value && !err; num++, node = node->next) {

		item = ParamTree_Find(node, "board", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &board) <= 0 || board >= 2)
				err = true;
	

		item = ParamTree_Find(node, "channel", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &ch) <= 0 || ch >= 12)
				err = true;

		if (!err) {
			fiu_table[num].adr_to_write = (board ? fiu2_adr : fiu1_adr) + ch * 2;
			board_en[board] = true;
		}

		item = ParamTree_Find(node, "cmd", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &reg) <= 0 || reg >= math_bool_out_num)
				err = true;

		if(!err)
			fiu_table[num].cmd = math_bool_out + reg;


		item = ParamTree_Find(node, "setpoint", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &reg) <= 0 || reg >= math_int_out_num)
				err = true;

		if (!err) {
			fiu_table[num].setpoint = math_int_out + reg;

			fiu_table_size++;
		}
	}

	if (err) {
		fiu_table_size = 0;
		return false;
	}
	
	if (!fiu_table_size)
		return true;  //нет настроенных каналов


	//проверка наличия фиу1 и фиу2
	if (board_en[0] && RTIn(fiu1_adr + FIU_SETTINGS_OFFSET + 7) != FIU_CODE) {
		fiu_table_size = 0;
		LOG_AND_SCREEN("FIU_1:  NO FIU!! fiu adress: 0x%04X", fiu1_adr);
		return false;
	}

	if (board_en[1] && RTIn(fiu2_adr + FIU_SETTINGS_OFFSET + 7) != FIU_CODE) {
		fiu_table_size = 0;
		LOG_AND_SCREEN("FIU_2:  NO FIU!! fiu adress: 0x%04X", fiu2_adr);
		return false;
	}
	
	
	return true;
}


void fiu_write() {

	int32_t setpoint;

	for (unsigned i = 0; i < fiu_table_size; i++)
		if (*(fiu_table[i].cmd)){
			setpoint = *(fiu_table[i].setpoint);
			if (setpoint >= 0){
				if (setpoint > 0xffff)
					setpoint = 0xffff;
				RTOutW(fiu_table[i].adr_to_write, (uint16_t)setpoint);
			}
		}

}

//---------------------------------------------------------




//------------------------------------------------------------------------
//   PARAMS
//------------------------------------------------------------------------

typedef union {
	float f32;
	int32_t i32;
} param_value_t;

typedef struct {
	unsigned type;
	unsigned num;
	param_value_t val;
	param_value_t val_min;
	param_value_t val_max;
	param_value_t val_default;
} params_t;


#define  PARAM_TYPE_FLOAT  0
#define  PARAM_TYPE_INT    1
#define  PARAM_TYPE_BOOL   2


static params_t* params_ptr;
static unsigned params_num;

static volatile int params_update_param_flag;
static volatile unsigned params_update_type;
static volatile unsigned params_update_num;
static volatile param_value_t params_update_val;

static void params_apply_all();

static bool params_read_file();
static bool params_save_file();

void params_recv_callback(net_msg_t* msg, uint64_t channel);


static bool init_params() {

	params_ptr = NULL;
	params_num = 0;
	params_update_param_flag = 0;


	param_tree_node_t* param_node;
	param_tree_node_t* node;
	param_tree_node_t* item;



	param_node = ParamTree_Find(ParamTree_MainNode(), "PARAMS", PARAM_TREE_SEARCH_NODE);
	if (!param_node) {
		LOG_AND_SCREEN("No logic params!");
		return true;   // Параметры не используются
	}



	unsigned n = ParamTree_ChildNum(param_node);
	if (!n) {
		LOG_AND_SCREEN("No logic params (0)!");
		return true;   // нет параметров
	}


	params_ptr = (params_t*)malloc(n * sizeof(params_t));
	if (!params_ptr) {
		return false;
	}


	char *str_min;
	char *str_max;
	char *str_default;
	

	bool err = false;
	n = 0;
	
	for (node = ParamTree_Child(param_node); node && !err; node = node->next, n++) {

		item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &params_ptr[n].type) <= 0 || params_ptr[n].type > PARAM_TYPE_BOOL)
				err = true;


		item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &params_ptr[n].num) <= 0)
				err = true;


		item = ParamTree_Find(node, "min", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else str_min = item->value;

		item = ParamTree_Find(node, "max", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else str_max = item->value;

		item = ParamTree_Find(node, "default", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else str_default = item->value;


		if (!err) {
			switch (params_ptr[n].type) {
			case PARAM_TYPE_FLOAT: err = true;
				                   if (params_ptr[n].num >= math_real_in_num) break;
								   if (sscanf_s(str_min, "%f", &(params_ptr[n].val_min.f32)) <= 0) break;
								   if (sscanf_s(str_max, "%f", &(params_ptr[n].val_max.f32)) <= 0) break;
								   if (sscanf_s(str_default, "%f", &(params_ptr[n].val_default.f32)) <= 0) break;
								   if (params_ptr[n].val_min.f32 > params_ptr[n].val_max.f32) break;
								   if (params_ptr[n].val_default.f32 < params_ptr[n].val_min.f32) break;
								   if (params_ptr[n].val_default.f32 > params_ptr[n].val_max.f32) break;
								   err = false;
								   break;
			case PARAM_TYPE_INT:   err = true;
				                   if (params_ptr[n].num >= math_int_in_num) break;
				                   if (sscanf_s(str_min, "%d", &(params_ptr[n].val_min.i32)) <= 0) break;
				                   if (sscanf_s(str_max, "%d", &(params_ptr[n].val_max.i32)) <= 0) break;
				                   if (sscanf_s(str_default, "%d", &(params_ptr[n].val_default.i32)) <= 0) break;
				                   if (params_ptr[n].val_min.i32 > params_ptr[n].val_max.i32) break;
				                   if (params_ptr[n].val_default.i32 < params_ptr[n].val_min.i32) break;
				                   if (params_ptr[n].val_default.i32 > params_ptr[n].val_max.i32) break;
				                   err = false;
				                   break;
			case PARAM_TYPE_BOOL:  err = true;
				                   if (params_ptr[n].num >= math_bool_in_num) break;
								   if (sscanf_s(str_default, "%d", &(params_ptr[n].val_default.i32)) <= 0) break;
								   if (params_ptr[n].val_default.i32 < 0) break;
								   if (params_ptr[n].val_default.i32 > 1) break;
								   err = false;
								   break;
			default: err = true; break;
			}
		}

	}

	if (err) {
		free(params_ptr);
		params_ptr = NULL;
		return false;
	}

	params_num = n;
	

	//чтение файла параметров 
	
	if (!params_read_file()) {
		LOG_AND_SCREEN("Default params apply !!!");
		for(unsigned i=0; i<params_num; i++)
			params_ptr[i].val.i32 = params_ptr[i].val_default.i32;
		params_save_file();
	}
	

	//применение всех параметров на входы МЯДа
	params_apply_all();


	//прием сообщений для изменения параметров
	net_add_dispatcher((uint8_t)NET_MSG_SET_PARAM, params_recv_callback);

	return true;
}


static void params_apply_all() {
	DEBUG_ADD_POINT(357);

	for (unsigned i = 0; i < params_num; i++) {
		switch (params_ptr[i].type) {
		case PARAM_TYPE_FLOAT: MATH_IO_REAL_IN[params_ptr[i].num] = params_ptr[i].val.f32; break;
		case PARAM_TYPE_INT:   MATH_IO_INT_IN[params_ptr[i].num] = params_ptr[i].val.i32; break;
		case PARAM_TYPE_BOOL:  MATH_IO_BOOL_IN[params_ptr[i].num] = (params_ptr[i].val.i32) ? true : false; break;
		default: break;
		}
	}
}



#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint32_t crc32;           //контрольная сумма по data[params_num*sizeof(param_value_t)]
	uint8_t  hash[16];        //MD5 хэш соответствующей конфигурации
	uint32_t params_num;      //размер данных
							  // data[params_num*sizeof(param_value_t)]
} params_file_header_t;
#pragma pack(pop)


static bool params_read_file() {

	
	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) return false;

	filesystem_fragment_t fragments[2];

	fragments[0].size = sizeof(params_file_header_t);
	fragments[1].size = 0;

	if (filesystem_read_file_fragments(PARAMS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR) return false;

	params_file_header_t* header = (params_file_header_t*) fragments[0].pointer;
	param_value_t* val_ptr = (param_value_t*) fragments[1].pointer;

	bool ret = true;

	//проверка считанных данных
	if (!header
		|| !val_ptr
		|| header->params_num != fragments[1].size / sizeof(param_value_t)
		|| header->params_num != params_num
		|| !crc32_check((char*)val_ptr, fragments[1].size, header->crc32)
		|| memcmp(header->hash, settings_header->hash, 16) )
		ret = false;
	else {
	
		//запись в таблицу прочитанных параметров и их проверка
		param_value_t *ptr = val_ptr;
		for (unsigned i = 0; i < params_num && ret; i++, ptr++) {

			switch (params_ptr[i].type) {

			case PARAM_TYPE_FLOAT: 
				if (!_finite(ptr->f32)
					|| ptr->f32<params_ptr[i].val_min.f32
					|| ptr->f32>params_ptr[i].val_max.f32)
					ret = false;
				else
					params_ptr[i].val.f32 = ptr->f32;

				break;

			case PARAM_TYPE_INT:
				if (ptr->i32<params_ptr[i].val_min.i32
					|| ptr->i32>params_ptr[i].val_max.i32)
					ret = false;
				else
					params_ptr[i].val.i32 = ptr->i32;

				break;

			case PARAM_TYPE_BOOL:
				if (ptr->i32<params_ptr[i].val_min.i32 < 0
					|| ptr->i32>params_ptr[i].val_max.i32 > 1)
					ret = false;
				else
					params_ptr[i].val.i32 = ptr->i32;

				break;

			default: break;
			}

		}

	}

	//освобождение выделенной памяти
	if (header)
		free(header);
	if (val_ptr)
		free(val_ptr);

	return ret;
}


static bool params_save_file() {

	LOG_AND_SCREEN("Save params...");

	params_file_header_t header;

	memcpy(header.hash, settings_header->hash, 16);
	header.params_num = params_num;

	size_t val_size = params_num * sizeof(param_value_t);

	param_value_t* val_ptr = (param_value_t*)malloc(val_size);
	if (!val_ptr) {
		LOG_AND_SCREEN("malloc() error!");
		return false;
	}

	for (unsigned i = 0; i < params_num; i++)
		val_ptr[i].i32 = params_ptr[i].val.i32;

	header.crc32 = crc32((char*)val_ptr, val_size);

	filesystem_fragment_t fragments[2];

	fragments[0].pointer = (char*)&header;
	fragments[0].size = sizeof(header);
	fragments[1].pointer = (char*)val_ptr;
	fragments[1].size = val_size;

	bool ret = true;

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR
		|| filesystem_write_file_fragments(PARAMS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR){
		LOG_AND_SCREEN("Filesystem error!");
		ret = false;
	}
	else {
		LOG_AND_SCREEN("Ok");
	}

	free(val_ptr);

	return ret;
}


void params_recv_callback(net_msg_t* msg, uint64_t channel) {
	DEBUG_ADD_POINT(350);

	if (msg->size < sizeof(msg_type_set_param_t))
		return;

	msg_type_set_param_t* p_msg = (msg_type_set_param_t*)&msg->data[0];

	if (memcmp(p_msg->hash, settings_header->hash, 16)) {
		LOG_AND_SCREEN("Can't apply param - configuration does not match!");
		return;
	}

	DEBUG_ADD_POINT(351);

	if (p_msg->num>=params_num) {
		if (p_msg->num==0xffff) {  //reset all to default
			global_spinlock_lock();
			for (unsigned i = 0; i < params_num; i++)
				params_ptr[i].val.i32 = params_ptr[i].val_default.i32;
			global_spinlock_unlock();
		}
		else {

			LOG_AND_SCREEN("Can't apply param - wrong param num!");
			return;
		}
	}

	DEBUG_ADD_POINT(352);

	bool ok = true;

	if(p_msg->num!=0xffff)
	switch (params_ptr[p_msg->num].type) {
	case PARAM_TYPE_FLOAT: 
		global_spinlock_lock();
		if (!_finite(p_msg->value.f32)
			|| p_msg->value.f32<params_ptr[p_msg->num].val_min.f32
			|| p_msg->value.f32>params_ptr[p_msg->num].val_max.f32) {
			ok = false;
		} else {
			params_ptr[p_msg->num].val.f32 = p_msg->value.f32;
		}
		global_spinlock_unlock();
		break;

	case PARAM_TYPE_INT:
		if (p_msg->value.i32<params_ptr[p_msg->num].val_min.i32
			|| p_msg->value.i32>params_ptr[p_msg->num].val_max.i32) {
			ok = false;
		}
		else {
			params_ptr[p_msg->num].val.i32 = p_msg->value.i32;
		}
		break;

	case PARAM_TYPE_BOOL:  
		if (p_msg->value.i32<0
			|| p_msg->value.i32>1) {
			ok = false;
		}
		else {
			params_ptr[p_msg->num].val.i32 = p_msg->value.i32;
		}
		break;

	default: return;
	}

	DEBUG_ADD_POINT(353);

	if (!ok) {
		LOG_AND_SCREEN("Can't apply param - wrong value!");
		return;
	}
	
	if (!params_save_file())
		return;

	DEBUG_ADD_POINT(354);

	if (atom_get_state(&params_update_param_flag))
		return;

	if (p_msg->num == 0xffff)
		params_update_num = 0xffff;
	else {
		params_update_type = params_ptr[p_msg->num].type;
		params_update_num = params_ptr[p_msg->num].num;
		params_update_val.i32 = params_ptr[p_msg->num].val.i32;
	}

	DEBUG_ADD_POINT(355);
	
	atom_set_state(&params_update_param_flag, 1);

    return;
}


static void params_update() {

	if (!params_update_param_flag)
		return;

	params_update_param_flag = 0;

	DEBUG_ADD_POINT(356);

	if (params_update_num == 0xffff)
		params_apply_all();
	else {
		switch (params_update_type) {
		case PARAM_TYPE_FLOAT: MATH_IO_REAL_IN[params_update_num] = params_update_val.f32; break;
		case PARAM_TYPE_INT:   MATH_IO_INT_IN[params_update_num] = params_update_val.i32; break;
		case PARAM_TYPE_BOOL:  MATH_IO_BOOL_IN[params_update_num] = (params_update_val.i32) ? true : false; break;
		default: return;
		}
	}
	
	return;
}


//------------------------------------------------------------------------




//------------------------------------------------------------------------
//   JOURNAL
//------------------------------------------------------------------------

#define EVENTS_BUF_SIZE  5000    //максимальное количество сохраняемых событий (храненение перед отправкой в шлюз)

typedef struct {
	uint8_t* value_ptr;  // указатель на флаг события
              //настройки из конфигурации
	uint32_t increment;
	uint32_t decrement;
	uint32_t block_level;
	uint32_t block_time;
	uint32_t block_warning_time;
	uint32_t edge;
	          //текущее состояние данных события во время работы
	uint32_t flag;       // текущее состояние флага события
	uint32_t prev_flag;  // флаги событий на предыдущем вызове
	uint32_t change;     // признак возникновения события с учетом фронта
	uint32_t timer;      // счетчик события
	uint32_t lock;       // статус заблокирован\разблокирован
	uint32_t block_warning_timer; // счетчик до предупреждения о слишком долгой блокировке
} journal_event_data_t;

static journal_event_data_t* event_data;      //данные каждого события
static journal_event_data_t* event_data_end;  // = event_data + events_num * sizeof(journal_event_data_t)

static uint8_t* events_result;  //результаты (состояния событий после обработки в такте сурзы)
static unsigned events_num;     //количество событий


static bool journal_init_ok;

static uint64_t journal_event_unique_id;


#pragma pack(push)
#pragma pack(1) 

typedef struct {
	uint64_t unique_id;
	surza_time_t time;
	//массив состояния событий events_result
	//данные события типа REAL
	//данные события типа INT
	//данные события типа BOOL
} journal_event_t;

#pragma pack(pop)

static unsigned journal_event_size;   //размер всей структуры journal_event_t, вычисляемый на этапе инициализации журнала

uint8_t* journal_events;              //кольцевой массив сохраненных событий
int journal_events_num;               //максимальное кол-во событий для сохранения в кольцевом массиве
volatile int journal_events_head;     //указатель на запись в массив. Всегда указывает на последнее добавленное событие. Исключение: При head==tail записей нет.
volatile int journal_events_tail;     //указатель на чтение из массива. Всегда указывает на событие, находящееся в массиве дольше всего. Исключение: При head==tail записей нет.

//смещение в байтах до полей с данными события от начала  структуры journal_event_t
static unsigned journal_event_offset_result;
static unsigned journal_event_offset_real;
static unsigned journal_event_offset_int;
static unsigned journal_event_offset_bool;

//количество данных для копирования
static unsigned journal_event_num_real;
static unsigned journal_event_num_int;
static unsigned journal_event_num_bool;

//указатели на массивы указателей на данные в выходных таблицах мяда
static float*   (*journal_event_real_myd_ptrs)[];
static int32_t* (*journal_event_int_myd_ptrs)[];
static uint8_t* (*journal_event_bool_myd_ptrs)[];

static size_t journal_msg_size;

void journal_request_callback(net_msg_t* msg, uint64_t channel);

static void journal_free_all_memory() {
	free(event_data);
	free(events_result);
	if (journal_event_real_myd_ptrs) free(journal_event_real_myd_ptrs);
	if (journal_event_int_myd_ptrs) free(journal_event_int_myd_ptrs);
	if (journal_event_bool_myd_ptrs) free(journal_event_bool_myd_ptrs);
}


static bool init_journal() {

	journal_init_ok = false;

	param_tree_node_t* journal_node;
	param_tree_node_t* events_node;
	param_tree_node_t* data_node;
	param_tree_node_t* node;
	param_tree_node_t* item;
	

	journal_node = ParamTree_Find(ParamTree_MainNode(), "JOURNAL", PARAM_TREE_SEARCH_NODE);
	if (!journal_node) {
		LOG_AND_SCREEN("No JOURNAL!");
		return true;   // Журнал событий не используются
	}


	events_node = ParamTree_Find(journal_node, "EVENTS", PARAM_TREE_SEARCH_NODE);
	if (!events_node) {
		LOG_AND_SCREEN("No JOURNAL EVENTS!");
		return true;   // Нет событий в журнале событий
	}
	else {
		events_num  = ParamTree_ChildNum(events_node);
		if (!events_num) {
			LOG_AND_SCREEN("No JOURNAL EVENTS!!");
			return true;   // нет событий
		}
	}


	//поиск и заполнение всех событий

	 //выделение памяти для данных каждого события и их результатов
	event_data = (journal_event_data_t*)calloc(events_num , sizeof(journal_event_data_t));
	if (!event_data) {
		LOG_AND_SCREEN("JOURNAL init error! (alloc event_data)");
		return false;
	}
	event_data_end = event_data + events_num;

	events_result = (uint8_t*)malloc(events_num);
	if (!events_result) {
		free(event_data);
		LOG_AND_SCREEN("JOURNAL init error! (alloc events_result)");
		return false;
	}


	//заполнение данных событий из дерева настроек

	bool err = false;
	unsigned n = 0;

	for (node = ParamTree_Child(events_node); node && !err; node = node->next, n++) {

		item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].increment)) <= 0 || event_data[n].increment >= math_bool_out_num)  //использую поле increment как временную переменную
				err = true;

		//указатель на флаг события в выходной структуре мяда
		event_data[n].value_ptr = math_bool_out + event_data[n].increment;
		
		
		item = ParamTree_Find(node, "edge", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].edge)) <= 0 || event_data[n].edge>2)
				err = true;

		item = ParamTree_Find(node, "increment", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].increment)) <= 0)
				err = true;

		item = ParamTree_Find(node, "decrement", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].decrement)) <= 0)
				err = true;

		item = ParamTree_Find(node, "block_level", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].block_level)) <= 0)
				err = true;

		item = ParamTree_Find(node, "block_time", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].block_time)) <= 0)
				err = true;

		item = ParamTree_Find(node, "block_add", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].block_warning_time)) <= 0)
				err = true;

	}

	if (err) {
		LOG_AND_SCREEN("JOURNAL init error! (incorrect event)");
		free(event_data);
		free(events_result);
		return false;
	}

	
	// инициализация данных событий
	journal_event_num_real = 0;
	journal_event_num_int = 0;
	journal_event_num_bool = 0;

	data_node = ParamTree_Find(journal_node, "DATA", PARAM_TREE_SEARCH_NODE);
	if (!data_node) {
		LOG_AND_SCREEN("No JOURNAL DATA!");   // Нет данных к событиям
	}
	else {
		//получение количества данных

		err = false;
		unsigned type, num;

		for (node = ParamTree_Child(data_node); node && !err; node = node->next) {

			item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
			if (!item || !item->value)
				err = true;
			else
				if (sscanf_s(item->value, "%u", &type) <= 0)
					err = true;


			if (!err) {
				item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
				if (!item || !item->value)
					err = true;
				else
					if (sscanf_s(item->value, "%u", &num) <= 0)
						err = true;
			}


			if (!err) {
				switch (type) {
				case LOGIC_TYPE_REAL_OUT:  if (num < math_real_out_num) journal_event_num_real++; else err = true; break;
				case LOGIC_TYPE_INT_OUT:   if (num < math_int_out_num) journal_event_num_int++; else err = true; break;
				case  LOGIC_TYPE_BOOL_OUT: if (num < math_bool_out_num) journal_event_num_bool++; else err = true; break;
				default: err = true; break;
				}
			}
	
		}

		if (err) {
			LOG_AND_SCREEN("JOURNAL data init error! (incorrect data settings)");
			free(event_data);
			free(events_result);
			return false;
		}		

	}

	
	//выделение памяти под указатели на данные
	journal_event_real_myd_ptrs = NULL;
	journal_event_int_myd_ptrs = NULL;
	journal_event_bool_myd_ptrs = NULL;

	err = false;

	if (journal_event_num_real) {
		journal_event_real_myd_ptrs = (float* (*)[])malloc(sizeof(float*)*journal_event_num_real);
		if (!journal_event_real_myd_ptrs)
			err = true;
	}

	if (journal_event_num_int) {
		journal_event_int_myd_ptrs = (int32_t* (*)[])malloc(sizeof(int32_t*)*journal_event_num_int);
		if (!journal_event_int_myd_ptrs)
			err = true;
	}

	if (journal_event_num_bool) {
		journal_event_bool_myd_ptrs = (uint8_t* (*)[])malloc(sizeof(uint8_t*)*journal_event_num_bool);
		if (!journal_event_bool_myd_ptrs)
			err = true;
	}

	if (err) {
		journal_free_all_memory();
		LOG_AND_SCREEN("JOURNAL init error! (data pointers alloc error)");
		return false;
	}

	//заполнение массивов указателей

	if (journal_event_num_real || journal_event_num_int || journal_event_num_bool) {

		err = false;
		unsigned type, num;
		int index_f = 0, index_i = 0, index_b = 0;

		for (node = ParamTree_Child(data_node); node && !err; node = node->next) {

			item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
			if (!item || !item->value)
				err = true;
			else
				if (sscanf_s(item->value, "%u", &type) <= 0)
					err = true;


			if (!err) {
				item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
				if (!item || !item->value)
					err = true;
				else
					if (sscanf_s(item->value, "%u", &num) <= 0)
						err = true;
			}


			if (!err) {
				switch (type) {
				case LOGIC_TYPE_REAL_OUT:  (*journal_event_real_myd_ptrs)[index_f++] = &math_real_out[num]; break;
				case LOGIC_TYPE_INT_OUT:   (*journal_event_int_myd_ptrs)[index_i++] = &math_int_out[num]; break;
				case  LOGIC_TYPE_BOOL_OUT: (*journal_event_bool_myd_ptrs)[index_b++] = &math_bool_out[num]; break;
				default: err = true; break;
				}
			}

		}

		if (err) {
			LOG_AND_SCREEN("JOURNAL data init internal error!");
			journal_free_all_memory();
			return false;
		}


	}


	//подсчет смещений для последующего быстрого доступа

	journal_event_offset_result = sizeof(journal_event_t);
	journal_event_offset_real = journal_event_offset_result + events_num;
	journal_event_offset_int = journal_event_offset_real + journal_event_num_real*4;
	journal_event_offset_bool = journal_event_offset_int + journal_event_num_int*4;
	
	journal_event_size = journal_event_offset_bool + journal_event_num_bool;

	//размер делаем кратным 4
	while (journal_event_size % 4)
		journal_event_size++;
	//-----------------------------------------------------------------------

   
	//выделение памяти под кольцевое хранилище событий
	journal_events = (uint8_t*)calloc(EVENTS_BUF_SIZE, journal_event_size);
	if (!journal_events) {
		journal_free_all_memory();
		LOG_AND_SCREEN("JOURNAL init error! (alloc journal_events)");
		return false;
	}
	journal_events_num = EVENTS_BUF_SIZE;
	journal_events_head = 0;
	journal_events_tail = 0;

	LOG_AND_SCREEN("JOURNAL memory: %.1f MBytes", (EVENTS_BUF_SIZE*journal_event_size)/(float)(1024*1024));


	//определение длины сообщения
	journal_msg_size = sizeof(msg_type_journal_event_t) + events_num + (journal_event_num_real * 4) + (journal_event_num_int * 4) + journal_event_num_bool;
	
	//получение уникального номера событий
	journal_event_unique_id = launchnum_get();
	journal_event_unique_id <<= 32;


	//прием запросов на удаление и отправку событий
	net_add_dispatcher((uint8_t)NET_MSG_JOURNAL_REQUEST, journal_request_callback);


	journal_init_ok = true;


	return true;
}


#define JOURNAL_EDGE_RISE  0
#define JOURNAL_EDGE_FALL  1
#define JOURNAL_EDGE_ANY   2

#define JOURNAL_NO_EVENT       0   // нет события
#define JOURNAL_EVENT_RISE     1   // передний фронт
#define JOURNAL_EVENT_FALL     2   // задний фронт
#define JOURNAL_EVENT_BLOCK    3   // заблокировано
#define JOURNAL_EVENT_FORCED   4   // принудительное добавление заблокированного события по истечении времени block_warning_time и возникновении события снова



static void journal_add() {

	if (!journal_init_ok)
		return;


	journal_event_data_t* ptr = event_data;
	uint8_t* res_ptr = events_result;
	bool trig = false;

	//для каждого события
	while (ptr != event_data_end) {

		*res_ptr = JOURNAL_NO_EVENT;  //по-умолчанию нет никаких изменений в результатах

		//получение текущего значения флага события
		ptr->flag = *(ptr->value_ptr);

		//обнаружение изменения флага события
		ptr->change = ptr->prev_flag ^ ptr->flag;
		ptr->prev_flag = ptr->flag;

		//снятие флага изменения если фронт изменения не соответствует настройкам события
		if (ptr->change) {
			if ( ((ptr->flag == 1) && (ptr->edge == JOURNAL_EDGE_FALL)) || ((ptr->flag == 0) && (ptr->edge == JOURNAL_EDGE_RISE)))
				ptr->change = 0;
			else
				if(!ptr->lock)
					trig = true;  //обнаружено новое событие для добавления
		}


		//есть новое событие
		if (ptr->change) {  //есть изменение события

			if (ptr->lock) {  //если уже было заблокировано

				ptr->timer = ptr->block_time;

				if (ptr->block_warning_timer < ptr->block_warning_time)  //если событие заблокированно не очень долго (меньше block_warning_time), то оно игнорируется
					ptr->change = 0;    //удаляем изменение для ранее заблокированного события
				else {  //событие слишком долго было заблокированным и вот возникло опять. добавляем принудительно
					ptr->block_warning_timer = 0;
					trig = true;
					*res_ptr = JOURNAL_EVENT_FORCED;
				}

			}
			else {  //событие не было заблокировано

				ptr->timer += ptr->increment;   //инкримент счетчика при возникновении события
				if (ptr->timer >= ptr->block_level) {  //если счетчик достиг уровня блокирвки, то блокируем событие
					ptr->timer = ptr->block_time;
					ptr->lock = 1;
				}

				//обновление состояния
				if(ptr->lock)
					*res_ptr = JOURNAL_EVENT_BLOCK;
				else *res_ptr = (ptr->flag) ? JOURNAL_EVENT_RISE : JOURNAL_EVENT_FALL;

			}

		}
		else {  // нет нового события

			if (ptr->timer) {  //если таймер события не нулевой

				if (ptr->lock) {   //если событие уже ранее заблокировано, то просто уменьшаю на единицу значение таймера
					ptr->timer--;
					//если блокировка была и счетчик досчитал до нуля, то убираю блокировку, обнуляю счетчик до предупреждения о слишком долгой блокировке
					if (ptr->timer == 0) {
						ptr->timer = 0;
						ptr->lock = 0;
						ptr->block_warning_timer = 0;
					}
				}
				else  //если блокировки нет, то уменьшаю на заданный в настройках декремент
					ptr->timer = (ptr->timer > ptr->decrement) ? (ptr->timer - ptr->decrement) : 0;

			}

		}


		//обновление счетчиков предупреждений о слишком долгой блокировке
		if (ptr->lock) {
			if (ptr->block_warning_timer<ptr->block_warning_time)
				ptr->block_warning_timer++;
		}
		else
			ptr->block_warning_timer = 0;


		ptr++;
		res_ptr++;
	}

	
	DEBUG_ADD_POINT(30);


	//если никаких изменений по событиям нет, то на этом работа закончена
	if (!trig)
		return;

	DEBUG_ADD_POINT(31);

	//добавление состояния событий в очередь
	int n = journal_events_head;
	if (journal_events_head == journal_events_tail) {
		//нет записей в кольцевом буфере
		n = journal_events_head;
		journal_events_head++;
		if (journal_events_head == journal_events_num)
			journal_events_head = 0;
	}
	else {
		journal_events_head++;
		if (journal_events_head == journal_events_num)
			journal_events_head = 0;
		if (journal_events_head == journal_events_tail) {
			journal_events_head = n;   //очередь полностью забита. добавлять некуда. возвращаем указатель бошки на прошлую позицию и уходим отсюда нафиг
			return;
		}
		n = journal_events_head;
	}

	DEBUG_ADD_POINT(32);

	//заполнение структуры события
	journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size*n);
	p->unique_id = journal_event_unique_id++;
	p->time = get_time();

	//копирование состояний событий
	memcpy((uint8_t*)p + journal_event_offset_result, events_result, events_num);

	DEBUG_ADD_POINT(33);

	//копирование данных real
	if (journal_event_num_real) {
		float* dst_ptr = (float*)((uint8_t*)p + journal_event_offset_real);
		float* dst_ptr_end = dst_ptr + journal_event_num_real;
		float** src_ptr = &(*journal_event_real_myd_ptrs)[0];
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	//копирование данных int
	if (journal_event_num_int) {
		int32_t* dst_ptr = (int32_t*)((uint8_t*)p + journal_event_offset_int);
		int32_t* dst_ptr_end = dst_ptr + journal_event_num_int;
		int32_t** src_ptr = &(*journal_event_int_myd_ptrs)[0];
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	//копирование данных bool
	if (journal_event_num_bool) {
		uint8_t* dst_ptr = (uint8_t*)((uint8_t*)p + journal_event_offset_bool);
		uint8_t* dst_ptr_end = dst_ptr + journal_event_num_bool;
		uint8_t** src_ptr = &(*journal_event_bool_myd_ptrs)[0];
		unsigned i = 0;
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	DEBUG_ADD_POINT(34);
	
}

//заполнение сообщения с событием на отправку
void journal_fill_msg(net_msg_t* msg, int index) {
	DEBUG_ADD_POINT(217);

	journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size * index);

	msg->type = (uint8_t)NET_MSG_JOURNAL_EVENT;
	msg->subtype = 0;
	msg_type_journal_event_t* event_msg = (msg_type_journal_event_t*)&msg->data[0];

	memcpy(event_msg->md5_hash, settings_header->hash, 16);
	event_msg->unique_id = p->unique_id;
	event_msg->time = p->time;

	//состояния событий
	event_msg->n_of_events = events_num;
	event_msg->events_offset = sizeof(msg_type_journal_event_t);
	memcpy((char*)event_msg + event_msg->events_offset, (uint8_t*)p + journal_event_offset_result, events_num);

	//данные real
	event_msg->n_of_data_real = journal_event_num_real;
	event_msg->data_real_offset = event_msg->events_offset + events_num;
	memcpy((char*)event_msg + event_msg->data_real_offset, (uint8_t*)p + journal_event_offset_real, journal_event_num_real * 4);

	//данные int
	event_msg->n_of_data_int = journal_event_num_int;
	event_msg->data_int_offset = event_msg->data_real_offset + journal_event_num_real * 4;
	memcpy((char*)event_msg + event_msg->data_int_offset, (uint8_t*)p + journal_event_offset_int, journal_event_num_int * 4);

	//данные bool
	event_msg->n_of_data_bool = journal_event_num_bool;
	event_msg->data_bool_offset = event_msg->data_int_offset + journal_event_num_int * 4;
	memcpy((char*)event_msg + event_msg->data_bool_offset, (uint8_t*)p + journal_event_offset_bool, journal_event_num_bool);

	DEBUG_ADD_POINT(218);

}



#define JOURNAL_MAX_MSGS_PER_CYCLE  20

void journal_update() {

	/******************************************/
#if 0
	//отладка журнала
	
	/*
		if (MATH_IO_BOOL_OUT[6])
			MATH_IO_BOOL_OUT[6] = 0;
		else
			MATH_IO_BOOL_OUT[6] = 1;

		if (MATH_IO_BOOL_OUT[5])
			MATH_IO_BOOL_OUT[5] = 0;
		else
			MATH_IO_BOOL_OUT[5] = 1;
	*/
	static bool block = true;

	static unsigned ccc = 0;
	ccc++;

	if (ccc == 100000) {
		ccc = 0;
		block = !block;
	}

	if (block) {
		if (MATH_IO_BOOL_OUT[4])
			MATH_IO_BOOL_OUT[4] = 0;
		else
			MATH_IO_BOOL_OUT[4] = 1;
	}

	journal_add();

	
#endif
	/******************************************/

	
	if (!journal_init_ok)
		return;

	static int last_time = 0;
	bool update = false;
	int new_time = steady_clock_get();

	if (steady_clock_expired(last_time, new_time, JOURNAL_PERIOD_MS * 1000)) {
		last_time = new_time;
		update = true;
	}

	DEBUG_ADD_POINT(204);

	//уходим, если нет соединений - некому отправлять
	if (net_connections() == 0)
		return;

	
	int head = atom_get_state(&journal_events_head);
	int tail = atom_get_state(&journal_events_tail);
	unsigned ev_num = (tail <= head) ? (head - tail) : (journal_events_num - tail + head);
	
	if (update) {
		DEBUG_ADD_POINT(205);

		//отправляем периодическое информационное сообщение
		net_msg_t* msg = net_get_msg_buf(sizeof(msg_type_journal_info_t));
		if (msg) {
			DEBUG_ADD_POINT(206);

			msg->type = (uint8_t)NET_MSG_JOURNAL_INFO;
			msg->subtype = 0;
			msg_type_journal_info_t* info_msg = (msg_type_journal_info_t*)&msg->data[0];

			memcpy(info_msg->md5_hash, settings_header->hash, 16);
			info_msg->events_num = ev_num;
			if (ev_num) {
				journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size * head);
				info_msg->head_id = p->unique_id;
				p = (journal_event_t*)(journal_events + journal_event_size * tail);
				info_msg->tail_id = p->unique_id;
			}
			DEBUG_ADD_POINT(207);

			net_send_msg(msg, NET_PRIORITY_LOWEST, NET_BROADCAST_CHANNEL);
		}
	}


	DEBUG_ADD_POINT(208);

	//если есть новые неотправленные события, то отправляю их (но не более JOURNAL_MAX_MSGS_PER_CYCLE)
	if (!ev_num)
		return;

	static int32_t last_sent_index = INT32_MAX;

	unsigned msgs_to_send = JOURNAL_MAX_MSGS_PER_CYCLE;


	while (last_sent_index != head && msgs_to_send) {
		DEBUG_ADD_POINT(209);

		msgs_to_send--;

		if (last_sent_index == INT32_MAX)
			last_sent_index = tail;

		int index = last_sent_index + 1;
		if (index == journal_events_num)
			index = 0;	

		net_msg_t* msg = net_get_msg_buf(journal_msg_size);
		if (msg) {
			DEBUG_ADD_POINT(210);

			journal_fill_msg(msg, index);

			//попытка отправить
			if (NET_ERR_NO_ERROR != net_send_msg(msg, NET_PRIORITY_MEDIUM, NET_BROADCAST_CHANNEL))
				break;
			else {  //отправилось сообщение,  инкремент указателя на следующее для отправки
				last_sent_index = index;
			}

			DEBUG_ADD_POINT(211);

		}
		else break;

	}
	
}


void journal_request_callback(net_msg_t* msg, uint64_t channel) {

	DEBUG_ADD_POINT(212);

	if (msg->size < sizeof(msg_type_journal_request_t))
		return;

	msg_type_journal_request_t* request = (msg_type_journal_request_t*)&msg->data[0];

	int head = atom_get_state(&journal_events_head);
	int tail = atom_get_state(&journal_events_tail);
	unsigned ev_num = (tail <= head) ? (head - tail) : (journal_events_num - tail + head);

	if (!ev_num)
		return;

	DEBUG_ADD_POINT(213);

	journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size * head);
	if (p->unique_id < request->event_id)
		return;
	p = (journal_event_t*)(journal_events + journal_event_size * tail);
	if (p->unique_id > request->event_id)
		return;

	DEBUG_ADD_POINT(214);

	//поиск требуемого события
	int index = tail;
	while (index != tail) {  //поиск требуемого события
		p = (journal_event_t*)(journal_events + journal_event_size * index);
		if (p->unique_id == request->event_id)
			break;
		index++;
		if (index == journal_events_num)
			index = 0;
	}
	if (index == tail)  //не найден по какой-то причине
		return;

	DEBUG_ADD_POINT(215);

	if (request->request == MSG_JOURNAL_REQUEST_GET) {

		//отправляем найденное событие
		size_t msg_size = sizeof(msg_type_journal_event_t) + events_num + (journal_event_num_real * 4) + (journal_event_num_int * 4) + journal_event_num_bool;


		if (net_msg_buf_get_available_space(msg) < sizeof(journal_msg_size)) {
			msg = net_get_msg_buf(journal_msg_size);
			if (!msg)
				return;
		}

		DEBUG_ADD_POINT(216);

		journal_fill_msg(msg, index);

		net_send_msg(msg, NET_PRIORITY_MEDIUM, NET_BROADCAST_CHANNEL);

		return;
	}


	if (request->request == MSG_JOURNAL_REQUEST_DELETE) {
		DEBUG_ADD_POINT(219);

		//удаляем все события до найденого, включая и его тоже
		index++;
		if (index == journal_events_num)
			index = 0;

		atom_set_state(&journal_events_tail, index);

		return;
	}

	return;
}


//------------------------------------------------------------------------
#ifdef DELTA_HMI_ENABLE

#define DELTA_MAX_REGS_TO_SEND   80   //всегда должно быть кратно 2

#define DELTA_REAL_START_REG     100
#define DELTA_INT_START_REG      500
#define DELTA_BOOL_START_REG     900

#define DELTA_PERIOD_MS          1000   //период отправки данных


static bool delta_HMI_init_flag = false;

float     *delta_HMI_f;
int32_t   *delta_HMI_i;
uint8_t   *delta_HMI_b;

void delta_HMI_init_regs() {

	delta_HMI_f = NULL;
	delta_HMI_i = NULL;
	delta_HMI_b = NULL;


	delta_HMI_f = (float*) malloc(math_real_out_num * 4);
	delta_HMI_i = (int32_t*) malloc(math_int_out_num * 4);
	delta_HMI_b = (uint8_t*) malloc(math_bool_out_num);

	if (delta_HMI_f == NULL || delta_HMI_i == NULL || delta_HMI_b == NULL) {
		if (delta_HMI_f) free(delta_HMI_f);
		if (delta_HMI_i) free(delta_HMI_i);
		if (delta_HMI_b) free(delta_HMI_b);
		return;
	}

	delta_HMI_init_flag = true;
}

//копирование свежих индикаторов
void delta_HMI_copy_indi(const net_msg_t* indi_msg) {

	if (!delta_HMI_init_flag || !indi_msg)
		return;

	DEBUG_ADD_POINT(358);

	msg_type_indi_t* p = (msg_type_indi_t*)&indi_msg->data;

	memcpy(delta_HMI_f, (uint8_t*)p + p->out_real_offset, math_real_out_num * 4);
	memcpy(delta_HMI_i, (uint8_t*)p + p->out_int_offset, math_int_out_num * 4);
	memcpy(delta_HMI_b, (uint8_t*)p + p->out_bool_offset, math_bool_out_num);

}


void delta_HMI_set_regs(uint16_t* ptr, uint16_t* start_reg, uint16_t* num) {

	if (!delta_HMI_init_flag) {
		*num = 0;
		return;
	}

	DEBUG_ADD_POINT(359);

	//  запуск цикла отправки
	static int last_time = 0;
	static bool update = false;

	if (!update) {
		int new_time = steady_clock_get();

		if (steady_clock_expired(last_time, new_time, DELTA_PERIOD_MS * 1000)) {
			last_time = new_time;
			update = true;
		}
		else {

			*num = 0;  //ничего не отправляем до следующего цикла отправки
			return;
		}
	}
	//------------------

	DEBUG_ADD_POINT(360);

	static unsigned type = 0;
	static unsigned n = 0;

	unsigned num_to_copy=0;
	
	switch (type) {
	case 0:
		DEBUG_ADD_POINT(361);
		if ((math_real_out_num - n) <= DELTA_MAX_REGS_TO_SEND / 2)
			num_to_copy = (math_real_out_num - n);
		else
			num_to_copy = DELTA_MAX_REGS_TO_SEND / 2;

		for (unsigned i = n; i < n + num_to_copy; i++, ptr += 2)
			*(float*)ptr = delta_HMI_f[i];

		*num = num_to_copy * 2;
		*start_reg = DELTA_REAL_START_REG + n * 2;

		n += num_to_copy;
		if (n >= math_real_out_num) {
			type = 1;
			n = 0;
		}

		break;

	case 1:
		DEBUG_ADD_POINT(362);
		if ((math_int_out_num - n) <= DELTA_MAX_REGS_TO_SEND / 2)
			num_to_copy = (math_int_out_num - n);
		else
			num_to_copy = DELTA_MAX_REGS_TO_SEND / 2;

		for (unsigned i = n; i < n + num_to_copy; i++, ptr += 2)
			*(int32_t*)ptr = delta_HMI_i[i];

		*num = num_to_copy * 2;
		*start_reg = DELTA_INT_START_REG + n * 2;

		n += num_to_copy;
		if (n >= math_int_out_num) {
			type = 2;
			n = 0;
		}

		break;

	case 2:
		DEBUG_ADD_POINT(363);
	    {
		unsigned need_regs = (math_bool_out_num - n);
		if (need_regs) {
			need_regs--;
			need_regs /= 16;
			need_regs++;
		}

		if (need_regs <= DELTA_MAX_REGS_TO_SEND)
			num_to_copy = (math_bool_out_num - n);
		else {
			num_to_copy = DELTA_MAX_REGS_TO_SEND * 16;
			need_regs = DELTA_MAX_REGS_TO_SEND;
		}


		uint16_t mask = 0x0001;
		for (unsigned i = n; i < n + num_to_copy; i++){
			
			if(mask==0x0001)  //тереть именно здесь, чтоб не вылезти за максимально допустимый регистр DELTA_MAX_REGS_TO_SEND
				*ptr = 0;

			if (delta_HMI_b[i])
				*ptr |= mask;

			mask <<= 1;
			if (!mask) {
				mask = 0x0001;
				ptr++;
			}
			
		}

		*num = need_regs;
		*start_reg = DELTA_BOOL_START_REG + n / 16;
		
		n += num_to_copy;
		if (n >= math_bool_out_num) {
			type = 0;
			n = 0;

			update = false;    //закончили один цикл отправки всего
		}


	    }

		break;

	default:
		*num = 0;
		break;
	}



#if 0
	static unsigned part = 0;
	part++;
	if (part == 3)
		part = 0;

	static int32_t i = 0;
	static float f = 0.0f;
	static uint16_t b = 0xE7AA;


	switch (part) {
	case 0: 
		f += 0.11f;
		*(float*)ptr = f;
		*num = 2;
		*start_reg = 310;
		break;
	case 1:
		i++;
		*(int32_t*)ptr = i;
		*num = 2;
		*start_reg = 210;
		break;
	case 2:
		b = ((b << 1) | ((b & 0x8000)?0x01:0x00));
		*ptr = b;
		*num = 1;
		*start_reg = 110;
		break;
	default:
		*num = 0;
	}

#endif


}
#endif





//основная периодическая функция сурзы

static void MAIN_LOGIC_PERIOD_FUNC() {

	DEBUG_ADD_POINT(21);

	steady_clock_update((int)SurzaPeriod());


	dic_read();


	// ====== вызов МЯДа  ==================
	DEBUG_ADD_POINT(22);
    MYD_step();
	DEBUG_ADD_POINT(23);
	// =====================================


	#if 0
	static unsigned cnt = 0;
	cnt++;
	if (cnt % 4000 == 0) {
		//for (unsigned i = 0; i < math_bool_out_num; i++)
		unsigned n = cnt / 4000;
		n = n % math_bool_out_num;
		MATH_IO_BOOL_OUT[n] = MATH_IO_BOOL_OUT[n] ? 0 : 1;
	}

	if (cnt == 25000) {
		*(int*)0x0 = 123;
	}

	#endif


#if 0
	/********************************************/
	//ОТЛАДКА ЖУРНАЛА
	static bool flag = true;

	static float f[3] = { 0,0,0 };
	static int i = 0;
	static bool b[2] = { false , false };

	f[0] += 0.1f;  f[1] += 0.2f;  f[2] += 0.3f;
	i++;

	if (i % 2) {
		b[0] = !b[0];  b[1] = !b[1];
	}


	MATH_IO_REAL_OUT[2] = f[0];
	MATH_IO_REAL_OUT[5] = f[1];
	MATH_IO_REAL_OUT[6] = f[2];

	MATH_IO_INT_OUT[2] = i;

	MATH_IO_BOOL_OUT[3] = b[0];
	MATH_IO_BOOL_OUT[0] = b[1];
	


	flag = !flag;

	if (flag)
		MATH_IO_BOOL_OUT[4] = 0;
	else
		MATH_IO_BOOL_OUT[4] = 1;
		
	
	/********************************************/
#endif

	DEBUG_ADD_POINT(24);
	dic_write();

	DEBUG_ADD_POINT(25);
	fiu_write();

	DEBUG_ADD_POINT(26);
	indi_copy();

	DEBUG_ADD_POINT(27);
	params_update();

	DEBUG_ADD_POINT(28);
	journal_add();


	DEBUG_ADD_POINT(29);
	/***************/
	//ВРЕМЕННО !!!  чтение измерителя шага , пока нет сделано это через конфигурацию
	RTInW(0x440);
	/***************/

}

