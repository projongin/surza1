
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "logic.h"
#include "net.h"
#include "net_messages.h"
#include "log.h"
#include "filesystem.h"
#include "crc32.h"
#include "common.h"
#include "param_tree.h"



#define SETTINGS_FILENAME  "settings.bin"
#define STRINGS_FILENAME   "strings.bin"


#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint32_t size;            //размер данных
	uint32_t crc32;           //контрольная сумма по data[size]
	uint8_t  hash[16];        //MD5 хэш по data[size]
	uint8_t  paired_hash[16]; //MD5 хэш по data[size] для парного файла (настройки\строки)
	// data[size]
} settings_file_header_t;
#pragma pack(pop)


// указатели на описание настроек и строк
static settings_file_header_t* settings_header = NULL;   
static settings_file_header_t* strings_header = NULL;

//указатели на выделенную память под хранение настроек
static char* settings_data_ptr = NULL;



static bool check_settings_msg(msg_type_settings_t* msg, unsigned size) {


	if (!crc32_check((char*)(&msg->crc32+1), msg->data1_offset + msg->bytes1 - 4, msg->crc32)) {
		LOG_AND_SCREEN("Settings file bad crc32!");
		return false;
	}

	if (msg->data1_offset < msg->data2_offset + msg->bytes2) {
		LOG_AND_SCREEN("Settings file incorrect data & offset fields!");
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

	header.size = s_msg->bytes1;
	header.crc32 = crc32((char*)s_msg+s_msg->data1_offset, s_msg->bytes1);
	memcpy(&header.hash, &s_msg->md5_hash1, 16);
	memcpy(&header.paired_hash, &s_msg->md5_hash2, 16);
	
	fragments[0].pointer = (char*) &header;
	fragments[0].size = sizeof(header);
	fragments[1].pointer = (char*)s_msg + s_msg->data1_offset;
	fragments[1].size = s_msg->bytes1;

	if (!filesystem_set_current_dir("C:\\") < 0)
		return;

	if (filesystem_write_file_fragments(SETTINGS_FILENAME, fragments, 2) < 0)
		return;

	header.size = s_msg->bytes2;
	header.crc32 = crc32((char*)s_msg + s_msg->data2_offset, s_msg->bytes2);
	memcpy(&header.hash, &s_msg->md5_hash2, 16);
	memcpy(&header.paired_hash, &s_msg->md5_hash1, 16);

	fragments[1].pointer = (char*)s_msg + s_msg->data2_offset;
	fragments[1].size = s_msg->bytes2;

	if (filesystem_write_file_fragments(STRINGS_FILENAME, fragments, 2) < 0) {
		filesystem_delete_file(SETTINGS_FILENAME);
		return;
	}
    
	LOG_AND_SCREEN("Reboot...");
	reboot();
}






int logic_init() {

	bool ok = true;

	if (init_flags.settings_init) {

		//init logic


	}
	else
		ok = false;

	//прием новых конфигурационных файлов включаем в любом случае
	net_add_dispatcher((uint8_t)NET_MSG_SURZA_SETTINGS, settings_recv_callback);

	return ok ? 0 : (-1);
}




int read_settings() {

	LOG_AND_SCREEN("Load settings...");

	if (!filesystem_set_current_dir("C:\\") < 0) {
		LOG_AND_SCREEN("Filesystem i/o error!");
		return -1;
	}
	
	filesystem_fragment_t fragments[2];

	fragments[0].size = sizeof(settings_file_header_t);
	fragments[1].size = 0;

	//чтение файла настроек
	if (filesystem_read_file_fragments(SETTINGS_FILENAME, fragments, 2) < 0) {
		LOG_AND_SCREEN("Load settings file error!");
		return -1;
	}

	settings_header = (settings_file_header_t*) fragments[0].pointer;
	settings_data_ptr = fragments[1].pointer;

	//проверка считанных данных
	if (settings_header->size != fragments[1].size
		|| !crc32_check(settings_data_ptr, settings_header->size, settings_header->crc32) ) {
		LOG_AND_SCREEN("Settings file data corrupted!");
		return -1;
	}

	//чтение файла строк
	if (filesystem_read_file_fragments(STRINGS_FILENAME, fragments, 1) < 0) {
		LOG_AND_SCREEN("Load strings file header error!");
		return -1;
	}
	
	strings_header = (settings_file_header_t*) fragments[0].pointer;

	//проверка соотвествия файла настроек файлу строк (сравнением парных хэшей)
	if ( memcmp(settings_header->hash, strings_header->paired_hash, 16)
		 || memcmp(strings_header->hash, settings_header->paired_hash, 16)) {
		LOG_AND_SCREEN("Settings file & strings file not a pair!");
		return -1;
	}

	LOG_AND_SCREEN("Settings load ok");
	LOG_AND_SCREEN("Building param tree...");

	
	if (ParamTree_Make(settings_data_ptr, settings_header->size) < 0) {
		LOG_AND_SCREEN("Param tree build failed!");
		return -1;
	}

	LOG_AND_SCREEN("Param tree built");
	LOG_AND_SCREEN("Read settings OK");

	return 0;
}



