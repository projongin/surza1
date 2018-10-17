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
	MSG_TYPE_CLOSE_THREAD     //��������� ���������. ��� ������������� ������� ��������� ����� ��������� ������������ � ������������ � �������
};


enum MSG_STATUS {
	STATUS_OK,
	STATUS_FAULT
};

typedef struct {
	enum MSG_TYPE msg_type;     //��� ��������� - ������\������\������ ������ �����
	void* data_buf;             //����� � �������\��� ������
	int8_t adr;                 //����� ���������� ���������
	uint16_t Start;             //��������� ����� �������� ��� ������ ������ \ ���� ����� �����
	uint16_t Count;             //���-�� ��������� ��� ������\������
	uint8_t label;              //����� ���������(��� ���), ��� ��� ������� �� ������ ��������� � ����������� ���������
	enum MSG_STATUS status;     //������ �������� ��������� (����������� ��� ������)
} modbus_message_t;


enum {
	COM_STATUS_CLOSED,
	COM_STATUS_OPENED
};

//������ ��� �������� ������
//recv_buf - ����� � ��������� ����������
//recv_start_reg - ��������� �������� ������� ������
//recv_n - ���-�� �������� ���������
//���������� ���-�� ���������� ���������; 
//������������ �������� �� ������ �������� reg_num ��������, ��� �� ��� �������� ����������� (������������ ������ ��������� ��� ������ � ������)
//������������� �������� ��� ���� ����������, ��� ��� � ������������ ������ �� ���������
typedef int(*modbus_callback_SETREG)(const uint16_t* recv_buf, uint16_t recv_start_reg, uint16_t recv_n);

//������ ��� ������������ ������
//send_buf - ����� � ������������� ����������
//send_start_reg - ��������� ������������ ������� ������
//send_n - ���-�� ������������ ���������
//���������� ���-�� ���������� ���������
//������������ �������� �� ������ �������� reg_num ��������, ��� �� ��� �������� ��������� (������������ �������� ��� ������ � ������)
//������������� �������� ��� ���� ����������, ��� ��� ���������� ������ ��������� ������
typedef int(*modbus_callback_GETREG)(uint16_t* send_buf, uint16_t send_start_reg, uint16_t send_n);

//������ ��� ���������� ������, ���� ������������ ��������� ���������� � ��������� ������, ���� ������ ��� �������� ����� �������� � ����������
typedef void(*modbus_callback_UPDATE)(bool read_update, bool write_update);



void Modbus_Init();
void Modbus_DeInit();

void Modbus_Open(int COM_port, int baudrate, int parity, int stops, int irq /*=-1*/, unsigned int io_base /*=0*/);

void Modbus_Close();
bool Modbus_IsOpened();

//���������� ����� ��������� � ������� ������
//���������� false , ���� ������� ������� ������ ��� ���� �� ������
bool Modbus_message(const struct modbus_message_t* msg);


//�������� ������� ������
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

