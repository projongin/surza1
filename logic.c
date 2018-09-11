
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <Rtk32.h>


#include "logic.h"
#include "net.h"
#include "net_messages.h"
#include "log.h"
#include "filesystem.h"
#include "crc32.h"
#include "common.h"
#include "param_tree.h"
#include "ai8s.h"



#define SETTINGS_FILENAME  "settings.bin"
#define FIRMWARE_FILENAME  "Surza.RTA"


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


int logic_init() {

	bool ok = false;

	if (init_flags.settings_init) {

		//init logic
		while (true) {

			if (!init_math()) {
				LOG_AND_SCREEN("Logic main i/o tables init failed!");
				break;
			}
			
			if (!init_adc()) {
				LOG_AND_SCREEN("ADC init failed!");
				break;
			}
			
			if (!init_dic()) {
				LOG_AND_SCREEN("DIC init failed!");
				break;
			}

			if (!init_fiu()) {
				LOG_AND_SCREEN("FIU init failed!");
				break;
			}
			
			if (!init_indi()) {
				LOG_AND_SCREEN("INDI init failed!");
				break;
			}
			

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
float   tmp_float_in[30];
int32_t tmp_int_in[30];
uint8_t tmp_bool_in[30];
float   tmp_float_out[30];
int32_t tmp_int_out[30];
uint8_t tmp_bool_out[30];


#define MATH_IO_REAL_IN   tmp_float_in
#define MATH_IO_INT_IN    tmp_int_in
#define MATH_IO_BOOL_IN   tmp_bool_in
#define MATH_IO_REAL_OUT  tmp_float_out
#define MATH_IO_INT_OUT   tmp_int_out
#define MATH_IO_BOOL_OUT  tmp_bool_out



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


	static int state = 0;
	static net_msg_t* msg = NULL;


	switch (state) {
	case 0:   //создать и заполнить новое сообщение
		if (update) {
			update = false;

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

			indi_msg->time = 0;

			//флаг необходимости заполнить данные
			atom_set_state(&indi_msg_fill_new_data, 1);
			state = 1;
		}

		break;

	case 1:   //ожидание заполнения данных

		if (!atom_get_state(&indi_msg_fill_new_data)) {
			//данные сообщения заполнены, отправка сообщения

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

#define FIU_SETTINGS_OFFSET  (32)

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
	if (board_en[0] && RTIn(fiu1_adr + FIU_SETTINGS_OFFSET) != FIU_CODE) {
		fiu_table_size = 0;
		LOG_AND_SCREEN("FIU_1:  NO FIU!! fiu adress: 0x%04X", fiu1_adr);
		return false;
	}

	if (board_en[1] && RTIn(fiu2_adr + FIU_SETTINGS_OFFSET) != FIU_CODE) {
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





//основная периодическая функция сурзы

static void MAIN_LOGIC_PERIOD_FUNC() {

	steady_clock_update((int)SurzaPeriod());


	dic_read();


	// STEP      STEP      STEP      STEP      STEP

	/***************/
	memcpy(tmp_float_out, tmp_float_in, sizeof(tmp_float_out));
	memcpy(tmp_int_out, tmp_int_in, sizeof(tmp_int_out));
	memcpy(tmp_bool_out, tmp_bool_in, sizeof(tmp_bool_out));
	/***************/


	dic_write();

	fiu_write();

	indi_copy();

}

