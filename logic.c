
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#include <Rtk32.h>
#include <Rtfiles.h>
#include <rttarget.h>

#include "Clock.h"

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
#include "buf_pool.h"


#ifdef DELTA_HMI_ENABLE
#include "delta_hmi.h"

void delta_HMI_init_regs();
void delta_HMI_copy_indi(const net_msg_t*);
void delta_HMI_set_regs(uint16_t* ptr, uint16_t* start_reg, uint16_t* num);
#endif


#include "math\MYD.h" 
#include "math\rtwtypes.h"


#define SETTINGS_FILENAME  "settings.bin"
#define FIRMWARE_FILENAME  "Surza.rtb"
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
	DEBUG_ADD_POINT(320);

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
	//reboot();
	RTReboot();
}


void settings_request_callback(net_msg_t* msg, uint64_t channel) {
	DEBUG_ADD_POINT(321);

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


	//�������� ��������� � ���������

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
	DEBUG_ADD_POINT(322);
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

	if (filesystem_write_file(FIRMWARE_FILENAME, ((char*)f_msg)+f_msg->data_offset,f_msg->bytes) != FILESYSTEM_NO_ERR) {
		LOG_AND_SCREEN("Error! Write file \"%s\" failed!", FIRMWARE_FILENAME);
		return;
	}


	//������ �������� �� ����

	//������ ��� �����
	int buf_size = 0xffff;

	uint8_t* buf = malloc(buf_size);
	if (!buf) {
		LOG_AND_SCREEN("Error! Memory allocation failed!");
		return;
	}

	int res = RTMakeBootDisk('C', -1, FIRMWARE_FILENAME, buf, buf_size, 0);
	free(buf);

	//�������� rtb ����� 
	filesystem_delete_file(FIRMWARE_FILENAME);

	if (res!=RT_BDISK_SUCCESS) {
		LOG_AND_SCREEN("Error! RTMakeBootDisk() return %d", res);
		return;
	}	


	LOG_AND_SCREEN("Reboot...");
	//reboot();
	RTReboot();
}


void steptime_start_check();


static bool init_math();
static bool init_adc();
static bool init_indi();
static bool init_dic();
static bool init_fiu();
static bool init_params();
static bool init_journal();
static bool init_oscilloscope();
static bool init_debug_osc();
static bool init_set_inputs();
static bool init_steptime();
static bool init_shu();
static bool init_commands();
static bool init_isa();


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

			if (!init_oscilloscope()) {
				LOG_AND_SCREEN("OSCILLOSCOPE init failed!");
				break;
			}
			
		    DEBUG_ADD_POINT(308);

			if (!init_set_inputs()) {
				LOG_AND_SCREEN("SET_INPUTS init failed!");
				break;
			}

			DEBUG_ADD_POINT(309);

			//������ �����������
            #ifdef DELTA_HMI_ENABLE
			delta_HMI_init_regs();
			delta_hmi_open(delta_HMI_set_regs);
            #endif

			//������������� �������
			time_init();
			
		    DEBUG_ADD_POINT(310);

			//������������� ���������� ����
			if (!init_steptime()) {
				LOG_AND_SCREEN("STEP_TIME init failed!");
				break;
			}

			//������������� ��
			if (!init_shu()) {
				LOG_AND_SCREEN("SHU init failed!");
				break;
			}
			
			//������������� ������
			if (!init_commands()) {
				LOG_AND_SCREEN("COMMANDS init failed!");
				break;
			}

			//������������� ������ � ����� ISA �� ����
			if (!init_isa()) {
				LOG_AND_SCREEN("ISA init failed!");
				break;
			}

			//���������� �����������
			if (!init_debug_osc()) {
				LOG_AND_SCREEN("DEBUG OSCILLOSCOPE init failed!");
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
	

	DEBUG_ADD_POINT(311);

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

	DEBUG_ADD_POINT(200);

	static int state = 0;
	static net_msg_t* msg = NULL;


	switch (state) {
	case 0:   //������� � ��������� ����� ���������
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

			indi_msg->time = time_get();
			indi_msg->launch_num = launchnum_get();

			//���� ������������� ��������� ������
			atom_set_state(&indi_msg_fill_new_data, 1);
			state = 1;
		}

		break;

	case 1:   //�������� ���������� ������

		if (!atom_get_state(&indi_msg_fill_new_data)) {
			//������ ��������� ���������, �������� ���������

			DEBUG_ADD_POINT(202);

            #ifdef DELTA_HMI_ENABLE
			  delta_HMI_copy_indi(msg);  //�������� ���������� ��� ������
            #endif

			DEBUG_ADD_POINT(203);

			net_send_msg(msg, NET_PRIORITY_MEDIUM, NET_BROADCAST_CHANNEL);
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

	DEBUG_ADD_POINT(20);

	//������ ��������� ������� (��� ����������� ����������)
	steptime_start_check();

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
static bool dic_en;

static unsigned dic_adr;

static uint8_t dic_dir[12];

static uint8_t* dic_table[96];
//static uint8_t* dic_table_out[96];


#define DIC_DIR_IGNORE  0
#define DIC_DIR_IN      1
#define DIC_DIR_OUT     2

static const uint8_t dic_adr_regs[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};


static bool init_dic() {

	dic_en = false;

	memset(dic_dir, DIC_DIR_IGNORE, 12);

	for (int i = 0; i < 96; i++)
		dic_table[i] = NULL;

	//����� ���� ������ � �������
	param_tree_node_t *in_node, *out_node;

	in_node = ParamTree_Find(ParamTree_MainNode(), "DIC_IN", PARAM_TREE_SEARCH_NODE);
	out_node = ParamTree_Find(ParamTree_MainNode(), "DIC_OUT", PARAM_TREE_SEARCH_NODE);

	if (!in_node && !out_node)
		return true;    //DIC �� ������������


	param_tree_node_t* item;

	//���������� ������
	unsigned in_out_counter = 0;
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

			in_out_counter++;

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

			in_out_counter++;

			item = ParamTree_NextItem(item);
		}
	}

	if (in_out_counter == 0)  //DIC �� ������������
		return true;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (!node)
		return false;

	//���������� ������ ����� DIC

	item = ParamTree_Find(node, "DIC", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		return false;

	if (sscanf_s(item->value, "%u", &dic_adr) <= 0)
		return false;



	//�������� ������� ����� DIC
	if (RTIn(dic_adr + 11) != 'g') {
		LOG_AND_SCREEN("DIC:  NO DIC!!! dic adress: 0x%04X", dic_adr);
		return false;
	}



	
	// ��������� ����� DIC  � ������������ � ����������� �������������

	//��� ������ ������� � ����������� ���������
	for(int i=0; i<12; i++)
		RTOut(dic_adr+dic_adr_regs[i], 0xff);

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
		RTOut(dic_adr + dic_adr_regs[i], 0xff);
	

	//��������� ����������� ������������
	RTOut(dic_adr + 15, 0x01);
	RTOut(dic_adr + 0, 0x08);  //320��
	RTOut(dic_adr + 15, 0x00);

	dic_en = true;

	return true;
}


void dic_read() {

	if (!dic_en)
		return;

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

	if (!dic_en)
		return;

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

	
	param_tree_node_t* node;
	param_tree_node_t* item;
	param_tree_node_t* fiu_node;


	fiu_node = ParamTree_Find(ParamTree_MainNode(), "FIU", PARAM_TREE_SEARCH_NODE);
	if (!fiu_node)
		return true;   // ��� �� ������������


	//���������� ������� ���� ���

	node = ParamTree_Find(ParamTree_MainNode(), "SYSTEM", PARAM_TREE_SEARCH_NODE);
	if (!node)
		return false;

	bool fiu_en[2] = { true, true };

	item = ParamTree_Find(node, "FIU1", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		fiu_en[0] = false;

	if(fiu_en[0])
		if (sscanf_s(item->value, "%u", &fiu1_adr) <= 0)
			return false;

	item = ParamTree_Find(node, "FIU2", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)
		fiu_en[1] = false;

	if(fiu_en[1])
		if (sscanf_s(item->value, "%u", &fiu2_adr) <= 0)
			return false;

	if (!fiu_en[0] && !fiu_en[1])  //��� �� ������������
		return true;


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
			if (sscanf_s(item->value, "%u", &board) <= 0 || board >= 2 || !fiu_en[board])
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
				if (ptr->i32 < 0 || ptr->i32 > 1)
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

#define EVENTS_BUF_SIZE  5000    //������������ ���������� ����������� ������� (���������� ����� ��������� � ����)

typedef struct {
	uint8_t* value_ptr;  // ��������� �� ���� �������
              //��������� �� ������������
	uint32_t increment;
	uint32_t decrement;
	uint32_t block_level;
	uint32_t block_time;
	uint32_t block_warning_time;
	uint32_t edge;
	          //������� ��������� ������ ������� �� ����� ������
	uint32_t flag;       // ������� ��������� ����� �������
	uint32_t prev_flag;  // ����� ������� �� ���������� ������
	uint32_t change;     // ������� ������������� ������� � ������ ������
	uint32_t timer;      // ������� �������
	uint32_t lock;       // ������ ������������\�������������
	uint32_t block_warning_timer; // ������� �� �������������� � ������� ������ ����������
} journal_event_data_t;

static journal_event_data_t* event_data;      //������ ������� �������
static journal_event_data_t* event_data_end;  // = event_data + events_num * sizeof(journal_event_data_t)

static uint8_t* events_result;  //���������� (��������� ������� ����� ��������� � ����� �����)
static unsigned events_num;     //���������� �������


static bool journal_init_ok;

static uint64_t journal_event_unique_id;


#pragma pack(push)
#pragma pack(1) 

typedef struct {
	uint64_t unique_id;
	surza_time_t time;
	//������ ��������� ������� events_result
	//������ ������� ���� REAL
	//������ ������� ���� INT
	//������ ������� ���� BOOL
} journal_event_t;

#pragma pack(pop)

static unsigned journal_event_size;   //������ ���� ��������� journal_event_t, ����������� �� ����� ������������� �������

uint8_t* journal_events;              //��������� ������ ����������� �������
int journal_events_num_max;           //������������ ���-�� ������� ��� ���������� � ��������� �������
volatile int journal_events_num;      //������� ���-�� ������� � �������
volatile int journal_events_head;     //��������� �� ������ � ������. ������ ��������� �� ��������� ����������� ������� (��� journal_events_num!=0)
volatile int journal_events_tail;     //��������� �� ������ �� �������. ������ ��������� �� �������, ����������� � ������� ������ ����� (��� journal_events_num!=0)

//�������� � ������ �� ����� � ������� ������� �� ������  ��������� journal_event_t
static unsigned journal_event_offset_result;
static unsigned journal_event_offset_real;
static unsigned journal_event_offset_int;
static unsigned journal_event_offset_bool;

//���������� ������ ��� �����������
static unsigned journal_event_num_real;
static unsigned journal_event_num_int;
static unsigned journal_event_num_bool;

//��������� �� ������� ���������� �� ������ � �������� �������� ����
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
		return true;   // ������ ������� �� ������������
	}


	events_node = ParamTree_Find(journal_node, "EVENTS", PARAM_TREE_SEARCH_NODE);
	if (!events_node) {
		LOG_AND_SCREEN("No JOURNAL EVENTS!");
		return true;   // ��� ������� � ������� �������
	}
	else {
		events_num  = ParamTree_ChildNum(events_node);
		if (!events_num) {
			LOG_AND_SCREEN("No JOURNAL EVENTS!!");
			return true;   // ��� �������
		}
	}


	//����� � ���������� ���� �������

	 //��������� ������ ��� ������ ������� ������� � �� �����������
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


	//���������� ������ ������� �� ������ ��������

	bool err = false;
	unsigned n = 0;

	for (node = ParamTree_Child(events_node); node && !err; node = node->next, n++) {

		item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)
			err = true;
		else
			if (sscanf_s(item->value, "%u", &(event_data[n].increment)) <= 0 || event_data[n].increment >= math_bool_out_num)  //��������� ���� increment ��� ��������� ����������
				err = true;

		//��������� �� ���� ������� � �������� ��������� ����
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

	
	// ������������� ������ �������
	journal_event_num_real = 0;
	journal_event_num_int = 0;
	journal_event_num_bool = 0;

	data_node = ParamTree_Find(journal_node, "DATA", PARAM_TREE_SEARCH_NODE);
	if (!data_node) {
		LOG_AND_SCREEN("No JOURNAL DATA!");   // ��� ������ � ��������
	}
	else {
		//��������� ���������� ������

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
				case LOGIC_TYPE_BOOL_OUT:  if (num < math_bool_out_num) journal_event_num_bool++; else err = true; break;
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

	
	//��������� ������ ��� ��������� �� ������
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

	//���������� �������� ����������

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


	//������� �������� ��� ������������ �������� �������

	journal_event_offset_result = sizeof(journal_event_t);
	journal_event_offset_real = journal_event_offset_result + events_num;
	journal_event_offset_int = journal_event_offset_real + journal_event_num_real*4;
	journal_event_offset_bool = journal_event_offset_int + journal_event_num_int*4;
	
	journal_event_size = journal_event_offset_bool + journal_event_num_bool;

	//������ ������ ������� 4
	while (journal_event_size % 4)
		journal_event_size++;
	//-----------------------------------------------------------------------

   
	//��������� ������ ��� ��������� ��������� �������
	journal_events = (uint8_t*)calloc(EVENTS_BUF_SIZE, journal_event_size);
	if (!journal_events) {
		journal_free_all_memory();
		LOG_AND_SCREEN("JOURNAL init error! (alloc journal_events)");
		return false;
	}
	journal_events_num_max = EVENTS_BUF_SIZE;
	journal_events_num = 0;
	journal_events_head = 0;
	journal_events_tail = 0;

	LOG_AND_SCREEN("JOURNAL memory: %.1f MBytes", (EVENTS_BUF_SIZE*journal_event_size)/(float)(1024*1024));


	//����������� ����� ���������
	journal_msg_size = sizeof(msg_type_journal_event_t) + events_num + (journal_event_num_real * 4) + (journal_event_num_int * 4) + journal_event_num_bool;
	
	//��������� ����������� ������ �������
	journal_event_unique_id = launchnum_get();
	journal_event_unique_id <<= 32;


	//����� �������� �� �������� � �������� �������
	net_add_dispatcher((uint8_t)NET_MSG_JOURNAL_REQUEST, journal_request_callback);


	journal_init_ok = true;


	return true;
}


#define JOURNAL_EDGE_RISE  0
#define JOURNAL_EDGE_FALL  1
#define JOURNAL_EDGE_ANY   2

#define JOURNAL_NO_EVENT       0   // ��� �������
#define JOURNAL_EVENT_RISE     1   // �������� �����
#define JOURNAL_EVENT_FALL     2   // ������ �����
#define JOURNAL_EVENT_BLOCK    3   // �������������
#define JOURNAL_EVENT_FORCED   4   // �������������� ���������� ���������������� ������� �� ��������� ������� block_warning_time � ������������� ������� �����



static void journal_add() {

	if (!journal_init_ok)
		return;


	journal_event_data_t* ptr = event_data;
	uint8_t* res_ptr = events_result;
	bool trig = false;

	//��� ������� �������
	while (ptr != event_data_end) {

		*res_ptr = JOURNAL_NO_EVENT;  //��-��������� ��� ������� ��������� � �����������

		//��������� �������� �������� ����� �������
		ptr->flag = *(ptr->value_ptr);

		//����������� ��������� ����� �������
		ptr->change = ptr->prev_flag ^ ptr->flag;
		ptr->prev_flag = ptr->flag;

		//������ ����� ��������� ���� ����� ��������� �� ������������� ���������� �������
		if (ptr->change) {
			if ( ((ptr->flag == 1) && (ptr->edge == JOURNAL_EDGE_FALL)) || ((ptr->flag == 0) && (ptr->edge == JOURNAL_EDGE_RISE)))
				ptr->change = 0;
			else
				if(!ptr->lock)
					trig = true;  //���������� ����� ������� ��� ����������
		}


		//���� ����� �������
		if (ptr->change) {  //���� ��������� �������

			if (ptr->lock) {  //���� ��� ���� �������������

				ptr->timer = ptr->block_time;

				if (ptr->block_warning_timer < ptr->block_warning_time)  //���� ������� �������������� �� ����� ����� (������ block_warning_time), �� ��� ������������
					ptr->change = 0;    //������� ��������� ��� ����� ���������������� �������
				else {  //������� ������� ����� ���� ��������������� � ��� �������� �����. ��������� �������������
					ptr->block_warning_timer = 0;
					trig = true;
					*res_ptr = JOURNAL_EVENT_FORCED;
				}

			}
			else {  //������� �� ���� �������������

				ptr->timer += ptr->increment;   //��������� �������� ��� ������������� �������
				if (ptr->timer >= ptr->block_level) {  //���� ������� ������ ������ ���������, �� ��������� �������
					ptr->timer = ptr->block_time;
					ptr->lock = 1;
				}

				//���������� ���������
				if(ptr->lock)
					*res_ptr = JOURNAL_EVENT_BLOCK;
				else *res_ptr = (ptr->flag) ? JOURNAL_EVENT_RISE : JOURNAL_EVENT_FALL;

			}

		}
		else {  // ��� ������ �������

			if (ptr->timer) {  //���� ������ ������� �� �������

				if (ptr->lock) {   //���� ������� ��� ����� �������������, �� ������ �������� �� ������� �������� �������
					ptr->timer--;
					//���� ���������� ���� � ������� �������� �� ����, �� ������ ����������, ������� ������� �� �������������� � ������� ������ ����������
					if (ptr->timer == 0) {
						ptr->timer = 0;
						ptr->lock = 0;
						ptr->block_warning_timer = 0;
					}
				}
				else  //���� ���������� ���, �� �������� �� �������� � ���������� ���������
					ptr->timer = (ptr->timer > ptr->decrement) ? (ptr->timer - ptr->decrement) : 0;

			}

		}


		//���������� ��������� �������������� � ������� ������ ����������
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


	//���� ������� ��������� �� �������� ���, �� �� ���� ������ ���������
	if (!trig)
		return;

	DEBUG_ADD_POINT(31);

	//���������� ��������� ������� � �������

	if (journal_events_num == journal_events_num_max) //������� ��������� ������. ��������� ������. ���������� ��������� ����� �� ������� ������� � ������ ������ �����
		return;
	
	journal_events_head++;
	if (journal_events_head == journal_events_num_max)
		journal_events_head = 0;

	
	journal_events_num++;

	//��������� �����  (������������ ������ ������ ��� �������� ������� �� ������� �� ��������� ��� ������� ������������)
	if (journal_events_num == 1)
		journal_events_tail = journal_events_head;
	

	DEBUG_ADD_POINT(32);

	//���������� ��������� �������
	journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size*journal_events_head);
	p->unique_id = journal_event_unique_id++;
	p->time = time_get();

	//����������� ��������� �������
	memcpy((uint8_t*)p + journal_event_offset_result, events_result, events_num);
	
	DEBUG_ADD_POINT(33);

	//����������� ������ real
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

	//����������� ������ int
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

	//����������� ������ bool
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

//���������� ��������� � �������� �� ��������
void journal_fill_msg(net_msg_t* msg, int index) {
	DEBUG_ADD_POINT(217);

	journal_event_t* p = (journal_event_t*)(journal_events + journal_event_size * index);

	msg->type = (uint8_t)NET_MSG_JOURNAL_EVENT;
	msg->subtype = 0;
	msg_type_journal_event_t* event_msg = (msg_type_journal_event_t*)&msg->data[0];

	memcpy(event_msg->md5_hash, settings_header->hash, 16);
	event_msg->unique_id = p->unique_id;
	event_msg->time = p->time;

	//��������� �������
	event_msg->n_of_events = events_num;
	event_msg->events_offset = sizeof(msg_type_journal_event_t);
	memcpy((char*)event_msg + event_msg->events_offset, (uint8_t*)p + journal_event_offset_result, events_num);

	//������ real
	event_msg->n_of_data_real = journal_event_num_real;
	event_msg->data_real_offset = event_msg->events_offset + events_num;
	memcpy((char*)event_msg + event_msg->data_real_offset, (uint8_t*)p + journal_event_offset_real, journal_event_num_real * 4);

	//������ int
	event_msg->n_of_data_int = journal_event_num_int;
	event_msg->data_int_offset = event_msg->data_real_offset + journal_event_num_real * 4;
	memcpy((char*)event_msg + event_msg->data_int_offset, (uint8_t*)p + journal_event_offset_int, journal_event_num_int * 4);

	//������ bool
	event_msg->n_of_data_bool = journal_event_num_bool;
	event_msg->data_bool_offset = event_msg->data_int_offset + journal_event_num_int * 4;
	memcpy((char*)event_msg + event_msg->data_bool_offset, (uint8_t*)p + journal_event_offset_bool, journal_event_num_bool);

	DEBUG_ADD_POINT(218);

}



#define JOURNAL_MAX_MSGS_PER_CYCLE  20

void journal_update() {

	/******************************************/
#if 0
	//������� �������
	
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

	//������, ���� ��� ���������� - ������ ����������
	if (net_connections() == 0)
		return;

	global_spinlock_lock();
	int head = journal_events_head;
	int tail = journal_events_tail;
	unsigned ev_num = (unsigned) journal_events_num;
	global_spinlock_unlock();
	
	if (update) {
		DEBUG_ADD_POINT(205);

		//���������� ������������� �������������� ���������
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

	//���� ���� ����� �������������� �������, �� ��������� �� (�� �� ����� JOURNAL_MAX_MSGS_PER_CYCLE)
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
		if (index == journal_events_num_max)
			index = 0;	

		net_msg_t* msg = net_get_msg_buf(journal_msg_size);
		if (!msg)
			break;

		DEBUG_ADD_POINT(210);

		journal_fill_msg(msg, index);

		//������� ���������
		if (NET_ERR_NO_ERROR != net_send_msg(msg, NET_PRIORITY_MEDIUM, NET_BROADCAST_CHANNEL))
			break;
		else {  //����������� ���������,  ��������� ��������� �� ��������� ��� ��������
			last_sent_index = index;
		}

		DEBUG_ADD_POINT(211);

	}
	
}


void journal_request_callback(net_msg_t* msg, uint64_t channel) {

	DEBUG_ADD_POINT(212);

	if (msg->size < sizeof(msg_type_journal_request_t))
		return;

	msg_type_journal_request_t* request = (msg_type_journal_request_t*)&msg->data[0];

	global_spinlock_lock();
	int head = journal_events_head;
	int tail = journal_events_tail;
	unsigned ev_num = (unsigned) journal_events_num;
	global_spinlock_unlock();

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

	//����� ���������� �������
	int index = tail;
	while(index != head){  //����� ���������� �������
		p = (journal_event_t*)(journal_events + journal_event_size * index);
		if (p->unique_id == request->event_id)
			break;
		index++;
		if (index == journal_events_num_max)
			index = 0;
	}
	if (index == head && ((journal_event_t*)(journal_events + journal_event_size * index))->unique_id != request->event_id)  //�� ������ �� �����-�� �������
		return;

	DEBUG_ADD_POINT(215);

	if (request->request == MSG_JOURNAL_REQUEST_GET) {

		//���������� ��������� �������
		
		if (net_msg_buf_get_available_space(msg) < journal_msg_size) {
			msg = net_get_msg_buf(journal_msg_size);
			if (!msg)
				return;
		}
		else {
			msg->size = journal_msg_size;
		}

		DEBUG_ADD_POINT(216);

		journal_fill_msg(msg, index);

		net_send_msg(msg, NET_PRIORITY_MEDIUM, channel);

		return;
	}


	if (request->request == MSG_JOURNAL_REQUEST_DELETE) {
		DEBUG_ADD_POINT(219);

		//������� ��� ������� �� ���������, ������� � ��� ����
		int new_tail = index+1;
		if (new_tail == journal_events_num_max)
			new_tail = 0;

		global_spinlock_lock();
		journal_events_tail = new_tail;
		if (index == head)
			journal_events_num = 0; //������� ���������
		else
			journal_events_num = (head >= new_tail) ? head - new_tail + 1 : journal_events_num_max - new_tail + head + 1;
		global_spinlock_unlock();

		return;
	}

	return;
}




//=======================================================================
//     �����������
//=======================================================================

#define  OSC_MAX_MEMORY_USAGE  (1024*1024*128)    //������ ���� ������� ��� ������ � ���������������

#define  OSC_MAX_LENGTH_FACTOR  20                //����������� ������������ ����� ������������� (���������� ��� ���-�� ����������� �������������) � ������ ������������� ������ ��� ����������

#define  OSC_INTERNAL_HEADERS_MAX  OSC_MAX_LENGTH_FACTOR     //������������ ���������� ������������ � ������ ������������ (��� ������� + ������� �����������)

#define  OSC_MIN_LENGTH_MSEC   20
#define  OSC_MAX_LENGTH_MSEC   10000

#define  OSC_TRIGGER_PERCENT_MIN   1
#define  OSC_TRIGGER_PERCENT_MAX   99

#define  OSC_NET_MSG_SIZE_MAX      (1024*1024)   //������������ ������ ������ ������������� ��� �������� � ����� ��������� (��������� ������������� �� ����� �������� �� ����� OSC_NET_MSG_SIZE_MAX)


static bool oscilloscope_init_ok;

static unsigned osc_min_length_msec;   //���������� ����� ������������� � ������������� (�� ��������)
static unsigned osc_min_length;        //���������� ���������� ����� � �������������, ������������� �� �� ���������� ����� � ������� ����� �����

static unsigned osc_trigger_percent;   //����� �������� � ��������� (�� ��������)
static unsigned osc_trigger_point;     //������������� ���-�� ����� �����������

static unsigned osc_trigger_num;       //�������.  �������� ����� BOOL

static unsigned osc_data_num;          //����� ���-�� ������

	   //���������� ������ ��� �����������
static unsigned osc_num_real;
static unsigned osc_num_int;
static unsigned osc_num_bool;  //������ +1 ���� � ���������� �� ������������ ��� �������� ��������

       //��������� �� ������� ���������� �� ������ � �������� �������� ����
static float*   (*osc_real_myd_ptrs)[];
static int32_t* (*osc_int_myd_ptrs)[];
static uint8_t* (*osc_bool_myd_ptrs)[];

//�������� � ������ �� ������ ������� ���� � ������ ������ ������� ������������ (��� ��������� ������ � ����������)
static unsigned osc_record_offset_real;
static unsigned osc_record_offset_int;
static unsigned osc_record_offset_bool;

//������ ������ ������ ������� ������������
static unsigned osc_record_size;

//������ ������ ������ ������� ������������ �� ����������� ������������ � ��������� �������� (������ ������)
static unsigned osc_record_size_raw;

//����� ���������� ��������� ������� ��� ������������
static unsigned osc_bufs_total;

// ����� ���� �������
static int osc_buf_pool_num;

//������ ��� ����������� ������������� ���������� �������������
//���������� �������
static unsigned osc_ch_num;
//������� ������ ������������ ������ ������ ������������ ������ ����� ������ � �������
static unsigned* osc_ch_data_offset;
//������� ������ ������������ ������ ������ ������������ ���� ����� ������
static uint8_t* osc_ch_data_type;
//----------------------------------------

typedef struct {
	void*        first;         //��������� �� ������ �����
	void*        last;          //��������� �� ��������� �����
	unsigned     num;           //���������� ������� � �������
	unsigned     n_to_complete; //���������� ������� ����������� ��� ���������� �������������
	unsigned     first_trigger; //����� ������� ������� ������������ ��������
	surza_time_t time;          //����� ������������ ������� ��������
	uint64_t     id;            //���������� ���������� ����� �������������
	unsigned     finished;      //������� ���������� ������������� (��� ������ ��������, ������ � ��������)
	//��������� ���� ������������ ������ ��� �������� �������������
	unsigned     next_part;  //����� �����, ��������� ��� ��������� ��������
	void*        next_buf;   //��������� �� �����, ��������� ��� ��������� �������� ����� next_part
} osc_internal_header_t;


//������ ������������ (������� �����������, ������� � �������� � ������� ������������)
static osc_internal_header_t  osc_internal_headers[OSC_INTERNAL_HEADERS_MAX];
static unsigned osc_internal_headers_head;
static unsigned osc_internal_headers_tail;

static uint64_t osc_id;   //������� ���������� ������� ������������

//----  ������������ ������ ��� �������� �������������
static struct oscilloscope_header_t osc_header;
static osc_internal_header_t* osc_internal_header_to_send;
static bool osc_header_en;
//-------------------------------------------------------

//�������� ����������� ������������ ����� � ���������. ������������� �� OSC_NET_MSG_SIZE_MAX,
//�����������  ������  osc_record_size_raw (��� ��������� � ���������� ��������� �� ������� ������� ��� �������� ������ �������������)
static unsigned osc_part_size_max;
static unsigned osc_part_size_max_bufs;  //�� �� �����, �� ���������� � ���������� ������� (������ �������� osc_record_size_raw)


//������ ��� ��������� ��������� ��� �������������
void oscilloscope_net_callback(net_msg_t* msg, uint64_t channel);


static void osc_free_memory() {
	if (osc_real_myd_ptrs) free(osc_real_myd_ptrs);
	if (osc_int_myd_ptrs)  free(osc_int_myd_ptrs);
	if (osc_bool_myd_ptrs) free(osc_bool_myd_ptrs);
	if (osc_ch_data_offset) free(osc_ch_data_offset);
	if (osc_ch_data_type) free(osc_ch_data_type);
	osc_real_myd_ptrs = NULL;
	osc_int_myd_ptrs = NULL;
	osc_bool_myd_ptrs = NULL;
	osc_ch_data_offset = NULL;
	osc_ch_data_type = NULL;
}


static bool init_oscilloscope() {

	oscilloscope_init_ok = false;


	param_tree_node_t* osc_node = ParamTree_Find(ParamTree_MainNode(), "OSCILLOSCOPE", PARAM_TREE_SEARCH_NODE);
	if (!osc_node) {
		LOG_AND_SCREEN("No OSCILLOSCOPE!");
		return true;   // ����������� ������� �� ������������
	}

	//���������� ����� ���������� ������������
	param_tree_node_t* item;


	item = ParamTree_Find(osc_node, "length_msec", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)  return false;
	if (sscanf_s(item->value, "%u", &osc_min_length_msec) <= 0) return false;
	if (osc_min_length_msec<OSC_MIN_LENGTH_MSEC || osc_min_length_msec>OSC_MAX_LENGTH_MSEC) return false;

	item = ParamTree_Find(osc_node, "trigger_point", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)  return false;
	if (sscanf_s(item->value, "%u", &osc_trigger_percent) <= 0) return false;
	if (osc_trigger_percent < OSC_TRIGGER_PERCENT_MIN)
		osc_trigger_percent = OSC_TRIGGER_PERCENT_MIN;
	if (osc_trigger_percent > OSC_TRIGGER_PERCENT_MAX)
		osc_trigger_percent = OSC_TRIGGER_PERCENT_MAX;

	item = ParamTree_Find(osc_node, "trigger_num", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)  return false;
	if (sscanf_s(item->value, "%u", &osc_trigger_num) <= 0) return false;
	if (osc_trigger_num >= math_bool_out_num) return false;


	//����������� ���������� ������

	param_tree_node_t* data_node = ParamTree_Find(osc_node, "DATA", PARAM_TREE_SEARCH_NODE);
	if (!data_node) {
		LOG_AND_SCREEN("NO OSCILLOSCOPE DATA SECTION!");
		return true;
	}
	else {
		osc_data_num = ParamTree_ChildNum(data_node);
		if (!osc_data_num) {
			LOG_AND_SCREEN("NO OSCILLOSCOPE DATA!");
			return true;
		}
	}



	// ����� � ������������� ������ ������������
	osc_num_real = 0;
	osc_num_int = 0;
	osc_num_bool = 0;

	
	//��������� ���������� ������
	param_tree_node_t* node;
	bool err = false;
	unsigned type, num;

	for (node = ParamTree_Child(data_node); node && !err; node = node->next) {

		item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)	err = true;
		else if (sscanf_s(item->value, "%u", &type) <= 0) err = true;


		if (!err) {
			item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
			if (!item || !item->value)	err = true;
			else if (sscanf_s(item->value, "%u", &num) <= 0) err = true;
		}


		if (!err) {
			switch (type) {
			case LOGIC_TYPE_REAL_OUT:  if (num < math_real_out_num) osc_num_real++; else err = true; break;
			case LOGIC_TYPE_INT_OUT:   if (num < math_int_out_num)  osc_num_int++; else err = true; break;
			case LOGIC_TYPE_BOOL_OUT:  if (num < math_bool_out_num) osc_num_bool++; else err = true; break;
			default: err = true; break;
			}
		}

	}

	if (err) {
		LOG_AND_SCREEN("OSCILLOSCOPE data init error! (incorrect data settings)");
		return false;
	}

	// ���� �������������� bool ��� �������� �������� ��������
	osc_num_bool++;


	//��������� ������ ������� ���������� �� ������
	osc_real_myd_ptrs = NULL;
	osc_int_myd_ptrs = NULL;
	osc_bool_myd_ptrs = NULL;

	err = false;

	if (osc_num_real) {
		osc_real_myd_ptrs = (float* (*)[])malloc(sizeof(float*)*osc_num_real);
		if (!osc_real_myd_ptrs)
			err = true;
	}

	if (osc_num_int) {
		osc_int_myd_ptrs = (int32_t* (*)[])malloc(sizeof(int32_t*)*osc_num_int);
		if (!osc_int_myd_ptrs)
			err = true;
	}

	if (osc_num_bool) {
		osc_bool_myd_ptrs = (uint8_t* (*)[])malloc(sizeof(uint8_t*)*osc_num_bool);
		if (!osc_bool_myd_ptrs)
			err = true;
	}

	//��������� ������ ��� ������� ������ ����������� ������ ������ ������������ ������ ����� ������ � ������� (������������ ���������� �������������)

	osc_ch_num = osc_num_real + osc_num_int + osc_num_bool;

	osc_ch_data_offset = (unsigned*)malloc(sizeof(unsigned)*osc_ch_num);
	if (!osc_ch_data_offset)
		err = true;

	osc_ch_data_type = (uint8_t*)malloc(osc_ch_num);
	if (!osc_ch_data_type)
		err = true;


	if (err) {
		osc_free_memory();
		LOG_AND_SCREEN("OSCILLOSCOPE init error! (data pointers alloc error)");
		return false;
	}


	//������� �������� ��� ������������ �������� ������� � ����������
	osc_record_offset_real = 4;  //������ 4 ����� �������� ��������� �� ��������� ����� � �������, ����� NULL
	osc_record_offset_int = osc_record_offset_real + osc_num_real * 4;
	osc_record_offset_bool = osc_record_offset_int + osc_num_int * 4;

	osc_record_size = osc_record_offset_bool + osc_num_bool;

	osc_record_size_raw = (osc_num_real * 4 + osc_num_int * 4 + osc_num_bool);

	//������ ������ ������� 4
	while (osc_record_size % 4)
		osc_record_size++;


	//���������� �������� ����������

	err = false;
	int index_f = 0, index_i = 0, index_b = 0;
	unsigned ch_num = 0;

	for (node = ParamTree_Child(data_node); node && !err; node = node->next, ch_num++) {

		item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
		if (!item || !item->value)	err = true;
		else if (sscanf_s(item->value, "%u", &type) <= 0) err = true;


		if (!err) {
			item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);
			if (!item || !item->value)	err = true;
			else if (sscanf_s(item->value, "%u", &num) <= 0) err = true;
		}

		osc_ch_data_type[ch_num] = (uint8_t) type;

		//�������� ��������� � ������������ ������ ������ ������������ � ������������ � �������� �� ������ ������ � �������
		if (!err) {
			switch (type) {
			case LOGIC_TYPE_REAL_OUT:  osc_ch_data_offset[ch_num] = osc_record_offset_real + index_f * 4;
				                       (*osc_real_myd_ptrs)[index_f++] = &math_real_out[num]; break;
			case LOGIC_TYPE_INT_OUT:   osc_ch_data_offset[ch_num] = osc_record_offset_int + index_i * 4;
				                       (*osc_int_myd_ptrs)[index_i++] = &math_int_out[num]; break;
			case LOGIC_TYPE_BOOL_OUT:  osc_ch_data_offset[ch_num] = osc_record_offset_bool + index_b;
				                       (*osc_bool_myd_ptrs)[index_b++] = &math_bool_out[num]; break;
			default: err = true; break;
			}
		}

	}

	if (err) {
		osc_free_memory();
		LOG_AND_SCREEN("OSCILLOSCOPE data init internal error!");
		return false;
	}

	//���������� ��������������� bool ��� ��������
	osc_ch_data_type[ch_num] = (uint8_t)LOGIC_TYPE_BOOL_OUT;
	osc_ch_data_offset[ch_num] = osc_record_offset_bool + index_b;
	(*osc_bool_myd_ptrs)[index_b++] = &math_bool_out[osc_trigger_num];


	

	//����� �������� �������, ����������� ����� ������������ � ������ �����, ����� � �.�.
	
	//������� ������������ ���������� ������� ��� �������������
	osc_min_length = (osc_min_length_msec * 1000) / SurzaPeriod();

	//������� ������ ������ ��������� ����� ������������ �� ������ �������������
	osc_trigger_point = (osc_min_length * osc_trigger_percent) / 100;

	//�������� ���� ������� 
	osc_bufs_total = OSC_MAX_MEMORY_USAGE / osc_record_size;
	//����������� �� ������� ������� ���������� ������� ����� ���� �������� �������� � ��������� ������� �������� �������� ��������� ���������� ������
	if (osc_bufs_total > osc_min_length * OSC_MAX_LENGTH_FACTOR)
		osc_bufs_total = osc_min_length * OSC_MAX_LENGTH_FACTOR;
    
	osc_buf_pool_num = buf_pool_add_pool(osc_record_size, osc_bufs_total);
	if (osc_buf_pool_num<0) {
		osc_free_memory();
		LOG_AND_SCREEN("OSCILLOSCOPE memory allocation error!");
		return false;
	}

	float total_size = (float)(osc_record_size*osc_bufs_total) / (float)(1024 * 1024);
	float total_time = ((float)osc_bufs_total*SurzaPeriod()) / 1000000.0f;
	LOG_AND_SCREEN("OSCILLOSCOPE: memory usage: %.1f Mbytes, max osc length = %.1f seconds", total_size, total_time);
	
	//��������� ������� ������������
	osc_internal_headers_head = 0;
	osc_internal_headers_tail = 0;
	//����� �������� ����������
	osc_internal_headers[osc_internal_headers_head].finished = 0;
	
	
	//���������� �����
	osc_id = launchnum_get();
	osc_id <<= 32;

	//��������� ������� � �������� �������������
	osc_header_en = false;
	osc_internal_header_to_send = NULL;

	//������ �����������  ���������� ������������ ����� � ���������. ������������� �� OSC_NET_MSG_SIZE_MAX,
	//�����������  ������  osc_record_size_raw
	osc_part_size_max_bufs = OSC_NET_MSG_SIZE_MAX / osc_record_size_raw;
	osc_part_size_max = osc_part_size_max_bufs * osc_record_size_raw;

	//����� �������� �� ���� �� �������� � �������� ������������ 
	net_add_dispatcher((uint8_t)NET_MSG_OSCILLOSCOPE, oscilloscope_net_callback);

	oscilloscope_init_ok = true;

	return true;
    
}


//����������� ��������� ������ ����� ����� �����
void osc_copy_data(void* buf) {

	DEBUG_ADD_POINT(221);

	//����������� ������ real
	if (osc_num_real) {
		float* dst_ptr = (float*)((uint8_t*)buf + osc_record_offset_real);
		float* dst_ptr_end = dst_ptr + osc_num_real;
		float** src_ptr = &(*osc_real_myd_ptrs)[0];
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	DEBUG_ADD_POINT(222);

	//����������� ������ int
	if (osc_num_int) {
		int32_t* dst_ptr = (int32_t*)((uint8_t*)buf + osc_record_offset_int);
		int32_t* dst_ptr_end = dst_ptr + osc_num_int;
		int32_t** src_ptr = &(*osc_int_myd_ptrs)[0];
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	DEBUG_ADD_POINT(223);

	//����������� ������ bool
	if (osc_num_bool) {
		uint8_t* dst_ptr = (uint8_t*)((uint8_t*)buf + osc_record_offset_bool);
		uint8_t* dst_ptr_end = dst_ptr + osc_num_bool;
		uint8_t** src_ptr = &(*osc_bool_myd_ptrs)[0];
		unsigned i = 0;
		while (dst_ptr != dst_ptr_end) {
			*dst_ptr = **src_ptr;
			dst_ptr++;
			src_ptr++;
		}
	}

	DEBUG_ADD_POINT(224);

}



#define OSC_ADD_STATE_INITIAL         0
#define OSC_ADD_STATE_WAIT_BUFS       1
#define OSC_ADD_STATE_PREHISTORY      2
#define OSC_ADD_STATE_WAIT_TRIGGER    3
#define OSC_ADD_STATE_HISTORY         4


void oscilloscope_add() {

	if (!oscilloscope_init_ok)
		return;

	//����������� ��������� �� ��������� ������ � �������������
	static void* main_points_last_buf = NULL;

	DEBUG_ADD_POINT(225);

	// ����������� ������������ ��������
	static bool trigger_prev = false;
	bool trigger = false;
	bool new_trigger = math_bool_out[osc_trigger_num];
	if (new_trigger != trigger_prev
		&& new_trigger)
		trigger = true;
	trigger_prev = new_trigger;
	//-----------------------------------------

	DEBUG_ADD_POINT(226);

	static int state = OSC_ADD_STATE_INITIAL;
	bool exit ;
	
	do {
		exit = true;

		switch (state) {
		case OSC_ADD_STATE_INITIAL:
		{
			DEBUG_ADD_POINT(227);

			//���� ���� ��� ������� ������������� (��������� ������ � ������ ����������, ���� ��������� ������ ���� �������
			//������������� � ��������� ������ � ����� ������ ��� �� ���������� (osc_internal_headers_head == osc_internal_headers_tail), �� ���������� ������� finished)
			if(   osc_internal_headers_head != osc_internal_headers_tail
			  || (/*osc_internal_headers_head == osc_internal_headers_tail && */ osc_internal_headers[osc_internal_headers_head].finished)){
				//������� ����� ����� ��������� ���������
				unsigned tmp = osc_internal_headers_head + 1;
				if (tmp == OSC_INTERNAL_HEADERS_MAX)
					tmp = 0;
				if (tmp == osc_internal_headers_tail) //���� ��������� � ����� (��������� ������ �������� ���������������)
					break;
				osc_internal_headers_head = tmp;
		    }

			state = OSC_ADD_STATE_WAIT_BUFS;
		    exit = false;
		}
		    break;

		case OSC_ADD_STATE_WAIT_BUFS:
		{
			//���� ������������ ������� - �� ������ �� ������.
			if (buf_pool_bufs_available_fast(osc_buf_pool_num) < (int)osc_min_length)
				break;

			DEBUG_ADD_POINT(228);

			//��������� ���  ������������� �������, �������� �� ��������� ����������

			osc_internal_header_t* header = &osc_internal_headers[osc_internal_headers_head];
			header->first = NULL;
			header->last = NULL;
			header->num = 0;
			//������������ ����� ������������� + �������������� ����� ������ ����� ����������� (��� ��������� �������������)
			//���� �� ���� ��������� ������������ ��������, �� ��� ���������� �������������� ������ ��������� �� ������� ������������� � ���������� ������������ �����
			header->n_to_complete = osc_min_length + osc_trigger_point;
			header->first_trigger = UINT32_MAX;

			//����� ����� ����������
			header->finished = 0;

			//��������� ���������� � ����������� �������������� ������ � ����� �������������
			main_points_last_buf = NULL;

			//�������� ��������� ���������� �������, ������� � ���������� ���������
			state = OSC_ADD_STATE_PREHISTORY;
			exit = false;
		}
			break;


		case OSC_ADD_STATE_PREHISTORY:
		{
			DEBUG_ADD_POINT(229);

			//��������� ������ ������
			void* new_buf = buf_pool_get_fast(osc_buf_pool_num);  //��� ��������. ������ ����� ������ ���� (������� ������� � OSC_ADD_STATE_PREHISTORY)

			//���������� ������ �������
			osc_copy_data(new_buf);

			//��������� �� ��������� ����� ����� NULL (����� ��������� � �������)
			*(void**)new_buf = NULL;

			//���������� ������ � �������
			osc_internal_header_t* header = &osc_internal_headers[osc_internal_headers_head];

			if (!header->first)
				header->first = new_buf;
			else
				*(void**)header->last = new_buf;  //last ��� �������� �� NULL ��� ��� �� ������ �������� ������ ������ � first

			header->last = new_buf;
			header->num++;
			header->n_to_complete--;

			DEBUG_ADD_POINT(230);
			
			//���� �������������� �������� ������� (�� ���������� ���� �����������)
			if (trigger) {

				DEBUG_ADD_POINT(231);

				//������������� ����������� ��� ������ ���������� �����
				//(����� ����� �������� + ����������� ��� ��������� �������������)
				header->n_to_complete = osc_min_length;

				header->first_trigger = header->num - 1;
				header->time = time_get();

				state = OSC_ADD_STATE_HISTORY;
			}
			else
				if (header->num == osc_trigger_point) {  //���� ��������� ��� �����������, �� ������� � ��������� �������� ��������

					DEBUG_ADD_POINT(232);

					state = OSC_ADD_STATE_WAIT_TRIGGER;
				}


		}
			break;


		case OSC_ADD_STATE_WAIT_TRIGGER:
		{

			DEBUG_ADD_POINT(233);

			osc_internal_header_t* header = &osc_internal_headers[osc_internal_headers_head];

			//������� �� ������� ������ �������� ������
			void* new_buf = header->first;
			header->first = *(void**)new_buf; //��� �������� �� NULL, ��� ��� � ��������� OSC_ADD_STATE_WAIT_TRIGGER ������ ����� ������ ����
			*(void**)new_buf = NULL;          //�������� ���������
			if (header->first == NULL)
				header->last = NULL;


			//���������� ������ �������
			osc_copy_data(new_buf);

			//���������� ������ � �������
			if (!header->first)
				header->first = new_buf;
			else
				*(void**)header->last = new_buf;  //last ��� �������� �� NULL ��� ��� �� ������ �������� ������ ������ � first

			header->last = new_buf;

			DEBUG_ADD_POINT(234);

			//���� �������� �������
			if (trigger) {

				DEBUG_ADD_POINT(235);

				header->first_trigger = header->num - 1;
				header->time = time_get();
				state = OSC_ADD_STATE_HISTORY;
			}
			

		}
			break;


		case OSC_ADD_STATE_HISTORY:
		{
			DEBUG_ADD_POINT(236);

			osc_internal_header_t* header = &osc_internal_headers[osc_internal_headers_head];

			//��������� ������ ������
			void* new_buf = buf_pool_get_fast(osc_buf_pool_num);
			if (!new_buf) {

				DEBUG_ADD_POINT(237);

				//������ �����������. ���������� ������������� ��� ���� � ���� � ����������� ���������
				header->id = osc_id++;
				header->finished = 1;
				state = OSC_ADD_STATE_INITIAL;
				break;
			}

			//���������� ������ �������
			osc_copy_data(new_buf);

			//��������� �� ��������� ����� ����� NULL (����� ��������� � �������)
			*(void**)new_buf = NULL;

			//���������� ������ � �������
			if (!header->first)
				header->first = new_buf;
			else
				*(void**)header->last = new_buf;  //last ��� �������� �� NULL ��� ��� �� ������ �������� ������ ������ � first

			header->last = new_buf;

			header->num++;
			header->n_to_complete--;

			DEBUG_ADD_POINT(238);

			//���������� ��������� �� ��������� ����� ����� ������� �������������� �����
			if(header->n_to_complete == osc_trigger_point)
				main_points_last_buf = new_buf;


			//�������� �� ����� ����������� ������� � ������������� �������� �������������
			if (trigger)
				header->n_to_complete = osc_min_length; // ����� ������������� ����� �������� + �������� �������������� ����������� (�� ����� ��� ����� ������ ����������� ����� ������������� )

			//�������� �� ����� ������ �������������
			if (header->n_to_complete == 0) {  //���������� �������������

				DEBUG_ADD_POINT(239);
				
				header->id = osc_id++;
				header->finished = 1;
				
				//��������� ����� ��������� ��� ��������� ������������� � ������� ����������� � ���
				//��� ���������� ��������� ��� ����� ������������� �������������� ������ �������� � ������� � ��������� � ����������� ��������� (����� �������� ������ ������������� ������� � ����������)

				//������� ����� ����� ��������� ���������
				unsigned tmp = osc_internal_headers_head + 1;
				if (tmp == OSC_INTERNAL_HEADERS_MAX)
					tmp = 0;
				if (tmp == osc_internal_headers_tail) { //���� ��������� � ����� (��������� ������ �������� ���������������)

					//������� � ��������� ���������
					//���� �� ����� �������� ������������ ���������� ������� - ������� ������������������� ������������������
					//(��������� ������� ����� �������������� ������������, ���� ���� �������, �������� ��� ��������� ������)
					state = OSC_ADD_STATE_INITIAL;

				}
				else {

					DEBUG_ADD_POINT(240);

					//������� ����������� � ����� �������������

					osc_internal_header_t* new_header = &osc_internal_headers[tmp];

					//������� � �����
					new_header->first = *(void**)main_points_last_buf;
					new_header->last = header->last;
					new_header->num = osc_trigger_point;
					new_header->n_to_complete = osc_min_length;  //��������� ������ ����������� ����� + �������������� �����������. �� ��� ��� ��� ��� ����, �� ������ ����������� �����
					new_header->first_trigger = UINT32_MAX;
					new_header->finished = 0;

					//�������� �������������� ������ � �������
					header->last = main_points_last_buf;
					*(void**)header->last = NULL;
					header->num -= osc_trigger_point;

					DEBUG_ADD_POINT(241);

					//������������ ������������� �� ����� ���������
					osc_internal_headers_head = tmp;
					main_points_last_buf = NULL;

					//� ����� ������������� ��� ���� ��� �����������, ���� ����� ������������ ��������, �������  � ��������������� ���������
					state = OSC_ADD_STATE_WAIT_TRIGGER;
				}

			}

		}
			break;

		}

	} while (!exit);

}





void oscilloscope_update() {

	if (!oscilloscope_init_ok)
		return;

	//���� ��� ������� ������������� �� ��������, �� �������� �� �������
	if (!osc_header_en) {

		DEBUG_ADD_POINT(242);
		
		bool new_osc_flag = false;

		global_spinlock_lock();
		if (osc_internal_headers_head != osc_internal_headers_tail) //���� ������� ������������� ����� osc_internal_headers_head � osc_internal_headers_tail ����������
			new_osc_flag = true;
		global_spinlock_unlock();

		if (new_osc_flag) {

			DEBUG_ADD_POINT(243);

			//��������� header
			osc_internal_header_t* internal_header = &osc_internal_headers[osc_internal_headers_tail];

			osc_header.id = internal_header->id;
			osc_header.total_length = internal_header->num;
			osc_header.step_time_nsecs = SurzaPeriod()*1000;
			osc_header.num_real = osc_num_real;
			osc_header.num_int = osc_num_int;
			osc_header.num_bool = osc_num_bool;
			osc_header.total_length_bytes = osc_record_size_raw * osc_header.total_length;
			osc_header.parts = (osc_header.total_length_bytes / osc_part_size_max) + ((osc_header.total_length_bytes%osc_part_size_max)?1:0);
			osc_header.time = SurzaTime_sub(internal_header->time, internal_header->first_trigger*osc_header.step_time_nsecs);

			//���������� ����� ��� ��������
			osc_internal_header_to_send = internal_header;
			osc_internal_header_to_send->next_part = 0;
			osc_internal_header_to_send->next_buf = internal_header->first;
			

			//��������� ����������������� ��������� OSCILLOSCOPE_MSG_NEW
			net_msg_t* msg = net_get_msg_buf(sizeof(msg_type_oscilloscope_t)+sizeof(oscilloscope_new_t));
			if (msg) {

				DEBUG_ADD_POINT(244);

				msg->type = (uint8_t)NET_MSG_OSCILLOSCOPE;
				msg->subtype = 0;

				msg_type_oscilloscope_t* osc_msg = (msg_type_oscilloscope_t*)&msg->data[0];
				osc_msg->type = OSCILLOSCOPE_MSG_NEW;
				osc_msg->size = sizeof(oscilloscope_new_t);
				memcpy(osc_msg->md5_hash, settings_header->hash, 16);

				DEBUG_ADD_POINT(245);

				memcpy((char*)(osc_msg+1), &osc_header, sizeof(oscilloscope_new_t));

				DEBUG_ADD_POINT(246);

				net_send_msg(msg, NET_PRIORITY_MEDIUM, NET_BROADCAST_CHANNEL);
			}

			osc_header_en = true;

		}

		DEBUG_ADD_POINT(247);

	}

}

void oscilloscope_net_callback(net_msg_t* msg, uint64_t channel) {

	DEBUG_ADD_POINT(260);

	// �������� ����������� ���������
	if (msg->size < sizeof(msg_type_oscilloscope_t)
		|| msg->type != (uint8_t)NET_MSG_OSCILLOSCOPE)
		return;

	if (!oscilloscope_init_ok)
		return;

	msg_type_oscilloscope_t* osc_msg = (msg_type_oscilloscope_t*)&msg->data[0];
	if (memcmp(osc_msg->md5_hash, settings_header->hash, 16))
		return;

	if (!osc_header_en)
		return;

	DEBUG_ADD_POINT(261);

	//�������� �������
	switch (osc_msg->type) {
	case OSCILLOSCOPE_MSG_REQUEST_NEW:
		{
		    DEBUG_ADD_POINT(262);

			const unsigned msg_size = sizeof(msg_type_oscilloscope_t) + sizeof(oscilloscope_new_t);
			if (net_msg_buf_get_available_space(msg) < msg_size) {
				msg = net_get_msg_buf(msg_size);
				if (!msg)
					return;
			}
			else {
				msg->size = msg_size;
			}

			msg->type = (uint8_t)NET_MSG_OSCILLOSCOPE;
			msg->subtype = 0;

			DEBUG_ADD_POINT(263);

			msg_type_oscilloscope_t* osc_msg = (msg_type_oscilloscope_t*)&msg->data[0];
			osc_msg->type = OSCILLOSCOPE_MSG_NEW;
			osc_msg->size = sizeof(oscilloscope_new_t);
			memcpy(osc_msg->md5_hash, settings_header->hash, 16);

			DEBUG_ADD_POINT(264);


			memcpy((char*)(osc_msg+1), &osc_header, sizeof(oscilloscope_new_t));

			DEBUG_ADD_POINT(265);

			net_send_msg(msg, NET_PRIORITY_MEDIUM, channel);
		}
		break;

	case OSCILLOSCOPE_MSG_REQUEST_DATA:
	    {
		DEBUG_ADD_POINT(266);

		if (osc_msg->size < sizeof(oscilloscope_request_data_t))
			return;

		oscilloscope_request_data_t* request = (oscilloscope_request_data_t*)(osc_msg + 1);
		if (request->id != osc_header.id
			|| request->part >= osc_header.parts )
			return;

		unsigned part = request->part;
		unsigned data_size = (part == osc_header.parts - 1) ? (osc_header.total_length_bytes % osc_part_size_max) : osc_part_size_max;
		const unsigned msg_size = sizeof(msg_type_oscilloscope_t) + sizeof(oscilloscope_data_t) + data_size;

		DEBUG_ADD_POINT(267);

		if (net_msg_buf_get_available_space(msg) < msg_size) {
			msg = net_get_msg_buf(msg_size);
			if (!msg)
				return;
		}
		else {
			msg->size = msg_size;
		}

		
		msg->type = (uint8_t)NET_MSG_OSCILLOSCOPE;
		msg->subtype = 0;

		msg_type_oscilloscope_t* osc_msg = (msg_type_oscilloscope_t*)&msg->data[0];
		osc_msg->type = OSCILLOSCOPE_MSG_DATA;
		osc_msg->size = sizeof(oscilloscope_data_t) + data_size;
		memcpy(osc_msg->md5_hash, settings_header->hash, 16);

		DEBUG_ADD_POINT(268);

		oscilloscope_data_t* osc_data = (oscilloscope_data_t*)(osc_msg + 1);
		osc_data->id = osc_header.id;
		osc_data->part = part;
		osc_data->part_length_bytes = data_size;

		//���������� �������, ������� ���������� �����������
		unsigned bufs_to_copy = data_size / osc_record_size_raw;

		//��������� �� ��������� ����� ��� �����������
		void *buf = osc_internal_header_to_send->next_buf;

		DEBUG_ADD_POINT(269);

		//���� ������������� �� ��������� �� ������� �����
		if (part != osc_internal_header_to_send->next_part) {

			DEBUG_ADD_POINT(270);

			//����� �����, ��������������� ������������� ����� 
			unsigned n_bufs = part * osc_part_size_max_bufs;
			buf = osc_internal_header_to_send->first;

			if (osc_internal_header_to_send->next_part < part) { //����� ������������, �� ����� �����
				//��������� �����, ������� � ��� ���������� ������������ ������
				n_bufs -= osc_internal_header_to_send->next_part * osc_part_size_max_bufs;
				buf = osc_internal_header_to_send->next_buf;
			}

			DEBUG_ADD_POINT(271);
			
			while (n_bufs) {
				n_bufs--;
				if (buf == NULL) {
					net_free_msg_buf(msg);
					return;
				}
				buf = *(void**)buf;
			}
			
		}

		DEBUG_ADD_POINT(272);
		
		//����������� ������
		char* dst_ptr = (char*)(osc_data + 1);
		while (bufs_to_copy) {
			bufs_to_copy--;

			if (buf == NULL) {
				net_free_msg_buf(msg);
				return;
			}
			memcpy(dst_ptr, (char*)buf + osc_record_offset_real, osc_record_size_raw);

			dst_ptr += osc_record_size_raw;
			buf = *(void**)buf;
		}

		DEBUG_ADD_POINT(273);

		//���� ���������� ��������� �����, �� ���������� ����������� ��������� � ����� ����� ��������� �����
		if (part == osc_internal_header_to_send->next_part) {
			osc_internal_header_to_send->next_buf = buf;
			osc_internal_header_to_send->next_part++;
		}


		net_send_msg(msg, NET_PRIORITY_LOW, channel);

	    }
		break;

	case OSCILLOSCOPE_MSG_REQUEST_DELETE:

		DEBUG_ADD_POINT(274);

		if (osc_msg->size < sizeof(oscilloscope_request_delete_t))
			return;

		oscilloscope_request_delete_t * request = (oscilloscope_request_delete_t*)(osc_msg + 1);
		if (request->id != osc_header.id)
			return;

		DEBUG_ADD_POINT(275);

		//���������� ��� ������� ������
		void* buf;
		do {
			
			buf = osc_internal_header_to_send->first;

			osc_internal_header_to_send->first = *(void**)osc_internal_header_to_send->first;

			global_spinlock_lock();
			   buf_pool_free_fast(osc_buf_pool_num, buf);
			global_spinlock_unlock();

		} while (buf != osc_internal_header_to_send->last);

		DEBUG_ADD_POINT(276);
	    		

		//����������� ��������� ������ � ������� ������������
		global_spinlock_lock();
		osc_internal_headers_tail++;
		if (osc_internal_headers_tail == OSC_INTERNAL_HEADERS_MAX)
			osc_internal_headers_tail = 0;
		global_spinlock_unlock();

		DEBUG_ADD_POINT(277);

		//�������, ��� ������ ��� ������� ������������ �������������
		osc_header_en = false;

		break;

	default: return;
	}

}

//=======================================================================


void osc_test_func() {

	static unsigned step = 0;

	static float test_f = 0.0f;
	static int32_t test_i = 0;
	static bool test_b = false;

	/*
	MATH_IO_REAL_OUT[2] = f[0];
	MATH_IO_REAL_OUT[5] = f[1];
	MATH_IO_REAL_OUT[6] = f[2];

	MATH_IO_INT_OUT[2] = i;

	MATH_IO_BOOL_OUT[3] = b[0];
	MATH_IO_BOOL_OUT[0] = b[1];
	*/
	step++;

	test_f += 0.5f;
	if (test_f > 100000.0f)
		test_f = 0.0f;
	test_i++;
	test_b = !test_b;
	

	if ((step % 7000) == 0) {
		MATH_IO_BOOL_OUT[11] = 1;
	}
	else MATH_IO_BOOL_OUT[11] = 0;
	

	MATH_IO_REAL_OUT[17] = test_f;
	MATH_IO_REAL_OUT[7] = test_f + 1.0f;
	MATH_IO_REAL_OUT[3] = test_f + 10.0f;

	MATH_IO_INT_OUT[11] = test_i;

	MATH_IO_BOOL_OUT[7] = test_b;
	MATH_IO_BOOL_OUT[29] = !test_b;

	
    oscilloscope_add();

}





//=======================================================================
//    ���������� �����������
//=======================================================================

#define  DEBUG_OSC_MAX_MEMORY_USAGE  (1024*1024*32)    //������ ���������� ������ ��� ����������� ������������


static bool debug_osc_init_ok;

#define DEBUG_OSC_STATE_WAIT_CONNECTION  0
#define DEBUG_OSC_STATE_ACCUMULATE_DATA  1 
#define DEBUG_OSC_STATE_ACCOMPLISH       2
#define DEBUG_OSC_STATE_WAIT_DISPATCH    3

static int debug_osc_state;


//����� ���������� ������� � ������� � ��������� ������
static unsigned debug_osc_bufs_total;
//����� �������� ������ ��� ��������� ������ ������
static unsigned debug_osc_buf_ptr;
//����� ������ � ������� ���� ��������� ��������� ��� ������
static unsigned debug_osc_buf_last_added_ptr;
//���������� ����������� ������� �� ������� ������
static unsigned debug_osc_bufs_added;
//�������� �� ��������� �����
static void* debug_osc_main_buf;

//������� ������������� � ��������� ����������� ������������ � ��������
static bool debug_osc_connection_en;
//������ ��� �������� ����������
static uint32_t debug_osc_connection_timer;
//�������� ��������
#define DEBUG_OSC_CONNECTION_TIMEOUT_USEC  3000000

//������������� ���������� ����� ������
static bool debug_osc_set_new_trigger;
//������ ������ �������
static debug_osc_trigger_t debug_osc_new_trigger_data;

//���������� id
static uint64_t debug_osc_id;




//������ ��� ��������� �������
static struct trigger_t {
	//��� �������
	unsigned type;
	//����� ������
	unsigned ch_num;
	//��� ������ ������ �������
	unsigned ch_type;
	//�������� �� ������ ������ � ������������ �������
	unsigned ch_offset;
	//���������� ����� ������� �� ����������� ��������
	unsigned trigger_point_num;
	//����� ����� ������������ �������������
	unsigned length;
	//���� �� �������� ���������� �������� �� ������
	bool previous_en;
	//���������� ��������
	union {
		float f32;
		int32_t i32;
	} previous_value;
	//�������
	union {
		float f32;
		int32_t i32;
	} setpoint;
	//����������� �����
	bool continuous_mode;
	//����� ������������ �������
	surza_time_t trigger_time;
	bool dispatched;
} debug_osc_trigger_data;

//������ ��� ��������� ��������� ��� ����������� ������������
void debug_osc_net_callback(net_msg_t* msg, uint64_t channel);


static bool init_debug_osc() {

	debug_osc_init_ok = false;

	if (!oscilloscope_init_ok)
		return true;

	//��������� ������ ��� ��������� �����
	debug_osc_buf_ptr = 0;
	debug_osc_bufs_added = 0;
	unsigned main_size = DEBUG_OSC_MAX_MEMORY_USAGE;
	debug_osc_main_buf = NULL;

	//������� ������� �����
	while (debug_osc_main_buf == NULL && main_size>1024*1024) {
		debug_osc_bufs_total = main_size / osc_record_size;
		debug_osc_main_buf = malloc(debug_osc_bufs_total*osc_record_size);
		if (debug_osc_main_buf == NULL)
			main_size -= (1024 * 1024);
	}

	if (debug_osc_main_buf == NULL) {
		LOG_AND_SCREEN("DEBUG OSCILLOSCOPE memory allocation error!");
		return false;
	} else {
		LOG_AND_SCREEN("DEBUG OSC memory usage: %.1f MBytes, max osc length = %.1f seconds", 
			 (float)(debug_osc_bufs_total*osc_record_size)/(float)(1024*1024),
			 ((float)debug_osc_bufs_total*SurzaPeriod()) / 1000000.0f);
	}

	debug_osc_state = DEBUG_OSC_STATE_WAIT_CONNECTION;

	debug_osc_connection_en = false;

	debug_osc_set_new_trigger = false;

	debug_osc_id = launchnum_get();
	debug_osc_id <<= 32;

	//���������� ������� ��������� ����������� ������������
	net_add_dispatcher((uint8_t)NET_MSG_DEBUG_OSCILLOSCOPE, debug_osc_net_callback);


	debug_osc_init_ok = true;

	return true;
}




void debug_osc_save_previous() {
	char* adr = (char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset;
	switch (debug_osc_trigger_data.ch_type) {
	case DEBUG_OSC_SETPOINT_TYPE_FLOAT: debug_osc_trigger_data.previous_value.f32 = *(float*)(adr); break;
	case DEBUG_OSC_SETPOINT_TYPE_INT32:   debug_osc_trigger_data.previous_value.i32 = *(int32_t*)(adr); break;
	case DEBUG_OSC_SETPOINT_TYPE_BOOL:  debug_osc_trigger_data.previous_value.i32 = (*(uint8_t*)(adr))?1:0; break;
	}
}


void debug_osc_add_data() {
	//����������� ������
	osc_copy_data((char*)debug_osc_main_buf + debug_osc_buf_ptr * osc_record_size);
	debug_osc_buf_last_added_ptr = debug_osc_buf_ptr;

	if (debug_osc_bufs_added < debug_osc_bufs_total)
		debug_osc_bufs_added++;
	debug_osc_buf_ptr++;
	if (debug_osc_buf_ptr == debug_osc_bufs_total)
		debug_osc_buf_ptr = 0;
    
	debug_osc_save_previous();
}




bool debug_osc_check_trigger_rise() {
	switch (debug_osc_trigger_data.ch_type) {
	case DEBUG_OSC_SETPOINT_TYPE_FLOAT: {
		float f = *(float*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		float setpoint = debug_osc_trigger_data.setpoint.f32;
		if (debug_osc_trigger_data.previous_value.f32 < setpoint && f >= setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_INT32: {
		int32_t i = *(int32_t*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		int32_t setpoint = debug_osc_trigger_data.setpoint.i32;
		if (debug_osc_trigger_data.previous_value.i32 < setpoint && i >= setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_BOOL: {
		int32_t i = (int32_t) *((uint8_t*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		if (debug_osc_trigger_data.previous_value.i32 != i && i == debug_osc_trigger_data.setpoint.i32) return true;
		break; }
	}
	return false;
}

bool debug_osc_check_trigger_fall() {
	switch (debug_osc_trigger_data.ch_type) {
	case DEBUG_OSC_SETPOINT_TYPE_FLOAT: {
		float f = *(float*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		float setpoint = debug_osc_trigger_data.setpoint.f32;
		if (debug_osc_trigger_data.previous_value.f32 > setpoint && f <= setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_INT32: {
		int32_t i = *(int32_t*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		int32_t setpoint = debug_osc_trigger_data.setpoint.i32;
		if (debug_osc_trigger_data.previous_value.i32 > setpoint && i <= setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_BOOL: {
		int32_t i = (int32_t) *((uint8_t*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		if (debug_osc_trigger_data.previous_value.i32 != i && i == debug_osc_trigger_data.setpoint.i32) return true;
		break; }
	}
	return false;
}


bool debug_osc_check_trigger_equal() {
	switch (debug_osc_trigger_data.ch_type) {
	case DEBUG_OSC_SETPOINT_TYPE_FLOAT: {
		float f = *(float*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		float setpoint = debug_osc_trigger_data.setpoint.f32;
		if (f == setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_INT32: {
		int32_t i = *(int32_t*)((char*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		int32_t setpoint = debug_osc_trigger_data.setpoint.i32;
		if (i == setpoint) return true;
		break; }
	case DEBUG_OSC_SETPOINT_TYPE_BOOL: {
		int32_t i = (int32_t) *((uint8_t*)debug_osc_main_buf + debug_osc_buf_last_added_ptr * osc_record_size + debug_osc_trigger_data.ch_offset);
		if (i == debug_osc_trigger_data.setpoint.i32) return true;
		break; }
	}
	return false;
}


//�������� ������� ������������ �������
bool debug_osc_check_trigger() {

	if (debug_osc_bufs_added < debug_osc_trigger_data.trigger_point_num)
		return false;

	switch (debug_osc_trigger_data.type){
	case DEBUG_OSC_TRIGGER_TYPE_RESET: return false;
	case DEBUG_OSC_TRIGGER_TYPE_UNCONDITIONAL: return true;
	case DEBUG_OSC_TRIGGER_TYPE_RISE: if (!debug_osc_trigger_data.previous_en) return false;
		                              return debug_osc_check_trigger_rise();
	case DEBUG_OSC_TRIGGER_TYPE_FALL: if (!debug_osc_trigger_data.previous_en) return false;
		                              return debug_osc_check_trigger_fall();
	case DEBUG_OSC_TRIGGER_TYPE_BOTH: if (!debug_osc_trigger_data.previous_en) return false;
		                              return (debug_osc_check_trigger_rise() || debug_osc_check_trigger_fall());
	case DEBUG_OSC_TRIGGER_TYPE_EQUAL: return debug_osc_check_trigger_equal();
	}

	return false;
}

//�������� ���������� �� �������������
bool debug_osc_accomplished() {
	return (debug_osc_bufs_added >= debug_osc_trigger_data.length);
}

//�������� ����������� �� ������� �������������
bool debug_osc_dispatch_complete() {
	return debug_osc_trigger_data.dispatched;
}

//��������� ��������� �� ����� ������������� � ������� part
uint8_t* debug_osc_get_osc_data(unsigned part) {

	//����������� ������� ������ ����� ��� ������ � ������� part
	unsigned sub = debug_osc_bufs_added - part;
	//����� ���������������� ������
	unsigned buf_ptr = (debug_osc_buf_ptr >= sub) ? debug_osc_buf_ptr >= sub : (debug_osc_bufs_total-sub)+debug_osc_buf_ptr;

	uint8_t* ptr = (uint8_t*)debug_osc_main_buf + buf_ptr * osc_record_size;
	ptr += osc_record_offset_real;
	
	return ptr;
}

//��������� ������ �������
void debug_osc_setup_trigger() {
	
	debug_osc_trigger_data.type = debug_osc_new_trigger_data.trigger_type;
	if (debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_RESET
		&& debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_UNCONDITIONAL
		&& debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_RISE
		&& debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_FALL
		&& debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_BOTH
		&& debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_EQUAL)
		debug_osc_trigger_data.type = DEBUG_OSC_TRIGGER_TYPE_RESET;

	//���� ��������
	debug_osc_trigger_data.dispatched = false;

	if (debug_osc_trigger_data.type == DEBUG_OSC_TRIGGER_TYPE_RESET)
		return;

	debug_osc_trigger_data.continuous_mode = debug_osc_new_trigger_data.continuous_mode;
	
	while (true) {

		unsigned ch = debug_osc_new_trigger_data.trigger_channel;
		if (ch >= osc_ch_num)
			break;

		debug_osc_trigger_data.ch_num = ch;

		debug_osc_trigger_data.ch_offset = osc_ch_data_offset[ch];

		bool err = false;
		switch (osc_ch_data_type[ch]) {
		case LOGIC_TYPE_REAL_OUT: debug_osc_trigger_data.ch_type = DEBUG_OSC_SETPOINT_TYPE_FLOAT; break;
		case LOGIC_TYPE_INT_OUT:  debug_osc_trigger_data.ch_type = DEBUG_OSC_SETPOINT_TYPE_INT32; break;
		case LOGIC_TYPE_BOOL_OUT: debug_osc_trigger_data.ch_type = DEBUG_OSC_SETPOINT_TYPE_BOOL; break;
		default: err = true; break;
		};
		if (err)
			break;

		debug_osc_trigger_data.previous_en = false;

		//������� ��������� ����� � ��������
		unsigned num = (debug_osc_new_trigger_data.length_msec * 1000) / SurzaPeriod();
		debug_osc_trigger_data.length = (num < debug_osc_bufs_total) ? num : debug_osc_bufs_total;

		//���������� ����� ������� �� ����������� ��������
		if (debug_osc_new_trigger_data.trigger_point >= 100)
			debug_osc_trigger_data.trigger_point_num = debug_osc_trigger_data.length - 1;
		else
			debug_osc_trigger_data.trigger_point_num = (debug_osc_trigger_data.length*debug_osc_new_trigger_data.trigger_point) / 100;

				
		//�������
		if (debug_osc_trigger_data.type != DEBUG_OSC_TRIGGER_TYPE_UNCONDITIONAL) {
			if (debug_osc_trigger_data.ch_type == DEBUG_OSC_SETPOINT_TYPE_FLOAT) {
				if (!_finite(debug_osc_new_trigger_data.setpoint.f))
					break;
				debug_osc_trigger_data.setpoint.f32 = debug_osc_new_trigger_data.setpoint.f;
			}
			else if (debug_osc_trigger_data.ch_type == DEBUG_OSC_SETPOINT_TYPE_INT32) {
				debug_osc_trigger_data.setpoint.i32 = debug_osc_new_trigger_data.setpoint.i;
			}
			else {
				debug_osc_trigger_data.setpoint.i32 = debug_osc_new_trigger_data.setpoint.i ? 1 : 0;
			}
		} else {
			debug_osc_trigger_data.setpoint.i32 = 0;
		}

		return;
	}

	//� ������ ������ ������ ��������
	debug_osc_trigger_data.type = DEBUG_OSC_TRIGGER_TYPE_RESET;
	return;
}

//������� ����� ������������ ������������
void debug_osc_relaunch(bool reset_trigger) {
	debug_osc_trigger_data.previous_en = false;
	debug_osc_buf_ptr = 0;
	debug_osc_bufs_added = 0;
	debug_osc_id++;
	if(reset_trigger)
		debug_osc_trigger_data.type = DEBUG_OSC_TRIGGER_TYPE_RESET;
}



//���������� ��������� ������
void debug_osc_add() {

	if (!debug_osc_init_ok)
		return;

	if (debug_osc_connection_en) {
		debug_osc_connection_timer = debug_osc_connection_timer > SurzaPeriod() ? debug_osc_connection_timer - SurzaPeriod() : 0;
		if (!debug_osc_connection_timer)
			debug_osc_connection_en = false;
	}

	if (!debug_osc_connection_en)
		debug_osc_state = DEBUG_OSC_STATE_WAIT_CONNECTION;



	switch (debug_osc_state) {
	case DEBUG_OSC_STATE_WAIT_CONNECTION:
		if (debug_osc_connection_en) {
			debug_osc_relaunch(true);
			debug_osc_state = DEBUG_OSC_STATE_ACCUMULATE_DATA;
		} else
			debug_osc_set_new_trigger = false;
		break;
	case DEBUG_OSC_STATE_ACCUMULATE_DATA:
	    debug_osc_add_data();
		if (debug_osc_set_new_trigger) {
			debug_osc_setup_trigger();
			debug_osc_set_new_trigger = false;
		}
		if (debug_osc_check_trigger()) {
			debug_osc_trigger_data.trigger_time = time_get();
			debug_osc_state = DEBUG_OSC_STATE_ACCOMPLISH;
			break;
		}
		debug_osc_save_previous();
		break;
	case DEBUG_OSC_STATE_ACCOMPLISH:
		if (debug_osc_set_new_trigger) {
			debug_osc_state = DEBUG_OSC_STATE_WAIT_CONNECTION;
			break;
		}
		if (debug_osc_accomplished()) {
			debug_osc_state = DEBUG_OSC_STATE_WAIT_DISPATCH;
			break;
		}
		//���������� ������ ����� �������� �� ���������� ��� ���������� ������ � �������� � ��������� �������
		debug_osc_add_data();  
		break;
    case DEBUG_OSC_STATE_WAIT_DISPATCH:
		if (debug_osc_dispatch_complete()) {
			if (debug_osc_trigger_data.continuous_mode) {
				debug_osc_relaunch(false);
				debug_osc_state = DEBUG_OSC_STATE_ACCUMULATE_DATA;
			} else
				debug_osc_state = DEBUG_OSC_STATE_WAIT_CONNECTION;
			break;
		}
		if (debug_osc_set_new_trigger) {
			debug_osc_state = DEBUG_OSC_STATE_WAIT_CONNECTION;
			break;
		}
		break;
	default: break;
	}


	//--------------------------
	static unsigned prev_state = DEBUG_OSC_STATE_WAIT_CONNECTION;
	if (prev_state != debug_osc_state) {
		switch (debug_osc_state) {
		case DEBUG_OSC_STATE_WAIT_CONNECTION: { LOG_AND_SCREEN("DEBUG_OSC_STATE_WAIT_CONNECTION"); } break;
		case DEBUG_OSC_STATE_ACCUMULATE_DATA: { LOG_AND_SCREEN("DEBUG_OSC_STATE_ACCUMULATE_DATA"); } break;
		case DEBUG_OSC_STATE_ACCOMPLISH: { LOG_AND_SCREEN("DEBUG_OSC_STATE_ACCOMPLISH"); } break;
		case DEBUG_OSC_STATE_WAIT_DISPATCH: { LOG_AND_SCREEN("DEBUG_OSC_STATE_WAIT_DISPATCH"); } break;
		}
	}
	prev_state = debug_osc_state;
    //------------------------


}


uint32_t debug_osc_get_status() {
	switch (atom_get_state(&debug_osc_state)) {
	case DEBUG_OSC_STATE_WAIT_CONNECTION: return DEBUG_OSC_STATUS_READY;
	case DEBUG_OSC_STATE_ACCUMULATE_DATA: return (atom_get_state(&debug_osc_trigger_data.type) == DEBUG_OSC_TRIGGER_TYPE_RESET ? DEBUG_OSC_STATUS_READY : DEBUG_OSC_STATUS_WAIT_TRIGGER);
	case DEBUG_OSC_STATE_ACCOMPLISH: return DEBUG_OSC_STATUS_ACCOMPLISH;
	case DEBUG_OSC_STATE_WAIT_DISPATCH: return DEBUG_OSC_STATUS_WAIT_DISPATCH;
	default: return DEBUG_OSC_STATUS_READY;
	}
}

void debug_osc_net_callback(net_msg_t* msg, uint64_t channel){

	// �������� ����������� ���������
	if (msg->size < sizeof(msg_type_debug_osc_t)
		|| msg->type != (uint8_t)NET_MSG_DEBUG_OSCILLOSCOPE)
		return;

	if (!debug_osc_init_ok)
		return;

	msg_type_debug_osc_t* osc_msg = (msg_type_debug_osc_t*)&msg->data[0];
	if (memcmp(osc_msg->md5_hash, settings_header->hash, 16))
		return;


	global_spinlock_lock();
	  debug_osc_connection_en = true;
	  debug_osc_connection_timer = DEBUG_OSC_CONNECTION_TIMEOUT_USEC;
	global_spinlock_unlock();
	

	switch (osc_msg->type) {
	case DEBUG_OSC_REQUEST_STATUS:
	{
        //�������� � ����� ��������� � ������� ��������
		const unsigned msg_size = sizeof(msg_type_debug_osc_t) + sizeof(debug_osc_status_t);
		if (net_msg_buf_get_available_space(msg) < msg_size) {
			msg = net_get_msg_buf(msg_size);
			if (!msg)
				return;
		} else	msg->size = msg_size;

		msg->type = (uint8_t)NET_MSG_DEBUG_OSCILLOSCOPE;
		msg->subtype = 0;

		msg_type_debug_osc_t* osc_msg = (msg_type_debug_osc_t*)&msg->data[0];
		osc_msg->type = DEBUG_OSC_STATUS;
		osc_msg->size = sizeof(debug_osc_status_t);
		memcpy(osc_msg->md5_hash, settings_header->hash, 16);

		debug_osc_status_t* status_data = (debug_osc_status_t*)(osc_msg+1);
		status_data->status = debug_osc_get_status();

		net_send_msg(msg, NET_PRIORITY_LOW, channel);
	}
		break;

	case DEBUG_OSC_REQUEST_HEADER:

		if (debug_osc_get_status() != DEBUG_OSC_STATUS_WAIT_DISPATCH)
			break;

		{
			//�������� � ����� ��������� � ���������� ����� �������������
			const unsigned msg_size = sizeof(msg_type_debug_osc_t) + sizeof(debug_osc_header_t);
			if (net_msg_buf_get_available_space(msg) < msg_size) {
				msg = net_get_msg_buf(msg_size);
				if (!msg)
					return;
			}
			else	msg->size = msg_size;

			msg->type = (uint8_t)NET_MSG_DEBUG_OSCILLOSCOPE;
			msg->subtype = 0;
			
			msg_type_debug_osc_t* osc_msg = (msg_type_debug_osc_t*)&msg->data[0];
			osc_msg->type = DEBUG_OSC_HEADER;
			osc_msg->size = sizeof(debug_osc_header_t);
			memcpy(osc_msg->md5_hash, settings_header->hash, 16);

			debug_osc_header_t* osc_header = (debug_osc_header_t*)(osc_msg + 1);

			//���������� ���������
			osc_header->id = debug_osc_id;
			osc_header->parts = debug_osc_trigger_data.length;
			osc_header->part_size = osc_record_size_raw;
			osc_header->step_time_usecs = SurzaPeriod();
			osc_header->trigger_channel = debug_osc_trigger_data.ch_num;
			osc_header->trigger_step = debug_osc_trigger_data.trigger_point_num;
			osc_header->trigger_time = debug_osc_trigger_data.trigger_time;

			net_send_msg(msg, NET_PRIORITY_LOW, channel);
		}

		break;

	case DEBUG_OSC_REQUEST_DATA:

		if (debug_osc_get_status() != DEBUG_OSC_STATUS_WAIT_DISPATCH)
			break;

		{
			//�������� ������� 
			if (osc_msg->size < sizeof(debug_osc_request_data_t))
				return;

			debug_osc_request_data_t* request = (debug_osc_request_data_t*)(osc_msg + 1);
			if (request->id != debug_osc_id
				|| request->part>= debug_osc_trigger_data.length)
				return;

			unsigned part = request->part;

			//�������� ����� ������� �������������
			const unsigned msg_size = sizeof(msg_type_debug_osc_t) + sizeof(debug_osc_data_t) + osc_record_size_raw;
			if (net_msg_buf_get_available_space(msg) < msg_size) {
				msg = net_get_msg_buf(msg_size);
				if (!msg)
					return;
			}
			else msg->size = msg_size;

			msg->type = (uint8_t)NET_MSG_DEBUG_OSCILLOSCOPE;
			msg->subtype = 0;

			osc_msg = (msg_type_debug_osc_t*)&msg->data[0];
			osc_msg->type = DEBUG_OSC_DATA;
			osc_msg->size = sizeof(debug_osc_data_t) + osc_record_size_raw;
			memcpy(osc_msg->md5_hash, settings_header->hash, 16);

			//����������
			debug_osc_data_t* osc_data = (debug_osc_data_t*)(osc_msg + 1);
			osc_data->id = debug_osc_id;
			osc_data->part = part;
			osc_data->part_size_bytes = osc_record_size_raw;

			memcpy(osc_data+1, debug_osc_get_osc_data(part), osc_record_size_raw);

			net_send_msg(msg, NET_PRIORITY_LOW, channel);
		}
		break;

	case DEBUG_OSC_SET_TRIGGER:

		if (osc_msg->size < sizeof(debug_osc_trigger_t))
			return;

		global_spinlock_lock();
		  memcpy(&debug_osc_new_trigger_data, osc_msg + 1, sizeof(debug_osc_trigger_t));
		  debug_osc_set_new_trigger = true;
		global_spinlock_unlock();

		break;

	case DEBUG_OSC_REQUEST_DELETE:

		if (debug_osc_get_status() != DEBUG_OSC_STATUS_WAIT_DISPATCH)
			break;

		global_spinlock_lock();
		  debug_osc_trigger_data.dispatched = true;
		global_spinlock_unlock();

		break;

	default: return;
	}

}




//------------------------------------------------------------------------
//  ��������� ��������� ������ ���
//------------------------------------------------------------------------

bool set_inputs_init_ok;

static float*   set_inputs_data_f;
static int32_t* set_inputs_data_i;
static uint8_t* set_inputs_data_b;

static unsigned* set_inputs_index_f;
static unsigned* set_inputs_index_i;
static unsigned* set_inputs_index_b;

static unsigned set_inputs_data_f_ptr;
static unsigned set_inputs_data_i_ptr;
static unsigned set_inputs_data_b_ptr;

void set_inputs_net_callback(net_msg_t* msg, uint64_t channel);


static bool init_set_inputs() {

	set_inputs_init_ok = false;

	set_inputs_data_f_ptr = 0;
	set_inputs_data_i_ptr = 0;
	set_inputs_data_b_ptr = 0;

	set_inputs_data_f = NULL;
	set_inputs_data_i = NULL;
	set_inputs_data_b = NULL;

	set_inputs_index_f = NULL;
	set_inputs_index_i = NULL;
	set_inputs_index_b = NULL;


	//��������� ������ ��� �������� ������������ �������� � ������ ����������
	set_inputs_data_f = (float*)malloc(math_real_in_num * 4);
	set_inputs_data_i = (int32_t*)malloc(math_int_in_num * 4);
	set_inputs_data_b = (uint8_t*)malloc(math_bool_in_num);

	set_inputs_index_f = (unsigned*)malloc(sizeof(unsigned)*math_real_in_num);
	set_inputs_index_i = (unsigned*)malloc(sizeof(unsigned)*math_int_in_num);
	set_inputs_index_b = (unsigned*)malloc(sizeof(unsigned)*math_bool_in_num);

	if (set_inputs_data_f == NULL || set_inputs_data_i == NULL || set_inputs_data_b == NULL ||
		set_inputs_index_f == NULL || set_inputs_index_i == NULL || set_inputs_index_b == NULL) {
		if (set_inputs_data_f) free(set_inputs_data_f);
		if (set_inputs_data_i) free(set_inputs_data_i);
		if (set_inputs_data_b) free(set_inputs_data_b);
		if (set_inputs_index_f) free(set_inputs_index_f);
		if (set_inputs_index_i) free(set_inputs_index_i);
		if (set_inputs_index_b) free(set_inputs_index_b);
		return false;
	}

	net_add_dispatcher((uint8_t)NET_MSG_SET_INPUT, set_inputs_net_callback);

	set_inputs_init_ok = true;

	return true;
}


void set_inputs_net_callback(net_msg_t* msg, uint64_t channel) {

	if (!set_inputs_init_ok)
		return;

	if (msg->size < sizeof(msg_type_set_input_t))
		return;

	msg_type_set_input_t* i_msg = (msg_type_set_input_t*)&msg->data[0];

	if (msg->size < sizeof(msg_type_set_input_t) + i_msg->num * sizeof(input_value_t))
		return;

	if (i_msg->num == 0)
		return;

	if (memcmp(i_msg->hash, settings_header->hash, 16))
		return;


	input_value_t* val = (input_value_t*)(i_msg + 1);

	for (unsigned i = i_msg->num; i != 0; i--, val++) {

		unsigned index = val->index;

		switch (val->type) {
		case SURZA_INPUT_TYPE_FLOAT:
			if (index < math_real_in_num) {
				global_spinlock_lock();
				if (_finite(val->val.f)) {
					set_inputs_data_f[set_inputs_data_f_ptr] = val->val.f;
					set_inputs_index_f[set_inputs_data_f_ptr] = index;
					set_inputs_data_f_ptr++;
				}
			    global_spinlock_unlock();
			}
			break;
		case SURZA_INPUT_TYPE_INT32:
			if (index < math_int_in_num) {
				global_spinlock_lock();
				 set_inputs_data_i[set_inputs_data_i_ptr] = val->val.i;
				 set_inputs_index_i[set_inputs_data_i_ptr] = index;
				 set_inputs_data_i_ptr++;
				global_spinlock_unlock();
			}
			break;
		case SURZA_INPUT_TYPE_BOOL:
			if (index < math_bool_in_num) {
				global_spinlock_lock();
				 set_inputs_data_b[set_inputs_data_b_ptr] = val->val.b ? true : false;
				 set_inputs_index_b[set_inputs_data_b_ptr] = index;
				 set_inputs_data_b_ptr++;
				global_spinlock_unlock();
			}
			break;
		default: return;
		}

	}

}


void set_inputs() {

	while (set_inputs_data_f_ptr) {
		set_inputs_data_f_ptr--;
		math_real_in[set_inputs_index_f[set_inputs_data_f_ptr]] = set_inputs_data_f[set_inputs_data_f_ptr];
	}

	while (set_inputs_data_i_ptr) {
		set_inputs_data_i_ptr--;
		math_int_in[set_inputs_index_i[set_inputs_data_i_ptr]] = set_inputs_data_i[set_inputs_data_i_ptr];
	}

	while (set_inputs_data_b_ptr) {
		set_inputs_data_b_ptr--;
		math_bool_in[set_inputs_index_b[set_inputs_data_b_ptr]] = set_inputs_data_b[set_inputs_data_b_ptr];
	}
	
}




//------------------------------------------------------------------------
//  �������
//------------------------------------------------------------------------
static bool commands_init_ok;

static bool* commands_allowed;
static unsigned* commands_apply_stack;
static unsigned commands_apply_stack_ptr;
static unsigned* commands_turnoff_stack;
static unsigned commands_turnoff_stack_ptr;


static void commands_net_callback(net_msg_t* msg, uint64_t channel) {

	if (!commands_init_ok)
		return;

	//�������� ���������

	if (msg->size < sizeof(msg_type_command_t))
		return;

	msg_type_command_t* cmd_msg = (msg_type_command_t*)&msg->data[0];

	if (memcmp(cmd_msg->hash, settings_header->hash, 16))
		return;

	if (cmd_msg->index >= math_bool_in_num)
		return;

	if (!commands_allowed[cmd_msg->index])
		return;

	//���������� ������� � ���� ������

	global_spinlock_lock();
	if (commands_apply_stack_ptr < math_bool_in_num)
		commands_apply_stack[commands_apply_stack_ptr++] = cmd_msg->index;
	global_spinlock_unlock();

}


static bool init_commands() {

	commands_init_ok = false;

	commands_allowed = NULL;
	commands_apply_stack = NULL;
	commands_turnoff_stack = NULL;
	commands_apply_stack_ptr = 0;
	commands_turnoff_stack_ptr = 0;

	param_tree_node_t* commands_node = ParamTree_Find(ParamTree_MainNode(), "COMMANDS", PARAM_TREE_SEARCH_NODE);
	if (!commands_node) {
		LOG_AND_SCREEN("No COMMANDS");
		return true;   // ������� �� ������������
	}


	//��������� ������ ��� ������
	commands_allowed = (bool*)malloc(math_bool_in_num);
	commands_apply_stack = (unsigned*)malloc(math_bool_in_num);
	commands_turnoff_stack = (unsigned*)malloc(math_bool_in_num);

	if (!commands_allowed || !commands_apply_stack || !commands_turnoff_stack) {
		if (commands_allowed) free(commands_allowed);
		if (commands_apply_stack) free(commands_apply_stack);
		if (commands_turnoff_stack) free(commands_turnoff_stack);
		return false;
	}


	for (unsigned i = 0; i < math_bool_in_num; i++)
		commands_allowed[i] = false;

	
	//��������� ���������� ������
	param_tree_node_t* node;
	unsigned num;

	for (node = ParamTree_Child(commands_node); node; node = node->next) {

		param_tree_node_t* item = ParamTree_Find(node, "num", PARAM_TREE_SEARCH_ITEM);

		if (!item || !item->value || sscanf_s(item->value, "%u", &num) <= 0)
			return false;

		if (num >= math_bool_in_num)
			return false;

		commands_allowed[num] = true;

	}

	net_add_dispatcher((uint8_t)NET_MSG_COMMAND, commands_net_callback);
	
	commands_init_ok = true;

	return true;
}


static void commands_apply() {

	//������ ������ ����������� �����
	while (commands_turnoff_stack_ptr)
		math_bool_in[commands_turnoff_stack[--commands_turnoff_stack_ptr]] = false;

	//���������� ������ �������� �����
	while (commands_apply_stack_ptr) {
		math_bool_in[commands_apply_stack[--commands_apply_stack_ptr]] = true;
		commands_turnoff_stack[commands_turnoff_stack_ptr++] = commands_apply_stack[commands_apply_stack_ptr];
	}

}


//--------------------------------------------------------------------------------------------------------------------



//------------------------------------------------------------------------
//  ���������� ����
//------------------------------------------------------------------------
#define STEPTIME_CPU_FREQUENCY_MHZ  500  // ������������ ��� �������� ��� ��������� �������� ������������� ������� TSC � ������������

static bool steptime_init_ok = false;

static unsigned steptime_adr;
static unsigned steptime_input;
static unsigned steptime_time;

#define STEPTIME_DISABLE  0
#define STEPTIME_FIU      1
#define STEPTIME_INTERNAL 2
static unsigned steptime_type;  //��� ������������� ����������

uint64_t tsc_reg_start, tsc_reg_stop;

static bool init_steptime() {

	steptime_init_ok = false;

	steptime_time = 0;
	steptime_type = STEPTIME_DISABLE;

	param_tree_node_t* node = ParamTree_Find(ParamTree_MainNode(), "TIME_CHECK", PARAM_TREE_SEARCH_NODE);
	if (!node) {
		return true;  //���������� ���� �� ������������
	}


	param_tree_node_t* item;

	item = ParamTree_Find(node, "type", PARAM_TREE_SEARCH_ITEM);
	if (item && item->value && sscanf_s(item->value, "%u", &steptime_type) > 0) {
		if (steptime_type != STEPTIME_DISABLE && steptime_type != STEPTIME_FIU && steptime_type != STEPTIME_INTERNAL)
			return false;
	}
	else {
		steptime_type = STEPTIME_DISABLE;
		return true;
	}


	if (steptime_type == STEPTIME_FIU) {
		item = ParamTree_Find(node, "adr", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			sscanf_s(item->value, "%u", &steptime_adr);
		}
	}


	item = ParamTree_Find(node, "input", PARAM_TREE_SEARCH_ITEM);
	if (!item || !item->value)  return false;
	if (sscanf_s(item->value, "%u", &steptime_input) <= 0) return false;
	if (steptime_input >= math_int_in_num) return false;


	steptime_init_ok = true;

	return true;

}

//������ ����������� ���������� ������� �����
static void steptime_start_check() {

	if (!steptime_init_ok)
		return;

	if (steptime_type == STEPTIME_INTERNAL) {
		//����� ����������� ����������
		tsc_reg_start = get_cpu_clks_64();
	}
}



//��������� ������� �����
static void steptime_update() {

	if (!steptime_init_ok)
		return;

	if (steptime_type == STEPTIME_FIU)
		steptime_time = RTInW(steptime_adr);  //���������� ���
	else {
		//���������� ����������
		tsc_reg_stop = get_cpu_clks_64() - tsc_reg_start;
	}

}

//����������� ������� ����� � ���
static void steptime_copy() {

	if (!steptime_init_ok)
		return;

	if (steptime_type == STEPTIME_FIU) {
		int steptime_us = (int)(steptime_time / 50);
		MATH_IO_INT_IN[steptime_input] = steptime_us;
	}
	else {
		//��� ����������� ����������
		MATH_IO_INT_IN[steptime_input] = (int32_t)(tsc_reg_stop / STEPTIME_CPU_FREQUENCY_MHZ);
	}

}





//------------------------------------------------------------------------
//  ���������� ��
//------------------------------------------------------------------------
static bool shu_init_ok = false;

static bool shu_en[2];
static unsigned shu_first_adr[2];
static unsigned shu_status_num[2];
static unsigned shu_status_adr[2];
static unsigned shu_reset_adr[2];
static unsigned shu_n_of_regs[2];
static unsigned shu_first_input[2];
static unsigned shu_reset_output[2];
static bool shu_debug_mode_one[2];
static bool shu_debug_mode_no_fb[2];

static unsigned shu_reg_counter[2];


static bool init_shu() {

	shu_init_ok = false;

	shu_en[0] = false;
	shu_en[1] = false;

	shu_reg_counter[0] = 0;
	shu_reg_counter[1] = 0;

	param_tree_node_t* shu_node = ParamTree_Find(ParamTree_MainNode(), "SHU", PARAM_TREE_SEARCH_NODE);
	if (!shu_node) {
		return true;  //�� �� ������������
	}


	int shu_n = 0;
	param_tree_node_t* node;
	param_tree_node_t* item;

	for (node = ParamTree_Child(shu_node); node; node = node->next, shu_n++) {

		unsigned u32;
		
		item = ParamTree_Find(node, "first_reg_adr", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_first_adr[shu_n] = u32;


		item = ParamTree_Find(node, "status_reg", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_status_num[shu_n] = u32;


		item = ParamTree_Find(node, "reset_reg_adr", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_reset_adr[shu_n] = u32;



		item = ParamTree_Find(node, "n_of_regs", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_n_of_regs[shu_n] = u32;



		item = ParamTree_Find(node, "first_input", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_first_input[shu_n] = u32;


		item = ParamTree_Find(node, "reset_output", PARAM_TREE_SEARCH_ITEM);
		if (item && item->value) {
			u32 = UINT_MAX;
			sscanf_s(item->value, "%u", &u32);
		}
		else
			return false;

		if (u32 < UINT_MAX)
			shu_reset_output[shu_n] = u32;


		shu_debug_mode_one[shu_n] = false;
		item = ParamTree_Find(node, "debug_one_cell", PARAM_TREE_SEARCH_ITEM);
		if(item && item->value[0] == '1')
			shu_debug_mode_one[shu_n] = true;

		shu_debug_mode_no_fb[shu_n] = false;
		item = ParamTree_Find(node, "debug_no_fb", PARAM_TREE_SEARCH_ITEM);
		if(item && item->value[0] == '1')
			shu_debug_mode_no_fb[shu_n] = true;


		//��������

		if (shu_status_num[shu_n] >= shu_n_of_regs[shu_n])
			return false;

		shu_status_adr[shu_n] = shu_status_num[shu_n] + shu_first_adr[shu_n];

		if (shu_first_adr[shu_n] > 0xffff || shu_reset_adr[shu_n] > 0xffff || shu_status_adr[shu_n] > 0xffff)
			return false;

		if (shu_first_input[shu_n] >= math_bool_in_num
			|| shu_reset_output[shu_n] >= math_bool_out_num
			|| shu_first_input[shu_n]+shu_n_of_regs[shu_n]*8 > math_bool_in_num)
			return false;


		shu_en[shu_n] = true;


		//������ ���������� �������
		uint16_t reg = 0x00E7;
		if (shu_debug_mode_one[shu_n]) reg |= 0x0100;
		if (shu_debug_mode_no_fb[shu_n]) reg |= 0x0200;
		RTOutW(shu_reset_adr[shu_n] + 2, reg);

	}

	shu_init_ok = true;

	return true;

}



//��������� ���������� ��
static void shu_update() {

	if (!shu_init_ok)
		return;


	uint8_t u8, mask;

	for (unsigned i = 0; i < 2; i++) {

		if (!shu_en[i])
			continue;

		//���������� ���������� ��������
		u8 = RTIn(shu_first_adr[i]+shu_reg_counter[i]);

		boolean_T* ptr = &MATH_IO_BOOL_IN[shu_first_input[i] + shu_reg_counter[i] * 8];
		for (mask = 0x01; mask; mask <<= 1, ptr++)
			*ptr = (mask&u8?true:false);

		shu_reg_counter[i]++;
		if(shu_reg_counter[i]==shu_status_num[i])  //������� �����c���� ��������
			shu_reg_counter[i]++;
		if (shu_reg_counter[i] >= shu_n_of_regs[i])
			shu_reg_counter[i] = 0;


		//���������� ���������� �������� �� ������ ����
		u8 = RTIn(shu_status_adr[i]);

		ptr = &MATH_IO_BOOL_IN[shu_first_input[i] + shu_status_num[i] * 8];
		for (mask = 0x01; mask; mask <<= 1, ptr++)
			*ptr = (mask&u8 ? true : false);

	}

}


static void shu_reset() {

	if (!shu_init_ok)
		return;

	for (unsigned i = 0; i < 2; i++) {

		if (!shu_en[i])
			continue;

		if (MATH_IO_BOOL_OUT[shu_reset_output[i]])
			RTOutW(shu_reset_adr[i], 0x7A01);

	}

}





//------------------------------------------------------------------------
//  ������ � ����� ISA �� ����
//------------------------------------------------------------------------

static bool isa_first_call;

typedef struct {
	unsigned adr;
	int32_t* data;
	uint8_t* control;
} isa_table_t;

static isa_table_t* isa_in_table;
static isa_table_t* isa_out_table;

unsigned isa_in_table_size;
unsigned isa_out_table_size;

static bool init_isa() {

	isa_first_call = true;
	isa_in_table_size = 0;
	isa_out_table_size = 0;


	param_tree_node_t* isa_node = ParamTree_Find(ParamTree_MainNode(), "ISA_IN", PARAM_TREE_SEARCH_NODE);
	if (isa_node) {

		//����������� ���������� �������
		isa_in_table_size = ParamTree_ChildNum(isa_node);
		if (isa_in_table_size) {

			//��������� ������ ��� �������
			isa_in_table = (isa_table_t*) malloc(sizeof(isa_table_t)*isa_in_table_size);
			if (isa_in_table) {

				//��������� ������
				param_tree_node_t* node;
				param_tree_node_t* item;
				bool err = false;
				unsigned u32, cnt=0;
				isa_table_t* ptr = isa_in_table;

				for (node = ParamTree_Child(isa_node); node && !err && cnt<isa_in_table_size; node = node->next, ptr++) {

					item = ParamTree_Find(node, "data_num", PARAM_TREE_SEARCH_ITEM);
					if (!item || !item->value)	err = true;
					else if (sscanf_s(item->value, "%u", &u32) <= 0  || u32 >= math_int_in_num ) err = true;
					else ptr->data = &MATH_IO_INT_IN[u32];

					if (!err) {
						item = ParamTree_Find(node, "control_num", PARAM_TREE_SEARCH_ITEM);
						if (!item || !item->value)	err = true;
						else if (sscanf_s(item->value, "%u", &u32) <= 0 || u32 >= math_bool_out_num) err = true;
						else ptr->control = &MATH_IO_BOOL_OUT[u32];
					}

					if (!err) {
						item = ParamTree_Find(node, "adr", PARAM_TREE_SEARCH_ITEM);
						if (!item || !item->value)	err = true;
						else if (sscanf_s(item->value, "%u", &u32) <= 0 || u32 >= 0xffff) err = true;
						else ptr->adr = u32;
					}

				}

				if (err) {
					free(isa_in_table);
					isa_in_table_size = 0;
					return false;
				}

			}
			else
				isa_in_table_size = 0;

		}

	}


	isa_node = ParamTree_Find(ParamTree_MainNode(), "ISA_OUT", PARAM_TREE_SEARCH_NODE);
	if (isa_node) {

		//����������� ���������� �������
		isa_out_table_size = ParamTree_ChildNum(isa_node);
		if (isa_out_table_size) {

			//��������� ������ ��� �������
			isa_out_table = (isa_table_t*)malloc(sizeof(isa_table_t)*isa_out_table_size);
			if (isa_out_table) {

				//��������� ������
				param_tree_node_t* node;
				param_tree_node_t* item;
				bool err = false;
				unsigned u32, cnt = 0;
				isa_table_t* ptr = isa_out_table;

				for (node = ParamTree_Child(isa_node); node && !err && cnt<isa_out_table_size; node = node->next, ptr++) {

					item = ParamTree_Find(node, "data_num", PARAM_TREE_SEARCH_ITEM);
					if (!item || !item->value)	err = true;
					else if (sscanf_s(item->value, "%u", &u32) <= 0 || u32 >= math_int_out_num) err = true;
					else ptr->data = &MATH_IO_INT_OUT[u32];

					if (!err) {
						item = ParamTree_Find(node, "control_num", PARAM_TREE_SEARCH_ITEM);
						if (!item || !item->value)	err = true;
						else if (sscanf_s(item->value, "%u", &u32) <= 0 || u32 >= math_bool_out_num) err = true;
						else ptr->control = &MATH_IO_BOOL_OUT[u32];
					}

					if (!err) {
						item = ParamTree_Find(node, "adr", PARAM_TREE_SEARCH_ITEM);
						if (!item || !item->value)	err = true;
						else if (sscanf_s(item->value, "%u", &u32) <= 0 || u32 >= 0xffff) err = true;
						else ptr->adr = u32;
					}

				}

				if (err) {
					free(isa_out_table);
					isa_out_table_size = 0;
					if (isa_in_table_size) {
						free(isa_in_table);
						isa_in_table_size = 0;
					}
					return false;
				}

			}
			else
				isa_in_table_size = 0;

		}

	}

	return true;
}


static void isa_read() {
	if (isa_first_call) {
		isa_first_call = false;
		return;
    }

	isa_table_t* ptr = isa_in_table;
	for (unsigned i = 0; i < isa_in_table_size; i++, ptr++)
		if (*(ptr->control))
			*(ptr->data) = (int32_t)RTIn(ptr->adr);

}

static void isa_write() {

	isa_table_t* ptr = isa_out_table;
	for (unsigned i = 0; i < isa_out_table_size; i++, ptr++)
		if (*(ptr->control))
			RTOut(ptr->adr,  (uint8_t)(*(ptr->data) & 0xff));

}

//-----------------------------------------------------------------------






//------------------------------------------------------------------------
#ifdef DELTA_HMI_ENABLE

#define DELTA_MAX_REGS_TO_SEND   80   //������ ������ ���� ������ 2

#define DELTA_REAL_START_REG     100
#define DELTA_INT_START_REG      500
#define DELTA_BOOL_START_REG     900

#define DELTA_PERIOD_MS          1000   //������ �������� ������


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

//����������� ������ �����������
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

	//  ������ ����� ��������
	static int last_time = 0;
	static bool update = false;

	if (!update) {
		int new_time = steady_clock_get();

		if (steady_clock_expired(last_time, new_time, DELTA_PERIOD_MS * 1000)) {
			last_time = new_time;
			update = true;
		}
		else {

			*num = 0;  //������ �� ���������� �� ���������� ����� ��������
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
			
			if(mask==0x0001)  //������ ������ �����, ���� �� ������� �� ����������� ���������� ������� DELTA_MAX_REGS_TO_SEND
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

			update = false;    //��������� ���� ���� �������� �����
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



#if 0
/**********************************************************************/
int t1, t2, test_time;

#define NUM_F  50
#define NUM_I  30
#define NUM_B  150

volatile float test_f[NUM_F];
volatile int32_t test_i[NUM_I];
volatile uint8_t test_b[NUM_B];

volatile float* test2_f[NUM_F];
volatile int32_t* test2_i[NUM_I];
volatile uint8_t* test2_b[NUM_B];

float* test_f_end;
int32_t* test_i_end;
uint8_t* test_b_end;

void speed_test() {

	static bool start = false;
	if (!start) {
		start = true;

		for (int i = 0; i < NUM_F; i++)
			test2_f[i] = (float*)&MATH_IO_REAL_OUT[rand()%math_real_out_num];

		for (int i = 0; i < NUM_I; i++)
			test2_i[i] = (int32_t*)&MATH_IO_INT_OUT[rand()%math_int_out_num];

		for (int i = 0; i < NUM_B; i++)
			test2_b[i] = (uint8_t*)&MATH_IO_BOOL_OUT[rand()% math_bool_out_num];

		
		test_f_end = (float*)&test_f[NUM_F];
		test_i_end = (int32_t*)&test_i[NUM_I];
		test_b_end = (uint8_t*)&test_b[NUM_B];
	}


	t1 = get_cpu_clks();

#if 0
	memcpy((char*)test_f, MATH_IO_REAL_OUT, NUM_F * 4);
	memcpy((char*)test_i, MATH_IO_INT_OUT, NUM_I * 4);
	memcpy((char*)test_b, MATH_IO_BOOL_OUT, NUM_B);
#else

	register float* src_f = (float*)test2_f;
	register float* dst_f = (float*)test_f;

	for (; dst_f != test_f_end; src_f++, dst_f++)
		*dst_f = *src_f;


	register int32_t* src_i = (int32_t*)test2_i;
	register int32_t* dst_i = (int32_t*)test_i;

	for (; dst_i != test_i_end; src_i++, dst_i++)
		*dst_i = *src_i;

	register uint8_t* src_b = (uint8_t*)test2_b;
	register uint8_t* dst_b = (uint8_t*)test_b;

	for (; dst_b != test_b_end; src_b++, dst_b++)
		*dst_b = *src_b;

	/*
	for (int i = 0; i < NUM_F; i++)
		test_f[i] = *(test2_f[i]);

	for (int i = 0; i < NUM_I; i++)
		test_i[i] = *(test2_i[i]);

	for (int i = 0; i < NUM_B; i++)
		test_b[i] = *(test2_b[i]);
		*/

#endif
	
	t2 = get_cpu_clks();

	test_time = t2 - t1;

}

/**********************************************************************/
#endif



//�������� ������������� ������� �����

static void MAIN_LOGIC_PERIOD_FUNC() {

	DEBUG_ADD_POINT(21);

	//������������� �������
	time_isr_update();

	DEBUG_ADD_POINT(22);

	dic_read();

	DEBUG_ADD_POINT(23);

	set_inputs();

	steptime_copy();

	shu_update();

	commands_apply();

	isa_read();

	// ====== ����� ����  ==================
	DEBUG_ADD_POINT(24);
    MYD_step();
	DEBUG_ADD_POINT(25);
	// =====================================

	DEBUG_ADD_POINT(26);
	dic_write();

	DEBUG_ADD_POINT(27);
	fiu_write();

	DEBUG_ADD_POINT(28);
	indi_copy();

	DEBUG_ADD_POINT(29);
	params_update();

	DEBUG_ADD_POINT(35);
	journal_add();

	DEBUG_ADD_POINT(36);
	oscilloscope_add();

	debug_osc_add();

	DEBUG_ADD_POINT(37);

	shu_reset();

	isa_write();

	steptime_update();
}

