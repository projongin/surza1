
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

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


#include "math\MYD.h" 
#include "math\rtwtypes.h"


#define SETTINGS_FILENAME  "settings.bin"
#define FIRMWARE_FILENAME  "Surza.RTA"
#define PARAMS_FILENAME    "params.dat"


#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint32_t size;            //������ ������
	uint32_t crc32;           //����������� ����� �� data[size]
	uint8_t  hash[16];        //MD5 ��� �� data[size]
	// data[size]
} settings_file_header_t;
#pragma pack(pop)


// ��������� �� �������� �������� � �����
static settings_file_header_t* settings_header = NULL;   
static settings_file_header_t* strings_header = NULL;

//��������� �� ���������� ������ ��� �������� ��������
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


	//������ ����� ��������

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) return;
	
	filesystem_fragment_t fragments[2];

	fragments[0].size = sizeof(settings_file_header_t);
	fragments[1].size = 0;

	if (filesystem_read_file_fragments(SETTINGS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR) return;

	settings_file_header_t* s_header = (settings_file_header_t*)fragments[0].pointer;
	char* s_data_ptr = fragments[1].pointer;

	//�������� ��������� ������
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


	//�������� ��������� �� ���������

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

    //������ ����� �� ����

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Filesystem i/o error!");
		return;
	}

	//������� ������� "������ ������"
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

			if (!init_params()) {
				LOG_AND_SCREEN("PARAMS init failed!");
				break;
			}
			

			ok = true;
			break;
		}
		
	}

	//����� ����� ���������������� ������ �������� � ����� ������
	net_add_dispatcher((uint8_t)NET_MSG_SURZA_SETTINGS, settings_recv_callback);

	//����� �������� �� ��������� �������� ����� ���������
	net_add_dispatcher((uint8_t)NET_MSG_SETTINGS_REQUEST, settings_request_callback);

	//����� ����� ����� ��������
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

	//������ ����� ��������
	if (filesystem_read_file_fragments(SETTINGS_FILENAME, fragments, 2) != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Load settings file error!");
		return -1;
	}

	settings_header = (settings_file_header_t*) fragments[0].pointer;
	settings_data_ptr = fragments[1].pointer;

	//�������� ��������� ������
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
//  �������� �������
//------------------------------------------------------------------------

#define MATH_IO_REAL_IN   MYD_U.In_Real
#define MATH_IO_INT_IN    MYD_U.In_Int
#define MATH_IO_BOOL_IN   MYD_U.In_Boolean
#define MATH_IO_REAL_OUT  MYD_Y.Out_Real
#define MATH_IO_INT_OUT   MYD_Y.Out_Int
#define MATH_IO_BOOL_OUT  MYD_Y.Out_Boolean



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


	//������������� ����
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

static unsigned base_offset[6];   //�������� ������ ������ �� ����� �� ������ ��������� (msg_type_indi_t)

static unsigned indi_msg_size;    //������ ������������ ��������� (msg_type_indi_t + ������)


//����������� ���������� ���������� ���������� ���� ����������� � ����� ���������� (����� �����)
//�������� ����������� ������ ������ 4 !!! (����� ����� �������� ������, ��� ��� ���� �������� ����� �������� ������� �� ������ ������)
#define INDI_PART_SIZE  64


static bool init_indi() {

	indi_init_ok = false;
	indi_msg = NULL;
	indi_msg_fill_new_data = 0;


	unsigned bytes_num[6];  //���������� ������ ��� ����������� �� ����� � ������
	bytes_num[0] = math_real_in_num * 4;
	bytes_num[1] = math_int_in_num * 4;
	bytes_num[2] = math_bool_in_num;
	bytes_num[3] = math_real_out_num * 4;
	bytes_num[4] = math_int_out_num * 4;
	bytes_num[5] = math_bool_out_num;

	void* src_ptr[6];    //��������� �� ��������� ������
	src_ptr[0] = math_real_in;
	src_ptr[1] = math_int_in;
	src_ptr[2] = math_bool_in;
	src_ptr[3] = math_real_out;
	src_ptr[4] = math_int_out;
	src_ptr[5] = math_bool_out;
	
	//�������� ������ ������ ������ ����� �� ������ ���������
	base_offset[0] = sizeof(msg_type_indi_t);
	for (int i = 1; i < 6; i++)
		base_offset[i] = base_offset[i - 1] + bytes_num[i-1];

	indi_msg_size = base_offset[5] + bytes_num[5];

	//����������� ���-�� ������ ��� ����������� ��� ������� ����
	unsigned parts[6];
	for (int i = 0; i < 6; i++)
		parts[i] = (bytes_num[i] / INDI_PART_SIZE) + ((bytes_num[i] % INDI_PART_SIZE) ? 1 : 0);

	indi_steps = 0;
	unsigned n;

	for (n = 0, indi_steps = 0; n < 6; n++)
		indi_steps += parts[n];

	//��������� ������ ��� �������
	indi_step_pointers = (struct indi_step_t*) malloc(sizeof(struct indi_step_t)*indi_steps);
	if (!indi_step_pointers)
		return false;


	//���������� �������
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
	case 0:   //������� � ��������� ����� ���������
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

			//����������� ��������� ���������
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

			//���� ������������� ��������� ������
			atom_set_state(&indi_msg_fill_new_data, 1);
			state = 1;
		}

		break;

	case 1:   //�������� ���������� ������

		if (!atom_get_state(&indi_msg_fill_new_data)) {
			//������ ��������� ���������, �������� ���������

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

	//���������� ������� ���� � ������� �����

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


	//��������� �����
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 8; j++)
			adc_table[i][j] = NULL;
		adc_ch_num[i] = 0;
	}
	adc_num = 1;


	//���������� ������� (���� ������)
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


		//���������� ������ ���

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

	//������ ������� ���
	if(adc_num>1)
		ai8s_start_second_adc();

	//������ ������� ��� 1
	for (unsigned i = 0; i < 8; i++)
		if(adc_table[0][i])
			*(adc_table[0][i]) = ai8s_read_ch(0, i);

	
	//����� ���������� ������� 7�� ������ (���� �� ��� �������� �� �����)
	if(!adc_table[0][7])
		(void)ai8s_read_ch(0, 7);


	//�������� � ������ ������� ������� ���
	if (adc_num > 1) {
		if (ai8s_wait_second_adc()) {
			for (unsigned i = 0; i < 8; i++)
				if (adc_table[1][i])
					*(adc_table[1][i]) = ai8s_read_ch(1, i);
		}
		else {  //� ������ ������ ������ ����� ��� ��������� ������������� ����������
			for (unsigned i = 0; i < 8; i++)
				if (adc_table[1][i])
					*(adc_table[1][i]) = 0;
		}
	}

	 
	//���������� ������
	wdt_update();

	//������ �������� ������� ������
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

	//���������� ������ ����� DIC

	item = ParamTree_Find(node, "DIC", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &dic_adr) <= 0)
		return false;


	//����� ���� ������ � �������
	param_tree_node_t *in_node, *out_node;

	in_node = ParamTree_Find(ParamTree_MainNode(), "DIC_IN", PARAM_TREE_SEARCH_NODE);
	out_node = ParamTree_Find(ParamTree_MainNode(), "DIC_OUT", PARAM_TREE_SEARCH_NODE);

	if (!in_node && !out_node)
		return true;    //DIC �� ������������



	//�������� ������� ����� DIC
	if (RTIn(dic_adr + 11) != 'g') {
		LOG_AND_SCREEN("DIC:  NO DIC!!! dic adress: 0x%04X", dic_adr);
		return false;
	}


	//���������� ������
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

	//���������� �������
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


	
	// ��������� ����� DIC  � ������������ � ����������� �������������

	//��� ������ ������� � ����������� ���������
	for(int i=0; i<12; i++)
		RTOut(dic_adr+dic_adr_regs[i], 0);

	//���������������� �� ����\�����
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

	//��� ������ ������� � ����������� ��������� (������ ��� �� ���� ������)
	for (int i = 0; i<12; i++)
		RTOut(dic_adr + dic_adr_regs[i], 0);
	

	//��������� ����������� ������������
	RTOut(dic_adr + 15, 0x01);
	RTOut(dic_adr + 0, 0x08);  //320��
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

	//���������� ������� ���� ���

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
		return true;   // ��� �� ������������
	

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
		return true;  //��� ����������� �������


	//�������� ������� ���1 � ���2
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
		return true;   // ��������� �� ������������
	}



	unsigned n = ParamTree_ChildNum(param_node);
	if (!n) {
		LOG_AND_SCREEN("No logic params (0)!");
		return true;   // ��� ����������
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
	

	//������ ����� ���������� 
	
	if (!params_read_file()) {
		LOG_AND_SCREEN("Default params apply !!!");
		for(unsigned i=0; i<params_num; i++)
			params_ptr[i].val.i32 = params_ptr[i].val_default.i32;
		params_save_file();
	}
	

	//���������� ���� ���������� �� ����� ����
	params_apply_all();


	//����� ��������� ��� ��������� ����������
	net_add_dispatcher((uint8_t)SURZA_SET_PARAM, params_recv_callback);

	return true;
}


static void params_apply_all() {
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
	uint32_t crc32;           //����������� ����� �� data[params_num*sizeof(param_value_t)]
	uint8_t  hash[16];        //MD5 ��� ��������������� ������������
	uint32_t params_num;      //������ ������
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

	//�������� ��������� ������
	if (!header
		|| !val_ptr
		|| header->params_num != fragments[1].size / sizeof(param_value_t)
		|| header->params_num != params_num
		|| !crc32_check((char*)val_ptr, fragments[1].size, header->crc32)
		|| memcmp(header->hash, settings_header->hash, 16) )
		ret = false;
	else {
	
		//������ � ������� ����������� ���������� � �� ��������
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

	//������������ ���������� ������
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

	if (msg->size < sizeof(msg_type_set_param_t))
		return;

	msg_type_set_param_t* p_msg = (msg_type_set_param_t*)&msg->data[0];

	if (memcmp(p_msg->hash, settings_header->hash, 16)) {
		LOG_AND_SCREEN("Can't apply param - configuration does not match!");
		return;
	}

	if (p_msg->num>=params_num) {
		if (p_msg->num==0xffff) {  //reset all to default
			for (unsigned i = 0; i < params_num; i++)
				params_ptr[i].val.i32 = params_ptr[i].val_default.i32;
		}
		else {

			LOG_AND_SCREEN("Can't apply param - wrong param num!");
			return;
		}
	}

	bool ok = true;

	if(p_msg->num!=0xffff)
	switch (params_ptr[p_msg->num].type) {
	case PARAM_TYPE_FLOAT: 
		if (!_finite(p_msg->value.f32)
			|| p_msg->value.f32<params_ptr[p_msg->num].val_min.f32
			|| p_msg->value.f32>params_ptr[p_msg->num].val_max.f32) {
			ok = false;
		} else {
			params_ptr[p_msg->num].val.f32 = p_msg->value.f32;
		}
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

	if (!ok) {
		LOG_AND_SCREEN("Can't apply param - wrong value!");
		return;
	}
	
	if (!params_save_file())
		return;


	if (atom_get_state(&params_update_param_flag))
		return;

	if (p_msg->num == 0xffff)
		params_update_num = 0xffff;
	else {
		params_update_type = params_ptr[p_msg->num].type;
		params_update_num = params_ptr[p_msg->num].num;
		params_update_val.i32 = params_ptr[p_msg->num].val.i32;
	}
	
	atom_set_state(&params_update_param_flag, 1);

    return;
}


static void params_update() {

	if (!params_update_param_flag)
		return;

	params_update_param_flag = 0;

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





//�������� ������������� ������� �����

static void MAIN_LOGIC_PERIOD_FUNC() {

	steady_clock_update((int)SurzaPeriod());


	dic_read();


	// ====== ����� ����  ==================
	MYD_step();
	// =====================================


	dic_write();

	fiu_write();

	indi_copy();

	params_update();

}

