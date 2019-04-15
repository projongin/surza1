#pragma once

#include <stdint.h>


#pragma pack(push)
#pragma pack(1) 

//message types
enum GateMessageType{
	GATE_SPECIAL_MSG_TYPE_LOOPBACK = 0,
	GATE_SPECIAL_MSG_TYPE_WATCHDOG,
	GATE_SPECIAL_MSG_TYPE_SUBSCRIBE,
	GATE_SPECIAL_MSG_TYPE_UNSUBSCRIBE,
	GATE_SPECIAL_MSG_TYPE_CHANNEL_PRIRORITY,
	//add new special types here

	GATE_SPECIAL_MSG_TYPE_MAX_NUM = 9,

	//application message types
	NET_MSG_SURZA_FIRMWARE,
	NET_MSG_SURZA_SETTINGS,
	NET_MSG_SETTINGS_REQUEST,
	NET_MSG_INDI,
	NET_MSG_SET_PARAM,
	NET_MSG_JOURNAL_EVENT,
	NET_MSG_JOURNAL_INFO,
	NET_MSG_JOURNAL_REQUEST,
	NET_MSG_OSCILLOSCOPE,
	NET_MSG_SET_INPUT,
	NET_GATE_MSG_IEC60870_DATA,
	NET_GATE_MSG_IEC60870_EVENTS,
	NET_MSG_COMMAND,

	NET_MSG_VYBORG_INFO = 40
};



typedef struct {
	int64_t  secs;   //seconds (unix time)
	uint32_t nsecs;  //nanoseconds
	uint64_t steady_nsecs; //steady clock nsecs
} surza_time_t;



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
	uint32_t   header_size;     //sizeof(msg_type_indi_tt) (for upward compatibility)
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
	surza_time_t time;
	uint32_t   launch_num;
	// add additional fields here
		
} msg_type_indi_t;



// set parameter
typedef struct{
	uint8_t hash[16]; //config id
	uint16_t num;   // param num (0-0xfffe),    0xffff - set all to default
	union {
		float f32;
		int32_t i32;
	} value;
} msg_type_set_param_t;



// journal messages

// journal events
typedef struct {
	uint8_t  md5_hash[16];
	uint64_t unique_id;
	surza_time_t time;
	uint32_t n_of_events;
	uint32_t events_offset;
	uint32_t n_of_data_real;   //events data of type REAL
	uint32_t data_real_offset; //REAL data offset
	uint32_t n_of_data_int;
	uint32_t data_int_offset;
	uint32_t n_of_data_bool;
	uint32_t data_bool_offset;
	//add new fields here
} msg_type_journal_event_t;

// journal info
typedef struct {
	uint8_t  md5_hash[16];
	uint32_t events_num;
	uint64_t head_id;
	uint64_t tail_id;
} msg_type_journal_info_t;

#define  MSG_JOURNAL_REQUEST_GET     0
#define  MSG_JOURNAL_REQUEST_DELETE  1

// journal request
typedef struct {
	uint32_t request;
	uint64_t event_id;   //delete events till id], or get event #id
} msg_type_journal_request_t;




//-------------------------------------------------------------------
//   oscilloscope
//-------------------------------------------------------------------
#define OSCILLOSCOPE_MSG_NEW             0
#define OSCILLOSCOPE_MSG_DATA            1
#define OSCILLOSCOPE_MSG_REQUEST_NEW     2
#define OSCILLOSCOPE_MSG_REQUEST_DATA    3
#define OSCILLOSCOPE_MSG_REQUEST_DELETE  4

struct oscilloscope_header_t {
	uint64_t id;                 //unique id
	uint32_t parts;              //num of parts
	uint32_t step_time_nsecs;    //step time in nsecs
	uint32_t total_length;       //num of steps
	uint32_t num_real;           //num of float
	uint32_t num_int;            //num of int32_t
	uint32_t num_bool;           //num of bool (uint8_t)
	uint32_t total_length_bytes; //total osc data size in bytes
	surza_time_t time;           //first step time
	uint8_t  reserved[16];
};

typedef struct {
	uint32_t type;
	uint32_t size;
	uint8_t  md5_hash[16];
	//uint8_t msg[size];
} msg_type_oscilloscope_t;

// osc msg types:

typedef struct oscilloscope_header_t oscilloscope_new_t;

typedef struct {
	uint64_t id;
	uint32_t part;
	uint32_t part_length_bytes;
	uint8_t  reserved[8];
	//uintt8_t data[part_length_bytes];
}  oscilloscope_data_t;

typedef struct {
	uint64_t id;
	uint32_t part;
	uint8_t  reserved[8];
} oscilloscope_request_data_t;

typedef struct {
	uint64_t id;
	uint8_t  reserved[8];
} oscilloscope_request_delete_t;

//-------------------------------------------------------------------



// set surza input data

#define SURZA_INPUT_TYPE_FLOAT    0
#define SURZA_INPUT_TYPE_INT32    1
#define SURZA_INPUT_TYPE_BOOL     2

typedef struct {
	uint32_t type;   // SURZA_INPUT_TYPE
	uint32_t index;  // input index
	union {
		float    f;
		int32_t  i;
		uint32_t b;
	} val;
} input_value_t;

typedef struct {
	uint8_t  hash[16]; //config id
	uint32_t num;
	uint8_t  reserv[16];
	//input_value_t values[num];
} msg_type_set_input_t;

//----------------------------------





// commands ------------------------

typedef struct msg_type_command_tt {
	uint8_t  hash[16]; //config id
	uint32_t index;    //BOOL_IN index
	uint8_t  reserv[16];
} msg_type_command_t;

//----------------------------------






typedef struct msg_type_byborg_info_tt {
	uint32_t  header[10];
	//uint16_t  regs[100];
} msg_type_byborg_info_t;


#pragma pack(pop)

