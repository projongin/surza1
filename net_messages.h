#pragma once

#include <stdint.h>


#pragma pack(1)


//message types
enum GateMessageType{
	GATE_SPECIAL_MSG_TYPE_MAX_NUM = 9,

	//application message types
	NET_MSG_SURZA_FIRMWARE,
	NET_MSG_SURZA_SETTINGS,
	NET_MSG_SETTINGS_REQUEST,
	NET_MSG_INDI,

	NET_MSG_VYBORG_INFO = 40
};




//firmware
typedef struct msg_type_firmware_tt {
	uint32_t  crc32;         // crc32 for rest of struct
	uint32_t  data_offset;   // (.firmware[] offset in bytes from start of msg_type_firmware_t struct) >>> for upward compatibility purpose <<<
	uint32_t  bytes;
	//dummy bytes possible []
	//uint8_t[bytes] firmware;
} msg_type_firmware_t;

//settings
typedef struct msg_type_settings_tt {
	uint32_t  crc32;      // crc32 for rest of struct
	uint32_t  data_offset;   // (.settings[] offset in bytes from start of msg_type_settings_t struct) >>> for upward compatibility purpose <<<
	uint32_t  bytes;
	uint8_t   md5_hash[16];
	//dummy bytes possible []
	//uint8_t[bytes] data;
} msg_type_settings_t;


//settings request
typedef struct msg_type_settings_request_tt {
	uint8_t   md5_hash[16];    //request settings with hash 'md5_hash', or current settings in case md5_hash contains all zeros
} msg_type_settings_request_t;


//indi
typedef struct msg_type_indi_tt {
	uint32_t   header_size;     //sizeof(msg_type_settings_request_t) (for upward compatibility)
	uint8_t    md5_hash[16];    //current settings hash
	uint32_t   in_real_num;
	uint32_t   in_real_offset;
	uint32_t   in_int_num;
	uint32_t   in_int_offset;
	uint32_t   in_bool_num;
	uint32_t   in_bool_offset;
	uint32_t   out_real_num;
	uint32_t   out_real_offset;
	uint32_t   out_int_num;
	uint32_t   out_int_offset;
	uint32_t   out_bool_num;
	uint32_t   out_bool_offset;
	uint64_t   time;
	// add additional fields here
		
} msg_type_indi_t;




typedef struct msg_type_byborg_info_tt {
	uint32_t  header[10];
	uint16_t  regs[100];
} msg_type_byborg_info_t;




#pragma pack()


