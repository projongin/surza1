
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


static bool check_settings_msg(msg_type_settings_t* msg) {

	return true;
}




void settings_recv_callback(net_msg_t* msg, uint64_t channel) {

	LOG_AND_SCREEN("New settings message received");

	if (msg->size < sizeof(msg_type_settings_t))
		return;

	msg_type_settings_t* s_msg  = (msg_type_settings_t*) &msg->data[0];
	if (!check_settings_msg(s_msg))
		return;


	settings_file_header_t header;
	filesystem_fragment_t fragments[2];

	header.size = s_msg->bytes1;
	header.crc32 = crc32((char*)s_msg+s_msg->data1_offset, header.size);
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
	header.crc32 = crc32((char*)s_msg + s_msg->data2_offset, header.size);
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



	




	net_add_dispatcher((uint8_t)NET_MSG_SURZA_SETTINGS, settings_recv_callback);

	return ok ? 0 : (-1);;
}




int read_settings() {






	return 0;
}