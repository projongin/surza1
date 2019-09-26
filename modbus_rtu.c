
#include "modbus_rtu.h"

#include <math.h>
#include <Clock.h>
#include <rtcom.h>
#include <Rtk32.h> 

#include "common.h"
#include "global_defines.h"


#define MODBUS_PRIORITY (RTKConfig.MainPriority+10)


#define  N_OF_ATTEMPTS   3    //кол-во попыток повторной передачи сообщения
#define  MAX_REGS        100  //максимальное кол-во регистров modbus в одном сообщении

//максимальное количество сообщение в очереди приемки и отправки
#define  QUEUE_MAX_LENGTH   50


const char *recv_mailbox_name = "Modbus recv";
const char *send_mailbox_name = "Modbus send";
const char *modbus_poll_thread_name = "Modbus poll";
const char *modbus_mutex_name = "Modbus mutex";


//мейлбокс очереди приема
RTKMailbox Modbus_recv_msgs;


//данные для обрабатывающего потока под защитой мьютекса
static RTKSemaphore mutex;
volatile bool stop_thread_flag;
static uint8_t port_state;
//--------------------------------------------------------
/*uint32_t baudrate;*/
static int port;


/* для потока обмена */
static modbus_message_t msg;
static uint16_t part_reg;
static uint16_t NOfRegs;
/**********************/

static void RTKAPI task_func(void* param);
static void RTKAPI Modbus_poll_thread(void* param);

static RTKDuration modbus_delay_in_ticks;
static DWORD usec_per_byte;

static RTKDuration get_bytes_time(unsigned bytes) { return CLKMicroSecsToTicks(bytes*usec_per_byte + usec_per_byte * 10); }
static RTKDuration get_byte_time() { return CLKMicroSecsToTicks(usec_per_byte + 5000); }
static RTKDuration byte_time;

//мейлбокс очереди отправки
static RTKMailbox Modbus_send_msgs;

//флаг признак работы в режиме сервера
static bool is_server;
static uint8_t serv_adr;
static modbus_callback_SETREG set_reg_callback;
static modbus_callback_GETREG get_reg_callback;
static modbus_callback_UPDATE update_regs_callback;

static int Modbus_recv_bytes(uint8_t* buf, unsigned num);

static void Modbus_SendException(uint8_t adr, uint8_t error_code, uint8_t exception_code);
static void Modbus_Send(uint8_t* msg, unsigned bytes);


static int Modbus_SetReg();
static int Modbus_GetRegs();
static int Modbus_SetRegs();
static int Modbus_GetCoils();
static int Modbus_SetCoils();
static int Modbus_SetCoil();

static const uint8_t auchCRCHi[256];
static const uint8_t auchCRCLo[256];


const uint8_t auchCRCHi[] = {
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
	0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40 };

const uint8_t auchCRCLo[] = {
	0x00,0xC0,0xC1,0x01,0xC3,0x03,0x02,0xC2,0xC6,0x06,0x07,0xC7,0x05,0xC5,0xC4,0x04,
	0xCC,0x0C,0x0D,0xCD,0x0F,0xCF,0xCE,0x0E,0x0A,0xCA,0xCB,0x0B,0xC9,0x09,0x08,0xC8,
	0xD8,0x18,0x19,0xD9,0x1B,0xDB,0xDA,0x1A,0x1E,0xDE,0xDF,0x1F,0xDD,0x1D,0x1C,0xDC,
	0x14,0xD4,0xD5,0x15,0xD7,0x17,0x16,0xD6,0xD2,0x12,0x13,0xD3,0x11,0xD1,0xD0,0x10,
	0xF0,0x30,0x31,0xF1,0x33,0xF3,0xF2,0x32,0x36,0xF6,0xF7,0x37,0xF5,0x35,0x34,0xF4,
	0x3C,0xFC,0xFD,0x3D,0xFF,0x3F,0x3E,0xFE,0xFA,0x3A,0x3B,0xFB,0x39,0xF9,0xF8,0x38,
	0x28,0xE8,0xE9,0x29,0xEB,0x2B,0x2A,0xEA,0xEE,0x2E,0x2F,0xEF,0x2D,0xED,0xEC,0x2C,
	0xE4,0x24,0x25,0xE5,0x27,0xE7,0xE6,0x26,0x22,0xE2,0xE3,0x23,0xE1,0x21,0x20,0xE0,
	0xA0,0x60,0x61,0xA1,0x63,0xA3,0xA2,0x62,0x66,0xA6,0xA7,0x67,0xA5,0x65,0x64,0xA4,
	0x6C,0xAC,0xAD,0x6D,0xAF,0x6F,0x6E,0xAE,0xAA,0x6A,0x6B,0xAB,0x69,0xA9,0xA8,0x68,
	0x78,0xB8,0xB9,0x79,0xBB,0x7B,0x7A,0xBA,0xBE,0x7E,0x7F,0xBF,0x7D,0xBD,0xBC,0x7C,
	0xB4,0x74,0x75,0xB5,0x77,0xB7,0xB6,0x76,0x72,0xB2,0xB3,0x73,0xB1,0x71,0x70,0xB0,
	0x50,0x90,0x91,0x51,0x93,0x53,0x52,0x92,0x96,0x56,0x57,0x97,0x55,0x95,0x94,0x54,
	0x9C,0x5C,0x5D,0x9D,0x5F,0x9F,0x9E,0x5E,0x5A,0x9A,0x9B,0x5B,0x99,0x59,0x58,0x98,
	0x88,0x48,0x49,0x89,0x4B,0x8B,0x8A,0x4A,0x4E,0x8E,0x8F,0x4F,0x8D,0x4D,0x4C,0x8C,
	0x44,0x84,0x85,0x45,0x87,0x47,0x46,0x86,0x82,0x42,0x43,0x83,0x41,0x81,0x80,0x40 };


//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void Modbus_fast_crc16(uint8_t* crc, uint8_t *buf, uint16_t buf_size) {
	uint32_t uIndex;
	crc[0] = 0xFF;
	crc[1] = 0xFF;

	while (buf_size)
	{
		uIndex = crc[0] ^ *buf++;
		crc[0] = crc[1] ^ auchCRCHi[uIndex];
		crc[1] = auchCRCLo[uIndex];
		buf_size--;
	}
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int Modbus_check_crc16(uint8_t *buf, uint16_t buf_size, uint8_t* crc) {
	uint8_t crc_new[2];

	Modbus_fast_crc16(crc_new, buf, buf_size);
	if (crc[0] == crc_new[0] && crc[1] == crc_new[1]) return 1;
	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


void Modbus_Init(){
	DEBUG_ADD_POINT(330);

	//создаю очереди приемки и отправки
	Modbus_send_msgs = RTKCreateMailbox(sizeof(modbus_message_t), QUEUE_MAX_LENGTH, send_mailbox_name);
	Modbus_recv_msgs = RTKCreateMailbox(sizeof(modbus_message_t), QUEUE_MAX_LENGTH, recv_mailbox_name);

	//создаю мьютекс
	mutex = RTKCreateSemaphore(ST_MUTEX, 1, modbus_mutex_name);

	stop_thread_flag = false;
	modbus_delay_in_ticks = 1;

	port_state = COM_STATUS_CLOSED;

	RTKRTLCreateThread(Modbus_poll_thread, MODBUS_PRIORITY, 0, 0, 0, modbus_poll_thread_name);

}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void Modbus_DeInit(){
	DEBUG_ADD_POINT(331);

	bool status = true;
	modbus_message_t msg;

	RTKWait(mutex);
	stop_thread_flag = true;
	RTKSignal(mutex);

	msg.msg_type = MSG_TYPE_CLOSE_THREAD;
	RTKPut(Modbus_send_msgs, &msg);

	do {
		RTKWait(mutex);
		status = stop_thread_flag;
		RTKSignal(mutex);
	} while (status);

	RTKDeleteSemaphore(&mutex);

	//очистка очередей
	RTKDeleteMailbox(&Modbus_send_msgs);
	RTKDeleteMailbox(&Modbus_recv_msgs);

	Modbus_send_msgs = 0;
	Modbus_recv_msgs = 0;

	Modbus_Close();
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void Modbus_Open(int com_port, int baudrate, int parity, int stops, int irq, unsigned int io_base) {
	DEBUG_ADD_POINT(332);

	//usec_per_byte = (DWORD)ceil((float)1000000*11/(float)baudrate);
	usec_per_byte = (DWORD)(1000000 * 11) / baudrate;
	modbus_delay_in_ticks = (RTKDuration)CLKMicroSecsToTicks((DWORD)(usec_per_byte * 3.5 + 1)) + 1;

	byte_time = get_byte_time();

	port = com_port;

	if (irq >= 0) {
		// устанавливается определенное прерывание и адрес ввода-вывода
		COMSetIOBase(port, io_base);
		COMSetIRQ(port, irq);
	}

	COMPortInit(port, baudrate, parity, stops, 8);
	COMSetProtocol(port, COM_PROT_NONE, 0, 0);
	COMEnableInterrupt(port, 1024);

	RTKClearMailbox(COMReceiveBuffer[port]);

	RTKWait(mutex);
	port_state = COM_STATUS_OPENED;
	RTKSignal(mutex);
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void Modbus_Close() {
	DEBUG_ADD_POINT(333);

	RTKWait(mutex);
	port_state = COM_STATUS_CLOSED;
	RTKSignal(mutex);

	COMDisableInterrupt(port);
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

bool Modbus_IsOpened() {
	return (port_state == COM_STATUS_OPENED) ? true : false;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_PollTarget() {
	switch (msg.msg_type) {
	case MSG_TYPE_WRITE_REG:   return Modbus_SetReg();
	case MSG_TYPE_WRITE_REGS:  return Modbus_SetRegs();
	case MSG_TYPE_READ_REGS:   return Modbus_GetRegs();
	case MSG_TYPE_SET_COIL:    return Modbus_SetCoil();
	case MSG_TYPE_SET_COILS:   return Modbus_SetCoils();
	case MSG_TYPE_GET_COILS:   return Modbus_GetCoils();
	default: return -1;
	}
}



void RTKAPI Modbus_poll_thread(void* param) {
	bool exit, timeout_processed = false;
	int attempts, res;
	uint8_t local_port_state;

	DEBUG_ADD_POINT(334);

	while (true) {


		RTKGet(Modbus_send_msgs, &msg);

		DEBUG_ADD_POINT(335);


		RTKWait(mutex);
		exit = stop_thread_flag;
		local_port_state = port_state;
		RTKSignal(mutex);


		if (msg.msg_type == MSG_TYPE_CLOSE_THREAD) {

			if (exit) {
				RTKWait(mutex);
				stop_thread_flag = false;
				RTKSignal(mutex);
				break;
			}
			else {
				msg.status = STATUS_FAULT;
				RTKPut(Modbus_recv_msgs, &msg);
			}
		}

		if (local_port_state != COM_STATUS_OPENED) {
			msg.status = STATUS_FAULT;
			RTKPut(Modbus_recv_msgs, &msg);
			RTKDelay(modbus_delay_in_ticks << 8);
			timeout_processed = true;
			continue;
		}

		part_reg = 0;

		//расчет кол-ва данных для записи\чтения
		switch (msg.msg_type) {
		case MSG_TYPE_WRITE_REGS:
		case MSG_TYPE_READ_REGS:
			if (msg.Count>MAX_REGS) NOfRegs = MAX_REGS;
			else  NOfRegs = msg.Count;
			break;

		case MSG_TYPE_GET_COILS:
		case MSG_TYPE_SET_COILS:
			//в MAX_REGS байтах вмещаются максимум MAX_REGS*16 флагов
			if (msg.Count>MAX_REGS * 16) NOfRegs = MAX_REGS * 16;
			else  NOfRegs = msg.Count;
			if (msg.Count>0x7b0) msg.Count = 0x7b0;
			break;

		case MSG_TYPE_WRITE_REG:
		case MSG_TYPE_SET_COIL:
			if (msg.Count>1) msg.Count = 1;
			NOfRegs = msg.Count;
			break;

		};


		attempts = N_OF_ATTEMPTS;
		while (attempts && !exit && part_reg<msg.Count) {

			attempts--;
			if (!timeout_processed) { RTKDelay(modbus_delay_in_ticks); timeout_processed = true; }

			res = Modbus_PollTarget();
			if (res == 0) {
				timeout_processed = false;
				attempts = N_OF_ATTEMPTS;
				part_reg += NOfRegs;

				//пересчет кол-ва оставшихся данных для записи\чтения
				switch (msg.msg_type) {
				case MSG_TYPE_WRITE_REGS:
				case MSG_TYPE_READ_REGS:
					if (msg.Count - part_reg>MAX_REGS) NOfRegs = MAX_REGS;
					else  NOfRegs = msg.Count - part_reg;
					break;

				case MSG_TYPE_GET_COILS:
				case MSG_TYPE_SET_COILS:
					//в MAX_REGS байтах вмещаются максимум MAX_REGS*16 флагов
					if (msg.Count - part_reg>MAX_REGS * 16) NOfRegs = MAX_REGS * 16;
					else  NOfRegs = msg.Count - part_reg;
					break;
				};
			}

			RTKWait(mutex);
			exit = stop_thread_flag;
			RTKSignal(mutex);

		}

		DEBUG_ADD_POINT(336);

		if (res == 0 && !exit) {
			// успешный обмен
			msg.status = STATUS_OK;
			RTKPut(Modbus_recv_msgs, &msg);
		}
		else {
			DEBUG_ADD_POINT(337);

			msg.status = STATUS_FAULT;
			RTKPut(Modbus_recv_msgs, &msg);
		}

	}
}


//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_SetReg() {
	DEBUG_ADD_POINT(338);

	uint8_t modbus_msg[MAX_REGS * 2 + 10];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t value, len, reg;
	int i;

	reg = msg.Start;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x06;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);

	value = *(uint16_t*)msg.data_buf;
	modbus_msg[4] = (uint8_t)((value >> 8) & 0x00ff);
	modbus_msg[5] = (uint8_t)(value & 0x00ff);

	Modbus_fast_crc16(&modbus_msg[6], modbus_msg, 6);

	len = 8;

	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50 + 8)) + 1;
	timeout = RTKGetTime() + period;

	//очищаю все даныные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;


	//получение ответа
	len = 8;
	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности 
	if (modbus_msg[2] != ((reg >> 8) & 0x00ff) || modbus_msg[3] != (reg & 0x00ff)
		|| modbus_msg[4] != ((value >> 8) & 0x00ff) || modbus_msg[5] != (value & 0x00ff)) return -7;

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_GetRegs() {
	DEBUG_ADD_POINT(339);

	uint8_t modbus_msg[MAX_REGS * 2 + 6];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t value, num, len, reg;
	int i;

	reg = msg.Start + part_reg;
	num = NOfRegs;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x03;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);
	modbus_msg[4] = 0;
	modbus_msg[5] = (num & 0xff);
	Modbus_fast_crc16((uint8_t*)&modbus_msg[6], modbus_msg, 6);

	len = 8;

	//очищаю все даныные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;

	//получение ответа
	len = num * 2 + 5;
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50)) + 1;
	timeout = RTKGetTime() + period;

	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности кол-ва байт
	if (modbus_msg[2] != num * 2) return -7;


	//обработка сообщения
	for (i = 0; i<num; i++) {
		value = ((((uint16_t)modbus_msg[i * 2 + 3]) << 8) | (uint16_t)modbus_msg[i * 2 + 4]);
		((uint16_t*)msg.data_buf)[part_reg + i] = value;
	}

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_SetRegs() {
	DEBUG_ADD_POINT(340);

	uint8_t modbus_msg[MAX_REGS * 2 + 10];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t value, num, len, reg;
	int i;

	reg = msg.Start + part_reg;
	num = NOfRegs;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x10;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);
	modbus_msg[4] = 0;
	modbus_msg[5] = (num & 0xff);
	modbus_msg[6] = ((num << 1) & 0xff);

	for (i = 0; i<num; i++) {
		value = ((uint16_t*)msg.data_buf)[part_reg + i];
		modbus_msg[i * 2 + 7] = (uint8_t)((value >> 8) & 0x00ff);
		modbus_msg[i * 2 + 8] = (uint8_t)(value & 0x00ff);
	}

	len = num * 2 + 7;
	Modbus_fast_crc16(&modbus_msg[len], modbus_msg, len);

	len += 2;

	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50 + 8)) + 1;
	timeout = RTKGetTime() + period;

	//очищаю все данные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;


	//получение ответа
	len = 8;
	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности кол-ва байт
	if (modbus_msg[2] != ((reg >> 8) & 0x00ff) || modbus_msg[3] != (reg & 0x00ff)
		|| modbus_msg[4] != 0 || modbus_msg[5] != (num & 0xff)) return -7;

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_SetCoils() {
	DEBUG_ADD_POINT(341);

	uint8_t modbus_msg[MAX_REGS * 2 + 10];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t num, len, reg;
	int i;

	reg = msg.Start + part_reg;
	num = NOfRegs;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x0f;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);
	modbus_msg[4] = 0;
	modbus_msg[5] = (num & 0xff);
	modbus_msg[6] = num / 8 + ((num % 8) ? 1 : 0);


	if (part_reg) num = part_reg / 8;
	else num = 0;

	for (i = 0; i<modbus_msg[6]; i++)
		modbus_msg[7 + i] = ((uint8_t*)msg.data_buf)[num + i];

	//записываю нулями лишние биты
	i = NOfRegs % 8;
	if (i) modbus_msg[7 + modbus_msg[6] - 1] &= (((uint8_t)0xff) >> (8 - i));

	//контрольная сумма
	len = 7 + modbus_msg[6];
	Modbus_fast_crc16(&modbus_msg[len], modbus_msg, len);

	len += 2;

	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50 + 8)) + 1;
	timeout = RTKGetTime() + period;

	//очищаю все даныные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;


	//получение ответа
	len = 8;
	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности кол-ва байт
	if (modbus_msg[2] != ((reg >> 8) & 0x00ff) || modbus_msg[3] != (reg & 0x00ff)
		|| modbus_msg[4] != 0 || modbus_msg[5] != (NOfRegs & 0xff)) return -7;

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_GetCoils() {
	DEBUG_ADD_POINT(342);

	uint8_t modbus_msg[MAX_REGS * 2 + 6];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t num, len, reg;
	int i;

	reg = msg.Start + part_reg;
	num = NOfRegs;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x01;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);
	modbus_msg[4] = (uint8_t)((num >> 8) & 0x00ff);
	modbus_msg[5] = (uint8_t)(num & 0x00ff);
	Modbus_fast_crc16((uint8_t*)&modbus_msg[6], modbus_msg, 6);

	len = 8;

	//очищаю все даныные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;


	//получение ответа
	len = 5 + num / 8 + ((num % 8) ? 1 : 0);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50)) + 1;
	timeout = RTKGetTime() + period;

	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности кол-ва байт
	if (modbus_msg[2] != len - 5) return -7;


	//обработка сообщения
	if (part_reg) num = part_reg / 8;
	else num = 0;
	for (i = 0; i<len - 5; i++)
		((uint8_t*)msg.data_buf)[num + i] = modbus_msg[3 + i];

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


static int Modbus_SetCoil() {
	DEBUG_ADD_POINT(343);

	uint8_t modbus_msg[MAX_REGS * 2 + 10];
	RTKTime timeout;
	RTKDuration period;
	RTKBool res;
	COMData data;
	uint16_t len, reg;
	int i;

	reg = msg.Start;

	//составляю сообщение
	modbus_msg[0] = msg.adr;
	modbus_msg[1] = 0x05;
	modbus_msg[2] = (uint8_t)((reg >> 8) & 0x00ff);
	modbus_msg[3] = (uint8_t)(reg & 0x00ff);
	if (*(uint16_t*)msg.data_buf) modbus_msg[4] = 0xff;
	else modbus_msg[4] = 0x00;
	modbus_msg[5] = 0x00;

	Modbus_fast_crc16(&modbus_msg[6], modbus_msg, 6);

	len = 8;

	period = CLKMicroSecsToTicks(usec_per_byte * (len + 50 + 8)) + 1;
	timeout = RTKGetTime() + period;

	//очищаю все даныные в приемнике и отправляю сообщение
	RTKClearMailbox(COMReceiveBuffer[port]);
	period = CLKMicroSecsToTicks(usec_per_byte * (len + 20)) + 1;
	if (COMSendBlockTimed(port, modbus_msg, len, period) != len) return -1;


	//получение ответа
	len = 8;
	i = 0;

	//пытаюсь получить первые два байта ответа
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<2);

	//проверка на совпадение адреса запроса с адресом ответа
	if (modbus_msg[0] != msg.adr) return -4;
	//проверка на код ошибки modbus
	if (modbus_msg[1] & 0x80) len = 5;

	//попытка получить оставшиеся байты ответного сообщения
	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, timeout);
		if (res != TRUE) return -2;
		if (data & 0xFF00) return -3;
		else modbus_msg[i] = (data & 0xff);
		i++;
	} while (i<len);

	//проверка полученного сообщения
	if (!Modbus_check_crc16(modbus_msg, len - 2, &modbus_msg[len - 2])) return -5;

	//код ошибки modbus
	if (modbus_msg[1] & 0x80) return -6;

	//проверка правильности
	if ((*(uint8_t*)msg.data_buf)) i = 0xff; else i = 0x00;
	if (modbus_msg[2] != ((reg >> 8) & 0x00ff) || modbus_msg[3] != (reg & 0x00ff)
		|| modbus_msg[4] != i || modbus_msg[5] != 0) return -7;

	return 0;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------


bool Modbus_message(const struct modbus_message_t* msg_data) {
	DEBUG_ADD_POINT(344);
	if (!RTKPutCond(Modbus_send_msgs, msg_data)) return false;
	else return true;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------



static int Modbus_recv_bytes(uint8_t* buf, unsigned num) {
	DEBUG_ADD_POINT(345);

	RTKTime timeout = RTKGetTime() + get_bytes_time(num);
	COMData data;
	RTKBool res;
	int cnt = 0;

	do {
		res = RTKGetUntil(COMReceiveBuffer[port], &data, /*RTKGetTime() + byte_time*/timeout);
		if (res == TRUE) {
			if (!(data & 0xFF00)) {
				*buf++ = (data & 0xff);
				num--;
				cnt++;
			}
			else {
				res = FALSE;
			}
		}
	} while (num && res == TRUE);

	return cnt;
}


uint16_t Modbus_get_reg(const uint8_t* adr) {
	uint16_t reg = *adr++;
	reg <<= 8;
	reg += *adr;
	return reg;
}

void Modbus_set_reg(uint8_t* adr, uint16_t reg) {
	*adr++ = (uint8_t)((reg >> 8) & 0x00ff);
	*adr = (uint8_t)(reg & 0x00ff);
}

/*
void Modbus_get_4_bytes(const uint8_t* adr, float* value){
uint8_t *ptr = (uint8_t*)(value);
ptr[0]=adr[1];
ptr[1]=adr[0];
ptr[2]=adr[3];
ptr[3]=adr[2];
}

void Modbus_get_4_bytes(const uint8_t* adr, uint32_t* value){
uint8_t *ptr = (uint8_t*)(value);
ptr[0]=adr[1];
ptr[1]=adr[0];
ptr[2]=adr[3];
ptr[3]=adr[2];
}

void Modbus_get_4_bytes(const uint8_t* adr, int32_t* value){
uint8_t *ptr = (uint8_t*)(value);
ptr[0]=adr[1];
ptr[1]=adr[0];
ptr[2]=adr[3];
ptr[3]=adr[2];
}

void Modbus_get_2_bytes(const uint8_t* adr, int16_t* value){
uint8_t *ptr = (uint8_t*)(value);
ptr[0]=adr[1];
ptr[1]=adr[0];
}



void Modbus_set_4_bytes(uint8_t* adr, float* value){
uint8_t *ptr = (uint8_t*)(value);
adr[0]=ptr[1];
adr[1]=ptr[0];
adr[2]=ptr[3];
adr[3]=ptr[2];
}


void Modbus_set_4_bytes(uint8_t* adr, uint32_t* value){
uint8_t *ptr = (uint8_t*)(value);
adr[0]=ptr[1];
adr[1]=ptr[0];
adr[2]=ptr[3];
adr[3]=ptr[2];
}


void Modbus_set_4_bytes(uint8_t* adr, int32_t* value){
uint8_t *ptr = (uint8_t*)(value);
adr[0]=ptr[1];
adr[1]=ptr[0];
adr[2]=ptr[3];
adr[3]=ptr[2];
}

void Modbus_set_2_bytes(uint8_t* adr, int16_t* value){
uint8_t *ptr = (uint8_t*)(value);
adr[0]=ptr[1];
adr[1]=ptr[0];
}

void Modbus_set_2_bytes(uint8_t* adr, uint16_t* value){
uint8_t *ptr = (uint8_t*)(value);
adr[0]=ptr[1];
adr[1]=ptr[0];
}
*/


static void Modbus_SendException(uint8_t adr, uint8_t error_code, uint8_t exception_code) {
	DEBUG_ADD_POINT(346);

	uint8_t buf[5];
	int bytes = 3;

	buf[0] = adr;
	buf[1] = (error_code | 0x80);
	buf[2] = exception_code;

	Modbus_fast_crc16(&buf[3], buf, 3);
	bytes += 2;

	COMSendBlockTimed(port, buf, bytes, get_bytes_time(bytes + 5));
}

static void Modbus_Send(uint8_t* msg, unsigned bytes) {
	DEBUG_ADD_POINT(347);

	Modbus_fast_crc16(&msg[bytes], &msg[0], bytes);
	bytes += 2;

	COMSendBlockTimed(port, msg, bytes, get_bytes_time(bytes + 5));
}

