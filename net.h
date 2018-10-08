#pragma once

#include <Rtk32.h>

#include <stddef.h>
#include <stdint.h>


#define _NET_DEBUG_PRINT

//--- �������������� ���������� ���������� ���������� ��� ���������� �������
#ifndef ALLOW_DEBUG_PRINT
#ifdef NET_DEBUG_PRINT
#undef NET_DEBUG_PRINT
#endif
#endif



//����������� ��������� ����� ������������� �����������
#define  NET_MAX_CONNECTIONS_ALLOWED  5

//����������� ���������� ���������� ������ � ������������ ���������
#define  NET_MAX_MSG_DATA_LENGTH    (32*1024*1024)



//����� ������ ��� �������� ���� ������������ ��������
#define NET_BROADCAST_CHANNEL          UINT64_MAX



typedef enum {
	NET_ERR_NO_ERROR = 0,      //��� ������
	NET_ERR_ANY,               //�����, �������������� ������ (����� ��� ������ �� �����)
	NET_ERR_QUEUE_OVERFLOW,    //������������ ������� ���������
	NET_ERR_MEM_ALLOC,         //������ ��������� ������
	NET_ERR_NO_CONNECTIONS,    //��� �������� ����������
	NET_ERR_INVALID_MSG        //������������ ���������
}  net_err_t;

typedef enum {
	NET_PRIORITY_HIGHEST = 0,
	NET_PRIORITY_HIGH,
	NET_PRIORITY_MEDIUM,
	NET_PRIORITY_LOW,
	NET_PRIORITY_LOWEST,
	NET_PRIORITY_BACKGROUND
} net_msg_priority_t;


#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint8_t  type;           //���� ���������
	uint8_t  subtype;        //������
	uint32_t size;           //����� ������
	uint8_t  data[];         //������ ���������
} net_msg_t;
#pragma pack(pop)


//��� ����������� ���������
typedef void(*net_msg_dispatcher)(net_msg_t* msg, uint64_t channel);

//��� ����������� ������ ��������� �������, ���������� �� ���������� �������� �����������
typedef void(*net_realtime_callback)(const void* data, int length);


//������������� ����������� � �������� tcp �������
//������ ��������� ��-��������� � ������ ��������� ������� ����� ����  NULL ���� �� ������������
net_err_t net_init(net_msg_dispatcher default_dispatcher, net_realtime_callback real_callback);


//���������� ��������� ���������
void net_start(void);
//������ ��������� ���������
void net_stop(void);

//���������� ����������� ��������� ������������� ����
void net_add_dispatcher(uint8_t type, net_msg_dispatcher dispatcher);


//���������� ���������� ������������ ��������, =0 ���� ��� ����������
volatile unsigned net_connections();


//��������� ������ ������ ��� ��������� � ������ ������ len
net_msg_t* net_get_msg_buf(uint32_t len);

//�������� ����������� ��������� ������ ������ ��� ���������� � ���������
size_t net_msg_buf_get_available_space(const net_msg_t* buf);

//������� ������
void net_free_msg_buf(net_msg_t* buf);

//������� ������ ���������
//��� ����� ������ ������� ���� ����������� ���������� �� ����� � ����������
net_err_t net_send_msg(net_msg_t* msg, net_msg_priority_t priority, uint64_t channel);

//������������� ������� ���������
net_err_t net_update(void);










