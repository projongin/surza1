#pragma once


#include <Rtk32.h>
#include <Clock.h>
#include <stdint.h>
#include <stdbool.h>


enum MSG_TYPE {
	MSG_TYPE_WRITE_REG,
	MSG_TYPE_WRITE_REGS,
	MSG_TYPE_READ_REGS,
	MSG_TYPE_SET_COIL,
	MSG_TYPE_SET_COILS,
	MSG_TYPE_GET_COILS,
	MSG_TYPE_CLOSE_THREAD     //служебное сообщение. при использовании данного сообщение извне сообщение игнорируется и возвращается с ошибкой
};


enum MSG_STATUS {
	STATUS_OK,
	STATUS_FAULT
};

typedef struct {
	enum MSG_TYPE msg_type;     //тип сообщения - чтение\запись\запись одного флага
	void* data_buf;             //буфер с данными\для данных
	int8_t adr;                 //адрес назначения сообщения
	uint16_t Start;             //начальный номер регистра для чтения записи \ либо номер флага
	uint16_t Count;             //кол-во регистров для записи\чтения
	uint8_t label;              //метка сообщения(его тип), для его отличия от других сообщений в последующей обработке
	enum MSG_STATUS status;     //статус отправки сообщения (заполняется при приеме)
} modbus_message_t;


enum {
	COM_STATUS_CLOSED,
	COM_STATUS_OPENED
};

//колбек для принятых данных
//recv_buf - буфер с принятыми регистрами
//recv_start_reg - начальный принятый регистр модбас
//recv_n - кол-во принятых регистров
//возвращает кол-во записанных регистров; 
//возвращаемое значение по модулю неравное reg_num означает, что не все регистры применились (неприемлемые номера регистров или ошибка в данных)
//отрицательное значение при этом обозначает, что еще и записываемые данные не корректны
typedef int(*modbus_callback_SETREG)(const uint16_t* recv_buf, uint16_t recv_start_reg, uint16_t recv_n);

//колбек для отправляемых данных
//send_buf - буфер с отправляемыми регистрами
//send_start_reg - начальный отправляемый регистр модбас
//send_n - кол-во отправляемых регистров
//возвращает кол-во записанных регистров
//возвращаемое значение по модулю неравное reg_num означает, что не все регистры считались (неприемлемые регистры или ошибка в данных)
//отрицательное значение при этом обозначает, что при считывании данных произошла ошибка
typedef int(*modbus_callback_GETREG)(uint16_t* send_buf, uint16_t send_start_reg, uint16_t send_n);

//колбек для обновления данных, если формирование сообщения происходит в отдельном потоке, либо данные для отправки могут меняться в прерывании
typedef void(*modbus_callback_UPDATE)(bool read_update, bool write_update);



void Modbus_Init();
void Modbus_DeInit();

void Modbus_Open(int COM_port, int baudrate, int parity, int stops, int irq /*=-1*/, unsigned int io_base /*=0*/);

void Modbus_Close();
bool Modbus_IsOpened();

//записывает новое сообщение в очередь обмена
//возвращает false , если очередь слишком забита или порт не открыт
bool Modbus_message(const struct modbus_message_t* msg);


//мейлбокс очереди приема
extern RTKMailbox Modbus_recv_msgs;

void Modbus_fast_crc16(uint8_t* crc, uint8_t *buf, uint16_t buf_size);
int Modbus_check_crc16(uint8_t* buf, uint16_t buf_size, uint8_t* crc);

uint16_t Modbus_get_reg(const uint8_t* adr);
void Modbus_set_reg(uint8_t* adr, uint16_t reg);


/*
void Modbus_get_4_bytes(const uint8_t* adr, float* value);
void Modbus_get_4_bytes(const uint8_t* adr, uint32_t* value);
void Modbus_get_4_bytes(const uint8_t* adr, int32_t* value);
void Modbus_set_4_bytes(uint8_t* adr, float* value);
void Modbus_set_4_bytes(uint8_t* adr, uint32_t* value);
void Modbus_set_4_bytes(uint8_t* adr, int32_t* value);
*/

