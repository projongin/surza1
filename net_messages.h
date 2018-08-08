#pragma once

#include <stdint.h>


#pragma pack(1)


//message types
enum GateMessageType{
	GATE_SPECIAL_MSG_TYPE_MAX_NUM = 9,

	//application message types
	NET_MSG_SURZA_FIRMWARE,
	NET_MSG_SURZA_SETTINGS,
	NET_MSG_TYPE2,
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
	uint32_t  data1_offset;   // (.settings[] offset in bytes from start of msg_type_settings_t struct) >>> for upward compatibility purpose <<<
	uint32_t  bytes1;
	uint8_t   md5_hash1[16];
	uint32_t  data2_offset;
	uint32_t  bytes2;
	uint8_t   md5_hash2[16];
	//dummy bytes possible []
	//uint8_t[bytes2] data2;
	//uint8_t[bytes1] data1;
} msg_type_settings_t;


typedef struct msg_type_byborg_info_tt {
	uint32_t  header[10];
	uint16_t  regs[100];
} msg_type_byborg_info_t;


#pragma pack()


