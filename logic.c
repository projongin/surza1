
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
	uint32_t crc32;           //контрольна€ сумма по data[size]
	uint8_t  hash[16];        //MD5 хэш по data[size]
	// data[size]
} settings_file_header_t;
#pragma pack(pop)


// указатели на описание настроек и строк
static settings_file_header_t* settings_header = NULL;   
static settings_file_header_t* strings_header = NULL;

//указатели на выделенную пам€ть под хранение настроек
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


	//заполн€ю сообщение им отправл€ю

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



bool init_math();
bool init_adc();



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
//  ќсновные таблицы
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

	



	//******************
	math_int_in_num = 30;
	math_int_in = &tmp_int_in[0];
	//*******************

	return true;
}

//------------------------------------------------------------------------

msg_type_indi_t* indi_msg;
volatile int indi_msg_rdy;
volatile int indi_msg_fill_new_data;


static void indi_init() {
	indi_msg = NULL;
	indi_msg_rdy = 0;
	indi_msg_fill_new_data = 0;

}


#define INDI_PART_SIZE  100   //максимально допустимое количество копируемых байт индикаторов в одном прерывании (такте сурзы)

static void indi_copy() {

	static int type = 0;
	static int part = 0;

	switch (type) {

	case 0:
		if (!atom_get_state(&indi_msg_fill_new_data))
			return;
		else
			if(part==0)
				atom_set_state(&indi_msg_fill_new_data, 0);


		if (math_real_in_num) {
			if ((math_real_in_num << 2) > INDI_PART_SIZE) {
				memcpy((char*)indi_msg + indi_msg->in_real_offset + INDI_PART_SIZE * part, (char*)math_real_in + INDI_PART_SIZE * part, INDI_PART_SIZE);
				part++;
			}
			else {
				memcpy((char*)indi_msg + indi_msg->in_real_offset, math_real_in, math_real_in_num*4);
				part = 0;
				type++;
			}
			break;
	    }
		type++;
		part = 0;

	case 1:   //продолжить далее все остальные кейсы по подобию
		if (math_int_in_num) {
			memcpy((char*)indi_msg + indi_msg->in_int_offset, math_int_in, math_int_in_num * 4);
			break;
		}
		type++;
		part = 0;

	case 2:
		if (math_bool_in_num) {
			memcpy((char*)indi_msg + indi_msg->in_bool_offset, math_bool_in, math_bool_in_num);
			break;
		}
		type++;

	case 3:
		if (math_real_out_num) {
			memcpy((char*)indi_msg + indi_msg->out_real_offset, math_real_out, math_real_out_num * 4);
			break;
		}
		type++;

	case 4:
		if (math_int_out_num) {
			memcpy((char*)indi_msg + indi_msg->out_int_offset, math_int_out, math_int_out_num * 4);
			break;
		}
		type++;

	case 5:
		if (math_bool_out_num) {
			memcpy((char*)indi_msg + indi_msg->out_bool_offset, math_bool_out, math_bool_out_num);
			break;
		}
		type++;

		break;
	
	}

	if (type==6) {
		type = 0;
		part = 0;

		atom_set_state(&indi_msg_rdy, 1);
	}

}

bool indi_send() {

	return false;
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

	
	//сброс прерывани€ чтением 7го канала (если не был прочитан до этого)
	if(!adc_table[0][7])
		(void)ai8s_read_ch(0, 7);


	//ожидание и чтение каналов второго ацп
	if (adc_num > 1) {
		if (ai8s_wait_second_adc()) {
			for (unsigned i = 0; i < 8; i++)
				if (adc_table[1][i])
					*(adc_table[1][i]) = ai8s_read_ch(1, i);
		}
		else {  //в случае отказа второй платы все измерени€ принудительно занул€ютс€
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




//основна€ периодическа€ функци€ сурзы

static void MAIN_LOGIC_PERIOD_FUNC() {

	steady_clock_update((int)SurzaPeriod());



}

