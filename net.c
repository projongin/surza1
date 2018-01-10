#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

//#include <rttarget.h>
#include <rtk32.h>
#include <socket.h>
#include <Clock.h>


#include "net.h"

#include "common.h"
#include "log.h"



/*
========================================================================================================
  ������� ������ ������:

    TCP ������ �������� ��������� � ��������� ������, ������� ����������, ��� ������� ����������� � �������������.
	��� ����� ���������� ������� ��� ������ - �������� � ������������, ������� ������ ��������������� ������ 2
	������ ������ ������� ����� ���������� (��� �������, ��� ���-�� ������� ���������� �� �������� NET_MAX_CONNECTIONS_ALLOWED) - �� ������ �� �� ����������� � ����������

	������������ ����� ������� ��������� ��� ������ �� ��������� � ���������� �� ����� � ���������.
	�������� �� ��������� ����������� � �������� - ��� ������������� �������� �������� �������.
	��� ������ �� ������ ��� ��� ����������� �������� ������� <2 (�������� ����� ����������)  - ��������� ������� ������� �� 1 � �����������.

	�������� ����� ������ ������ �� ������  � ���������.  ���������� ��������� ���������� � �������� ������ ��� ������� ����������.
	��� ������ �� ������, ��� ������ � ���������� ������ ��� ��� ����������� �������� ������� <2 (������������ ����� ����������) - ��������� ������� ������� �� 1 � �����������.
		
	������������� ������� ��������� ���������� � �������� �����.
	������������ ��������� ���� ����������:
	    ���� ���������� ����� �������, ������� ������� �������� � ��������� ��������� � ����������, ��������� �����
		�������� ��������� �� ����� ������� �� �������� � ��������� � �������������� ������� � ���������� ��� ������� ����������
		�������� �������� ��������� �� �������� ���������� � ��������� � ����� ������� ������ �������� �����������
		�������� ���������� ��� ������� ����������� ���������, � �������������� ������ ����� ��-��� ��������� � ��� �������, ���� ����� �� ��� ����������� � �����������
		    (����� �� ��� ����������� ��� �������� ��������� � �� ��� ������� �������������� ������� net_free_msg_buf)

	������� ���������� ��������� ��������� ��������� �� �������� � ����� ������� �������� �����������

	������� ��������� ������ ������ ���������� ��� ������ ������������ ���������. �������������� ���������� ��� ������ ��� ������ ������ ��������� (��� ������� ��� ������ �� ����)
	   ���������� - ����� ��������� ������������ � ����������� ��������� ��������� � ������� ����� �������� �� ��������.


========================================================================================================
*/


enum net_buf_type {
	NET_BUF_TYPE_POOL = 0,
    NET_BUF_TYPE_HEAP
};

enum net_mailbox_msg_type_t {
	NET_MAILBOX_EXIT = 0,
	NET_MAILBOX_MSG
};


#pragma pack(push)
#pragma pack(1) 

//��������� ���������,������������� �� ����
//������������ ��������������� ����� ��� �������� ������ ���������,  ������������� � crc ���, ��� ��� crc32 ���� � tcp
typedef struct {
	uint32_t size;         //������ ������ �����, ������� ���� ���������
	uint32_t label;        //��������� �����,  ���������� ������ ���������
	uint8_t  priority;     //��������� ���������, (net_msg_priority_t)
	uint8_t  msg_data[];   //������ ��������� (net_msg_t) + ����� ����� � ��������� ������� ������
} net_raw_msg_t;



//��������� ������ ��� �������� ���������
typedef struct {
	enum net_buf_type buf_type;   //��� ������ - �� ���� ��� ���������� �� ����
	uint64_t          channel;    //�����(����� �������) ��������,  NET_BROADCAST_CHANNEL  - �������� ���� ������������, ����������� �� ������ ip ������ � ����� ������������� �������
	net_raw_msg_t     net_msg;
} net_buf_t;

#pragma pack(pop)



#define NET_MSG_OFFSET         (offsetof(net_raw_msg_t, msg_data))
#define NET_RAW_MSG_OFFSET     (offsetof(net_buf_t, net_msg))

#define NET_BUF_TO_MSG_OFFSET  (NET_RAW_MSG_OFFSET + NET_RAW_MSG_OFFSET)



//�������� ���������

typedef struct {
	byte LocalIP[4];
	byte NetMask[4];
	byte DefaultGateway[4];
	byte DNSServer[4];
	byte RemoteIP[4];
	uint16_t LocalPort;
	uint16_t RemotePort;
} net_settings_t;




//������, ����������� �� ����� �������� ����, ���� ���������� �������� ��-��������� ��� ������ ������ ����� ��������
static net_settings_t net_settings;


//������ ��-���������, ��� ������������� ��������� ��������� �� �����
static const net_settings_t net_settings_default = {
	{ 192, 168,  5,  20 },     //LocalIP
	{ 255, 255, 255,  0 },     //NetMask
	{ 192, 168,  5,   1 },     //DefaultGateway
	{ 192, 168,  5,   1 },     //DNSServer
    { 192, 168,  5,  21 },     //RemoteIP
	10020,                     //Local port  (TCP server port)
	10010                      //Remote port
};



//������������ ���������� ���������  � ������� �� �������� � ��������� ��� ������� ������
#define NET_THREAD_QUEUE_LENGTH   1000

//������������ ���������� ��������� � ����� �������� �� �������� � ���������
#define NET_MAIN_QUEUE_LENGTH     (NET_THREAD_QUEUE_LENGTH*NET_MAX_CONNECTIONS_ALLOWED)





//---------------------------------------------------------------------------------------------------------------------------------------------------------------
// ������� ������ � ��������
//---------------------------------------------------------------------------------------------------------------------------------------------------------------

typedef enum {
	NET_MAIN_QUEUE_SEND = 0,
	NET_MAIN_QUEUE_RECV
}  net_main_queue_type_t;

typedef struct net_queue_node_tt {
	struct net_queue_node_tt* next;
	void *ptr;
} net_queue_node_t;

#define NET_QUEUE_PRIORITY_NUM  (NET_PRIORITY_HIGHEST+1)     //���������� �����������
#define NET_MAIN_QUEUE_NUM      (NET_MAIN_QUEUE_RECV+1)      //���������� ����� ��������


net_queue_node_t* priority_queue_head[NET_MAIN_QUEUE_NUM][NET_QUEUE_PRIORITY_NUM];
net_queue_node_t* priority_queue_tail[NET_MAIN_QUEUE_NUM][NET_QUEUE_PRIORITY_NUM];

#define NET_NODE_STACK_SIZE  NET_MAIN_QUEUE_LENGTH
net_queue_node_t  net_queue_nodes[NET_MAIN_QUEUE_NUM][NET_NODE_STACK_SIZE];
net_queue_node_t* net_queue_node_stack[NET_MAIN_QUEUE_NUM][NET_NODE_STACK_SIZE];
int net_queue_node_stack_ptr[NET_MAIN_QUEUE_NUM];


void net_main_queue_init() {
	for (int j = 0; j < NET_MAIN_QUEUE_NUM; j++) {
		for (int i = 0; i < NET_QUEUE_PRIORITY_NUM; i++) {
			priority_queue_head[j][i] = NULL;
			priority_queue_tail[j][i] = NULL;
		}
		net_queue_node_stack_ptr[j] = NET_NODE_STACK_SIZE;
		for (int i = 0; i < NET_NODE_STACK_SIZE; i++)
			net_queue_node_stack[j][i] = &net_queue_nodes[j][i];
	}
}

bool net_main_queue_is_full(net_main_queue_type_t queue) {
	return net_queue_node_stack_ptr[queue] == 0 ? true : false;
}

int net_add_to_main_queue(net_main_queue_type_t queue, net_msg_priority_t priority, void* ptr) {
	if (net_main_queue_is_full(queue))
		return -1;
	
	net_queue_node_t* node = net_queue_node_stack[queue][--net_queue_node_stack_ptr[queue]];
	node->next = NULL;
	node->ptr = ptr;

	if (priority_queue_tail[queue][priority] != NULL) {
		priority_queue_tail[queue][priority]->next = node;
		priority_queue_tail[queue][priority] = node;
	}else
		priority_queue_tail[queue][priority] = node;

	if (priority_queue_head[queue][priority] == NULL)
		priority_queue_head[queue][priority] = node;

	return 0;
}

void* net_remove_from_main_queue(net_main_queue_type_t queue, net_msg_priority_t priority) {

	net_queue_node_t* node = priority_queue_head[queue][priority];

	if (node) {
		priority_queue_head[queue][priority] = node->next;
		if (priority_queue_head[queue][priority] == NULL)
			priority_queue_tail[queue][priority] = NULL;
		
		net_queue_node_stack[queue][net_queue_node_stack_ptr[queue]] = node;
		net_queue_node_stack_ptr[queue]++;
	}

	return node?node->ptr:NULL;
}

void* net_remove_from_main_queue_by_priority(net_main_queue_type_t queue) {
	void* ptr=NULL;

	for (int pr = NET_QUEUE_PRIORITY_NUM - 1; pr >= 0; pr--) {
		ptr = net_remove_from_main_queue(queue, pr);
		if (ptr) return ptr;
	}

	return NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------------------

//������ ������� � ����� ������� ��������
RTKSemaphore send_queue_mutex;




//��������� ������ ������ ��� ��������� � �������� ������ len
static net_buf_t* net_get_buf(size_t len) { return NULL; }

//������� ������ � ��� ��� �����������
static void net_free_buf(net_buf_t* buf) {};



#define DEVICE_ID     I82559_DEVICE
#define DEVICE_MINOR  0               // first device of type DEVICE_ID (use 1 for second, 2 for third, etc)


// The following values are ignored for PCI devices (the BIOS supplies
// them), but they must be set correctly for ISA systems

#define ED_IO_ADD     0//0x300              // I/O address of the device
#define ED_IRQ        (-1)//5               // IRQ         of the device
#define ED_MEM_ADD    0                  // Memory Window (only some devices)


static int interface = SOCKET_ERROR;



static SOCKET server_sock;

static net_msg_dispatcher callback = 0;



volatile static int callback_en = 0;



typedef struct {

	SOCKET         sock;                    //�����
	volatile int   thread_cnt_atomic;       //������� ��������� ���������� �������

	RTKMailbox     read_mailbox;            //�������� ��������� ������
	RTKMailbox     write_mailbox;           //�������� ������������� ������

	int            read_missed_counter;     //������� ����������� ��������� �� ����� ��-�� ������� �������
	int            write_missed_counter;    //������� ����������� ��������� �� �������� ��-�� ������� �������
	

	RTKTaskHandle  writer_handle;           //������ ������� �� ������ ������
	RTKTaskHandle  reader_handle;

	uint64_t       channel;                 //���������� ����� ������, �������������� �� ������ ip ������ � ����� ������������� �������

	uint32_t       label;                   //������� ����� ������ ��������� (�������� �� �����, ���� �� ������������ � ������ ������ ������������������ rand())

} net_thread_state_t;

static net_thread_state_t net_thread_state[NET_MAX_CONNECTIONS_ALLOWED];

static char net_thread_mailbox_name[] = "net_thread_mailbox";
static char net_server_thread_name[] = "net_server_thread";
static char net_reader_thread_name[] = "net_reader_thread";
static char net_writer_thread_name[] = "net_writer_thread";

static RTKTaskHandle net_tcp_server_handler = NULL;





static void RTKAPI net_tcp_server_func(void* param);




void InterfaceCleanup(void)
{
	if (interface != SOCKET_ERROR)
	{
		const int One = 1;
		xn_interface_opt(interface, IO_HARD_CLOSE, (const char *)&One, sizeof(int));
		xn_interface_close(interface);
		interface = SOCKET_ERROR;
	}
}




//������������� ����������� � �������� tcp �������
net_err_t net_init(net_msg_dispatcher dispatcher) {
	int res;

	
	//��������� ������� �������� 
	res = false;
	{
		//��� �������� ���������� ������� �������� �� �����
	}
	if (!res) {
		LOG_AND_SCREEN("net_init(): Default settings apply!");
		memcpy(&net_settings, &net_settings_default, sizeof(net_settings));
	}



	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//��� ���������
	//
	CFG_KA_INTERVAL = 20;
	CFG_KA_RETRY = 4;
	CFG_KA_TMO = 10;
	CFG_TIMER_FREQ = 50;
	SIZESTACK_HUGE = 1024;
	//--------------------------------------

	res = xn_rtip_init();
	if (res == SOCKET_ERROR) {
		LOG_AND_SCREEN("net_init(): xn_rtip_init failed");
		goto err;
	}

	atexit(InterfaceCleanup);         // make sure the driver is shut down properly

	res = xn_bind_i82559(DEVICE_MINOR);     // tell RTIP-32 what Ethernet driver we want (see Netcfg.h)
	if (res == SOCKET_ERROR) {
		LOG_AND_SCREEN("net_init(): driver initialization failed");
		goto err;
	}

	// Open the interface
	interface = xn_interface_open_config(DEVICE_ID, DEVICE_MINOR, ED_IO_ADD, ED_IRQ, ED_MEM_ADD);
	if (interface == SOCKET_ERROR) {
		int err = xn_getlasterror();
		LOG_AND_SCREEN("net_init() : Interface config failed : err_num = %d, <%s>", err, xn_geterror_string(err));
		goto err;
	}
	else {
		struct _iface_info ii;
		xn_interface_info(interface, &ii);
		LOG_AND_SCREEN("MAC: %02x-%02x-%02x-%02x-%02x-%02x",
			ii.my_ethernet_address[0], ii.my_ethernet_address[1], ii.my_ethernet_address[2],
			ii.my_ethernet_address[3], ii.my_ethernet_address[4], ii.my_ethernet_address[5]);
	}

	// Set the IP address and interface
	LOG_AND_SCREEN("Static IP address %i.%i.%i.%i", net_settings.LocalIP[0], net_settings.LocalIP[1], net_settings.LocalIP[2], net_settings.LocalIP[3]);
	res = xn_set_ip(interface, net_settings.LocalIP, net_settings.NetMask);

	// define default gateway and DNS server
	xn_rt_add(RT_DEFAULT, ip_ffaddr, net_settings.DefaultGateway, 1, interface, RT_INF);
	xn_set_server_list((dword*)net_settings.DNSServer, 1);



	//������������� ��������, �������� � �.�.
	callback_en = 0;

	for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++) {
		net_thread_state_t  *conn_data = &net_thread_state[i];

		conn_data->sock = INVALID_SOCKET;
		conn_data->thread_cnt_atomic=0;
		
		conn_data->read_mailbox = RTKCreateMailbox(sizeof(net_buf_t*), NET_THREAD_QUEUE_LENGTH, net_thread_mailbox_name);
		conn_data->write_mailbox = RTKCreateMailbox(sizeof(net_buf_t*), NET_THREAD_QUEUE_LENGTH, net_thread_mailbox_name);

		conn_data->read_missed_counter = 0;
		conn_data->write_missed_counter=0;

		conn_data->reader_handle = NULL;
		conn_data->writer_handle = NULL;

		conn_data->label = rand();

	}
	 



	//�������� ����� �������� ��� ������� � �������� ���������, � ����� ���������� �� ��������� ������������ �����������
	net_main_queue_init();

	send_queue_mutex = RTKOpenSemaphore(ST_MUTEX, 1, SF_COPY_NAME, "net_send_queue_mutex");




	if (false) {
		LOG_AND_SCREEN("net_init(): memory allocation failed");
		return NET_ERR_MEM_ALLOC;
	}




	
    //�������� ������ tcp �������
	net_tcp_server_handler  = RTKRTLCreateThread(net_tcp_server_func, NET_TCP_SERVER_PRIORITY,  100000, TF_NO_MATH_CONTEXT, NULL, net_server_thread_name);



	//-------------------------------------


	return NET_ERR_NO_ERROR;

err:
	return NET_ERR_ANY;

}


//���������� ��������� ���������
void net_start() {
	atom_set_state(&callback_en, 1);
}
//������ ��������� ���������
void net_stop() {
	atom_set_state(&callback_en, 0);
}

//���������� ���������� ������������ ��������, <0 ���� ��� ����������
volatile unsigned net_connections() {
	unsigned c = 0;

	for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
		if (net_thread_state[i].sock != INVALID_SOCKET)
			c++;
	
	return c;
}


//--------------------------------------------------------------------------

//��������� ������ ������
net_buf_t* net_get_net_buf(size_t len) { return NULL; }

//������� ������
void net_free_net_buf(net_buf_t* buf) {}


//��������� ������ ������ ��� ��������� � ������ ������ len
net_msg_t* net_get_msg_buf(size_t len) { return NULL; }

//������� ������
void net_free_msg_buf(net_msg_t* buf) {}

//�������� ����������� ��������� ������ ������ ��� ���������� � ���������
size_t net_msg_buf_get_available_space(const net_msg_t* buf) { return 0; };

//---------------------------------------------------------------------------

//������� ������ ���������
net_err_t net_send_msg(net_msg_t* msg, net_msg_priority_t priority, unsigned channel) {
	

	//������ ������� �������

	//���������� � ����������� � �����������

	//���� ������ = �����

	//����� ������\����� = rand();

	//������������ ������� �������

	return NET_ERR_NO_ERROR;
}





void net_reader_thread_func(void* params) {

	net_thread_state_t* data = (net_thread_state_t*)params;

	atom_dec(&data->thread_cnt_atomic);


	//���-�� ������ ��� �������� = ������ ���������

	//���������� = 0

	//������� = �������������

//����:

	//�������� ������ �� ������ � ���������

	//���� �a�����, �� ��������� ���������. ��� ������������� ��������� �����, ������� �� �������

	//���� ������ ������, �� ��������� �����, �������� ������ � ����������, ������� �� �������

	//������ ���������:

	  //0  ���� ������� 


}

void net_writer_thread_func(void* params) {

	net_thread_state_t* data = (net_thread_state_t*)params;

	atom_dec(&data->thread_cnt_atomic);

}



enum net_tcp_server_state_t {
	NET_TCP_SERVER_INIT=0,
	NET_TCP_SERVER_ACCEPT,
	NET_TCP_SERVER_CLOSE
};

static void RTKAPI net_tcp_server_func(void* param){

	enum net_tcp_server_state_t state = NET_TCP_SERVER_INIT;
    
	SOCKET serv_socket = INVALID_SOCKET;
	struct sockaddr_in sin;
	struct sockaddr_in remote;
	int remote_len = sizeof(remote);
	int ret;
	net_thread_state_t* thread_data=NULL;
	int sock_opt;
	uint64_t u64;


	while (true) {

		switch (state) {

		case NET_TCP_SERVER_INIT:

			RTKDelay(CLKMilliSecsToTicks(200));
			serv_socket = socket(AF_INET, SOCK_STREAM, 0);
			if (serv_socket == INVALID_SOCKET)
				break;


			//����� ������
			ret = -1;
			while (true) {

				if (sock_opt = 1, setsockopt(serv_socket, SOL_SOCKET, SO_KEEPALIVE, (PFCCHAR)&sock_opt, sizeof(sock_opt))) break;
				if (sock_opt = 1, setsockopt(serv_socket, SOL_SOCKET, SO_REUSESOCK, (PFCCHAR)&sock_opt, sizeof(sock_opt))) break;
				if (sock_opt = 1, setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, (PFCCHAR)&sock_opt, sizeof(sock_opt))) break;

				ret = 0;
				break;
			}
			if (ret < 0) {
				state = NET_TCP_SERVER_CLOSE;
				break;
			}


			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = htonl(INADDR_ANY);
			sin.sin_port = htons(net_settings.LocalPort);
			ret = bind(serv_socket, (const struct sockaddr*)&sin, sizeof(sin));
			if (ret == SOCKET_ERROR) {
				state = NET_TCP_SERVER_CLOSE;
				break;
			}

			ret = listen(serv_socket, (unsigned)NET_MAX_CONNECTIONS_ALLOWED - net_connections());
			if (ret == SOCKET_ERROR) {
				state = NET_TCP_SERVER_CLOSE;
				break;
			}

			state = NET_TCP_SERVER_ACCEPT;
			break;


		case NET_TCP_SERVER_ACCEPT:

			//���� ��������� �����
			thread_data = NULL;
			if (net_connections() < NET_MAX_CONNECTIONS_ALLOWED)
				for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
					if (net_thread_state[i].sock == INVALID_SOCKET) {
						thread_data = &net_thread_state[i];
						break;
					}

			if (thread_data == NULL) {
				RTKDelay(CLKMilliSecsToTicks(1000));
				break;
			}
			
			thread_data->sock = accept(serv_socket, (struct sockaddr*)&remote, &remote_len);
			if (thread_data->sock == INVALID_SOCKET) {
				/*
				int err = xn_getlasterror();
				LOG_AND_SCREEN("accept error #%d!  %s", err, xn_geterror_string(err));
				*/
				state = NET_TCP_SERVER_CLOSE;
				break;
			}

			//����� ������ 
			while (true) {
				ret = -1;

				if (sock_opt = 1, setsockopt(thread_data->sock, SOL_SOCKET, SO_KEEPALIVE, (PFCCHAR)&sock_opt, sizeof(sock_opt))) break;
				//��������� ��������� ���� �� �������������

				ret = 0;
				break;
			}
			if (ret < 0) {
				state = NET_TCP_SERVER_CLOSE;
				break;
			}

			// ������ ����� ������� ������ ������

			u64 = remote.sin_addr.s_un.S_addr;
			u64 <<= 16;
			u64 += remote.sin_port;
			thread_data->channel = u64;

			thread_data->read_missed_counter = 0;
			thread_data->write_missed_counter = 0;

			atom_set_state(&thread_data->thread_cnt_atomic, 2);

			thread_data->writer_handle = RTKRTLCreateThread(net_writer_thread_func, NET_TCP_WRITER_PRIORITY, 200000, TF_NO_MATH_CONTEXT, thread_data, net_writer_thread_name);
			thread_data->reader_handle = RTKRTLCreateThread(net_reader_thread_func, NET_TCP_READER_PRIORITY, 200000, TF_NO_MATH_CONTEXT, thread_data, net_reader_thread_name);

			break;


		case NET_TCP_SERVER_CLOSE:

			closesocket(serv_socket);
			serv_socket = INVALID_SOCKET;
			thread_data = NULL;
			state = NET_TCP_SERVER_INIT;
			break;

		}

	}

}



bool net_add_to_queue_in(net_msg_t* msg) {


	//������� �����, ���� ��� ����������� ��������


	return false;
}



void net_connection_control_func() {

   //�������� ��������� ��������\������������ �������

	if (net_connections() < NET_MAX_CONNECTIONS_ALLOWED)
		for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
			if (net_thread_state[i].sock != INVALID_SOCKET) {
				//�������� �� ������������� ���������� ��� ����� ���������� �������
				net_thread_state_t *data = &net_thread_state[i];
				if (atom_get_state(&data->thread_cnt_atomic) == 0) {
					// ��� ������� ������ ���� �������� � ������� �������. �� �� ���������
					// ������ ������� ������ ������
					RTKBool res;
					net_buf_t* net_buf;
					do {
						net_buf = NULL;
						res = RTKGetCond(data->read_mailbox, &net_buf);
						if (res && net_buf) {
							// �������� ��������� � ����� ������� ������
							if (net_add_to_main_queue(NET_MAIN_QUEUE_RECV, (net_buf->net_msg.priority < NET_QUEUE_PRIORITY_NUM ? net_buf->net_msg.priority : NET_PRIORITY_BACKGROUND), net_buf) < 0)
								net_free_net_buf(net_buf);
						}
					} while (res == TRUE);

					// ������ ������� �� ��������
					do {
						net_buf = NULL;
						res = RTKGetCond(data->write_mailbox, &net_buf);
						if (res && net_buf) {
							net_free_net_buf(net_buf);
						}
					} while (res == TRUE);

					// �������� �����
					closesocket(data->sock);
					data->sock = INVALID_SOCKET;

				}

				break;
			}

}

void net_read_recv_queues() {

	for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
		if (net_thread_state[i].sock != INVALID_SOCKET) {
			net_thread_state_t *data = &net_thread_state[i];

			RTKBool res;
			net_buf_t* net_buf;
			do {
				if (net_main_queue_is_full(NET_MAIN_QUEUE_RECV)) break;
				net_buf = NULL;
				res = RTKGetCond(data->read_mailbox, &net_buf);
				if (res && net_buf) {
					// �������� ��������� � ����� ������� ������
					if (net_add_to_main_queue(NET_MAIN_QUEUE_RECV, (net_buf->net_msg.priority < NET_QUEUE_PRIORITY_NUM ? net_buf->net_msg.priority : NET_PRIORITY_BACKGROUND), net_buf) < 0) {
						net_free_net_buf(net_buf);
						data->read_missed_counter++;
					}		
				}
			} while (res == TRUE);

		}

}

void net_write_send_queues() {

	net_buf_t* net_buf = NULL;

	do {

		RTKWait(send_queue_mutex);

		  net_buf = net_remove_from_main_queue_by_priority(NET_MAIN_QUEUE_SEND);

		  if (net_buf) {
			  ....
		  }

		RTKSignal(send_queue_mutex);

	} while (net_buf);
	

}

void net_dispatch_msgs() {
	//��������� ��������� �� ���������, ������������ � ����������, ���� ����������� ��� �������� �� ����������� ������� ������� net_free_msg_buf,
	// � ����� ��� ���������� ����� ������ �� ����������� � �������  net_send_msg
	//������ ����� ����� ���������� �����������, ���� � ��� ����� �� ��� ������� � net_send_msg
}


//������������� ������� ���������
net_err_t net_update() {

	//��������� ��������� ���� �������
	net_connection_control_func();

	//��������� ��� ������� ��������� �� ������� �������� � �������� � ����� ������� ������ �������� �����������
	net_read_recv_queues();
	
	//��������� ����� ������� �� �������� � ����������� � �������������� ������� �� �������� ��� ������� ������ �������� �����������
	net_write_send_queues();

	//������� ����������� ��� ���� ��������� � ����� ������� ������
	net_dispatch_msgs();


	return NET_ERR_NO_ERROR;
}


