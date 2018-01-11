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
  Принцип работы модуля:

    TCP сервер работает постоянно в отдельном потоке, ожидает соединения, при ошибках закрывается и пересоздается.
	При новом соединении создает два потока - читающий и записывающий, счетчик потока устанавливается равным 2
	Сервер только создает новые соединения (при условии, что кол-во текущих соединений не достигло NET_MAX_CONNECTIONS_ALLOWED) - не следит за их завершением и состоянием

	Записывающий поток ожидает сообщения для записи на мейлбоксе и отправляет на сокет с таймаутом.
	Ожидание на мейлбоксе реализовано с таймером - для периодической проверки счетчика потоков.
	При ошибке на сокете или при обнаружении счетчика потоков <2 (читающий поток завершился)  - уменьшает счетчик потоков на 1 и завершается.

	Читающий поток читает данные из сокета  с таймаутом.  Полученные сообщения складывает в мейлбокс приема для данного соединения.
	При ошибке на сокете, при ошибке в полученных данных или при обнаружении счетчика потоков <2 (записывающий поток завершился) - уменьшает счетчик потоков на 1 и завершается.
		
	Периодическая функция обработки вызывается в основном цикле.
	Контролирует состояние всех соединений:
	    ждет завершения обоих потоков, очищает очередь входящих и исходящих сообщений в мейлбоксах, закрывает сокет
		забирает сообщения из общей очереди на отправку и рассылает в индивидуальные очереди в мейлбоксах для каждого соединения
		забирает принятые сообщения из очередей соединений и добавляет в общую очередь приема согласно приоритетам
		вызывает обработчик для каждого полученного сообщения, и самостоятельно отдает буфер из-под сообщения в пул буферов, если буфер не был использован в обработчике
		    (буфер не был использован для отправки сообщения и не был передан самостоятельно функции net_free_msg_buf)

	Функция добавления сообщений добавляет сообщения на отправку в общую очередь согласно приоритетам

	Функция получения буфера должна вызываться под каждое отправляемое сообщение. Самостоятельно определяет тип буфера под разный размер сообщений (пул буферов или память из кучи)
	   Исключение - когда сообщение отправляется в обработчике принятого сообщения и текущий буфер подходит по размерам.


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

//структура сообщения,передаваемого по сети
//используется псевдослучайная метка для контроля границ сообщения,  необходимости в crc нет, так как crc32 есть в tcp
typedef struct {
	uint32_t size;         //размер данных общий, включая этот заголовок
	uint32_t label;        //случайная метка,  отмечающая начало сообщения
	uint8_t  priority;     //приоритет сообщения, (net_msg_priority_t)
	uint8_t  msg_data[];   //данные сообщения (net_msg_t) + копия метки в последних четырех байтах
} net_raw_msg_t;



//структура буфера для хранения сообщений
typedef struct {
	enum net_buf_type buf_type;   //тип буфера - из пула или выделенный из кучи
	uint64_t          channel;    //канал(номер клиента) отправки,  NET_BROADCAST_CHANNEL  - отправка всем подключенным, формируется на основе ip адреса и порта подключенного клиента
	net_raw_msg_t     net_msg;
} net_buf_t;

#pragma pack(pop)



#define NET_MSG_OFFSET         (offsetof(net_raw_msg_t, msg_data))
#define NET_RAW_MSG_OFFSET     (offsetof(net_buf_t, net_msg))

#define NET_BUF_TO_MSG_OFFSET  (NET_RAW_MSG_OFFSET + NET_RAW_MSG_OFFSET)



//стетевые настройки

typedef struct {
	byte LocalIP[4];
	byte NetMask[4];
	byte DefaultGateway[4];
	byte DNSServer[4];
	byte RemoteIP[4];
	uint16_t LocalPort;
	uint16_t RemotePort;
} net_settings_t;




//адреса, загружаемые из файла настроек сети, либо заменяемые адресами по-умолчанию при ошибки чтения файла настроек
static net_settings_t net_settings;


//адреса по-умолчанию, при невозможности загрузить настройки из файла
static const net_settings_t net_settings_default = {
	{ 192, 168,  5,  20 },     //LocalIP
	{ 255, 255, 255,  0 },     //NetMask
	{ 192, 168,  5,   1 },     //DefaultGateway
	{ 192, 168,  5,   1 },     //DNSServer
    { 192, 168,  5,  21 },     //RemoteIP
	10020,                     //Local port  (TCP server port)
	10010                      //Remote port
};


//загрузка сетевых настроек из файла
bool net_load_settings() {

	//тут доделать считывание сетевых настроек из файла

	return false;
}






//максимальное количество сообщений  в очереди на отправку и получение для каждого потока
#define NET_THREAD_QUEUE_LENGTH   1000

//максимальное количество сообщений в общих очередях на отправку и получение
#define NET_MAIN_QUEUE_LENGTH     (NET_THREAD_QUEUE_LENGTH*NET_MAX_CONNECTIONS_ALLOWED)





//---------------------------------------------------------------------------------------------------------------------------------------------------------------
// очереди приема и отправки
//---------------------------------------------------------------------------------------------------------------------------------------------------------------

typedef enum {
	NET_MAIN_QUEUE_SEND = 0,
	NET_MAIN_QUEUE_RECV
}  net_main_queue_type_t;

typedef struct net_queue_node_tt {
	uint64_t type;
	struct net_queue_node_tt* next;
	void *ptr;
} net_queue_node_t;

#define NET_QUEUE_PRIORITY_NUM  (NET_PRIORITY_HIGHEST+1)     //количество приоритетов
#define NET_MAIN_QUEUE_NUM      (NET_MAIN_QUEUE_RECV+1)      //количество общих очередей


static net_queue_node_t* priority_queue_head[NET_MAIN_QUEUE_NUM][NET_QUEUE_PRIORITY_NUM];
static net_queue_node_t* priority_queue_tail[NET_MAIN_QUEUE_NUM][NET_QUEUE_PRIORITY_NUM];

#define NET_NODE_STACK_SIZE  NET_MAIN_QUEUE_LENGTH
static net_queue_node_t  net_queue_nodes[NET_MAIN_QUEUE_NUM][NET_NODE_STACK_SIZE];
static net_queue_node_t* net_queue_node_stack[NET_MAIN_QUEUE_NUM][NET_NODE_STACK_SIZE];
static int net_queue_node_stack_ptr[NET_MAIN_QUEUE_NUM];

//пропускать сообщения при выборке из очереди 
static uint64_t net_skip_type[NET_MAIN_QUEUE_NUM][NET_MAX_CONNECTIONS_ALLOWED];
static int net_skip_type_num[NET_MAIN_QUEUE_NUM];


void net_main_queue_init() {
	for (int j = 0; j < NET_MAIN_QUEUE_NUM; j++) {
		for (int i = 0; i < NET_QUEUE_PRIORITY_NUM; i++) {
			priority_queue_head[j][i] = NULL;
			priority_queue_tail[j][i] = NULL;
		}
		net_queue_node_stack_ptr[j] = NET_NODE_STACK_SIZE;
		for (int i = 0; i < NET_NODE_STACK_SIZE; i++)
			net_queue_node_stack[j][i] = &net_queue_nodes[j][i];

		net_skip_type_num[j] = 0;
	}
}

bool net_main_queue_is_full(net_main_queue_type_t queue) {
	return net_queue_node_stack_ptr[queue] == 0 ? true : false;
}

int net_add_to_main_queue(net_main_queue_type_t queue, net_msg_priority_t priority, uint64_t type, bool front, void* ptr) {
	if (net_main_queue_is_full(queue))
		return -1;
	
	net_queue_node_t* node = net_queue_node_stack[queue][--net_queue_node_stack_ptr[queue]];
	node->next = NULL;
	node->ptr = ptr;
	node->type = type;

	if (front) {

		//добавляем в начало
		node->next = priority_queue_head[queue][priority];
		priority_queue_head[queue][priority] = node;
		if (priority_queue_tail[queue][priority] == NULL)
			priority_queue_tail[queue][priority] = priority_queue_head[queue][priority];

	} else {

		//добавляем в конец
		if (priority_queue_tail[queue][priority] != NULL) {
			priority_queue_tail[queue][priority]->next = node;
			priority_queue_tail[queue][priority] = node;
		}
		else
			priority_queue_tail[queue][priority] = node;

		if (priority_queue_head[queue][priority] == NULL)
			priority_queue_head[queue][priority] = node;

	}

	return 0;
}

void* net_remove_from_main_queue(net_main_queue_type_t queue, net_msg_priority_t priority) {

	net_queue_node_t* node = priority_queue_head[queue][priority];
	net_queue_node_t* prev = NULL;
	
	while (node) {
		if (net_skip_type_num[queue] > 0) {
			//надо пропускать некоторые типы сообщений
			for (int i = 0; i < net_skip_type_num[queue]; i++) {
				if (node->type == net_skip_type[queue][i]) {
					prev = node;
					node = node->next;
					break;
				}
			}
		}
	}
	
	if (node) {
		if (prev == NULL) {  //забираем самый первый
			priority_queue_head[queue][priority] = node->next;
			if (priority_queue_head[queue][priority] == NULL)
				priority_queue_tail[queue][priority] = NULL;
		} else { //забираем не первый (были пропуски по типу)
			prev->next = node->next;
			if (priority_queue_tail[queue][priority] == node)
				priority_queue_tail[queue][priority] = prev;
		}
		
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

void net_main_queue_skip_type(net_main_queue_type_t queue, uint64_t type) {
	if (net_skip_type_num[queue] < NET_MAX_CONNECTIONS_ALLOWED) {
		net_skip_type[queue][net_skip_type_num[queue]] = type;
		net_skip_type_num[queue]++;
	}
}

void net_main_queue_skip_reset(net_main_queue_type_t queue) {
	net_skip_type_num[queue] = 0;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------------------




//мютекс доступа к общей очереди отправки
RTKSemaphore send_queue_mutex;



//получение нового буфера под сообщение с размером данных len
static net_buf_t* net_get_buf(size_t len) { return NULL; }

//возврат буфера в пул или деаллокация
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

static net_msg_dispatcher default_callback = 0;



volatile static int callback_en = 0;

static net_msg_t* save_callback_buf = NULL;

static net_msg_dispatcher net_callbacks[UINT8_MAX + 1];



typedef struct {

	SOCKET         sock;                    //сокет
	volatile int   thread_cnt_atomic;       //счетчик нормально работающих потоков

	RTKMailbox     read_mailbox;            //мейлбокс читающего потока
	RTKMailbox     write_mailbox;           //мейлбокс записывающего потока

	RTKTaskHandle  writer_handle;           //хэндлы потоков на всякий случай
	RTKTaskHandle  reader_handle;

	uint64_t       channel;                 //уникальный номер канала, сформированный на основе ip адреса и порта подключенного клиента + инкремент после каждого подключения

	uint32_t       label;                   //текущая метка границ сообщения (задается на канал, чтоб не использовать в каждом потоке потоконебезопасную rand())

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



//инициализация контроллера и создание tcp сервера
net_err_t net_init(net_msg_dispatcher dispatcher) {
	int res;

	
	//получение сетевых настроек 
	if (!net_load_settings()) {
		LOG_AND_SCREEN("net_init(): Default settings apply!");
		memcpy(&net_settings, &net_settings_default, sizeof(net_settings));
	}



	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//доп настройки
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


	callback_en = 0;

	default_callback = dispatcher;
	for (int i = 0; i < (sizeof(net_callbacks) / sizeof(net_callbacks[0])); i++)
		net_callbacks[i] = NULL;
	


	//создание пула буферов
	


	//инициализация мютексов, очередей и т.п.
	
	for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++) {
		net_thread_state_t  *conn_data = &net_thread_state[i];

		conn_data->sock = INVALID_SOCKET;
		conn_data->thread_cnt_atomic=0;
		
		conn_data->read_mailbox = RTKCreateMailbox(sizeof(net_buf_t*), NET_THREAD_QUEUE_LENGTH, net_thread_mailbox_name);
		conn_data->write_mailbox = RTKCreateMailbox(sizeof(net_buf_t*), NET_THREAD_QUEUE_LENGTH, net_thread_mailbox_name);

		conn_data->reader_handle = NULL;
		conn_data->writer_handle = NULL;

		conn_data->label = rand();

		conn_data->channel = conn_data->label;

	}
	 



	//создание общих очередей для входных и выходных сообщений, а также указателей на сообщения определенных приоритетов
	net_main_queue_init();

	send_queue_mutex = RTKOpenSemaphore(ST_MUTEX, 1, SF_COPY_NAME, "net_send_queue_mutex");




	if (false) {
		LOG_AND_SCREEN("net_init(): memory allocation failed");
		return NET_ERR_MEM_ALLOC;
	}




	
    //создание потока tcp сервера
	net_tcp_server_handler  = RTKRTLCreateThread(net_tcp_server_func, NET_TCP_SERVER_PRIORITY,  100000, TF_NO_MATH_CONTEXT, NULL, net_server_thread_name);



	//-------------------------------------


	return NET_ERR_NO_ERROR;

err:
	return NET_ERR_ANY;

}


//разрешение получения сообщений
void net_start() {
	atom_set_state(&callback_en, 1);
}
//запрет получения сообщений
void net_stop() {
	atom_set_state(&callback_en, 0);
}


//добавление обработчика сообщений определенного типа
void net_add_dispatcher(uint8_t type, net_msg_dispatcher dispatcher) {
	net_callbacks[type] = dispatcher;
}


//возвращает количество подключенных клиентов, 0 если нет соединений
volatile unsigned net_connections() {
	unsigned c = 0;

	for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
		if (net_thread_state[i].sock != INVALID_SOCKET)
			c++;
	
	return c;
}


//--------------------------------------------------------------------------
// работа с буферами
//--------------------------------------------------------------------------

//получение нового буфера
net_buf_t* net_get_net_buf(size_t len) { return NULL; }

//получение нового буфера и инициализация копией buf,  возвращает  NULL при неудачи выделения нового буфера
net_buf_t* net_copy_net_buf(const net_buf_t* buf) {
	return NULL;
}

//возврат буфера
void net_free_net_buf(net_buf_t* buf) {}

//получение net_buf_t* из net_msg_t*
net_buf_t* net_get_net_buf_from_msg(const net_msg_t* ptr) {
	return NULL;
}

//получение нового буфера под сообщение с длиной данных len
net_msg_t* net_get_msg_buf(size_t len) { return NULL; }

//возврат буфера
void net_free_msg_buf(net_msg_t* buf) {
	
	if (save_callback_buf == buf)
		save_callback_buf = NULL;



	//..............

}

//получить максимально возможный размер данных для сохранения в сообщении
size_t net_msg_buf_get_available_space(const net_msg_t* buf) { return 0; };


//---------------------------------------------------------------------------







//посылка нового сообщения
net_err_t net_send_msg(net_msg_t* msg, net_msg_priority_t priority, unsigned channel) {

	if (save_callback_buf == msg)
		save_callback_buf = NULL;

	net_buf_t* net_buf = net_get_net_buf_from_msg(msg);

	net_buf->channel = channel;
	net_buf->net_msg.priority = priority < NET_QUEUE_PRIORITY_NUM ? priority : NET_PRIORITY_BACKGROUND;

	if (net_connections() == 0) {
		//нет активных соединений,
		net_free_net_buf(net_buf);
		return NET_ERR_NO_CONNECTIONS;
	}
	

	net_err_t ret = NET_ERR_NO_ERROR;

	//добавление в соответствии с приоритетом

	if (net_buf->channel!=NET_BROADCAST_CHANNEL) {  //обычное сообщение

	    //захват мютекса очереди
		RTKWait(send_queue_mutex);
		if (net_add_to_main_queue(NET_MAIN_QUEUE_SEND, net_buf->net_msg.priority, net_buf->channel, false, net_buf) < 0) {
			//очередь забита, удаляем буфер, теряем сообщение
			net_free_net_buf(net_buf);
			ret = NET_ERR_QUEUE_OVERFLOW;
		}
		//освобождение мютекса очереди
		RTKSignal(send_queue_mutex);

		return ret;

	} 
	
	
	 //широковещательное.   копируем и отправляем на все доступные каналы
		
	 net_buf_t* send_stack[NET_MAX_CONNECTIONS_ALLOWED];
	 int send_stack_ptr = 0;
	 

	 for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
		 if (net_thread_state[i].sock != INVALID_SOCKET) {
			 net_buf->channel = net_thread_state[i].channel;
			 send_stack[send_stack_ptr++] = net_buf;
			 net_buf = net_copy_net_buf(net_buf);
			 if (net_buf == NULL) {
				 ret = NET_ERR_MEM_ALLOC;
				 break;
			 }
		 }

	 if (send_stack_ptr == 0) {
		 //нет активных соединений,
		 net_free_net_buf(net_buf);
		 return NET_ERR_NO_CONNECTIONS;
	 }

	 if (ret != NET_ERR_NO_ERROR) {
		 // удалить буферы
		 for (int i = 0; i < send_stack_ptr; i++)
			 net_free_net_buf(send_stack[i]);
		            
		return ret;
	 }

	 //отправка на все найденные каналы
	 for (int i = 0; i < send_stack_ptr; i++) {

		 net_buf = send_stack[i];

		 //захват мютекса очереди
		 RTKWait(send_queue_mutex);
		 if (net_add_to_main_queue(NET_MAIN_QUEUE_SEND, net_buf->net_msg.priority, net_buf->channel, false, net_buf) < 0)
			 ret = NET_ERR_QUEUE_OVERFLOW;
		 //освобождение мютекса очереди
		 RTKSignal(send_queue_mutex);

		 if (ret != NET_ERR_NO_ERROR) break;

		 send_stack[i] = NULL;

	 }
		 
	 if (ret != NET_ERR_NO_ERROR) {
		 // удалить буферы
		 for (int i = 0; i < send_stack_ptr; i++)
			 if(send_stack[i]!=NULL)
				 net_free_net_buf(send_stack[i]);
	 }
	 

	 return ret;
	 
}





void net_reader_thread_func(void* params) {

	net_thread_state_t* data = (net_thread_state_t*)params;

	atom_dec(&data->thread_cnt_atomic);


	//кол-во данных для ожидания = размер заголовка

	//соостояние = 0

	//таймаут = периодический

//цикл:

	//ожидание данных на сокете с таймаутом

	//если тaймаут, то проверить параметры. при необходимости закрываем сокет, выходим из функции

	//если ошибка сокета, то закрываем сокет, отмечаем ошибку в параметрах, выходим из функции

	//разбор состояний:

	  //0  если таймаут 


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
	uint16_t u16;


	while (true) {

		switch (state) {

		case NET_TCP_SERVER_INIT:

			RTKDelay(CLKMilliSecsToTicks(200));
			serv_socket = socket(AF_INET, SOCK_STREAM, 0);
			if (serv_socket == INVALID_SOCKET)
				break;


			//опции сокета
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

			//ищем свободный канал
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

			//опции сокета 
			while (true) {
				ret = -1;

				if (sock_opt = 1, setsockopt(thread_data->sock, SOL_SOCKET, SO_KEEPALIVE, (PFCCHAR)&sock_opt, sizeof(sock_opt))) break;
				//остальные добавлять сюда по необходимости

				ret = 0;
				break;
			}
			if (ret < 0) {
				state = NET_TCP_SERVER_CLOSE;
				break;
			}

			// запуск новых потоков чтения записи

			//формируем номер канала на основе ip адреса клиента, его порта + инкремент предыдущего подключения (чтоб при повторном быстром подключении того же клиента  сообщения из входной очереди, предназначенные для  старого подключения не попали в новое из-за совпадения ip и порта)
			u64 = remote.sin_addr.s_un.S_addr;
			u64 <<= 16;
			u64 += remote.sin_port;
			u64 <<= 16;
			u16 = ((thread_data->channel+1) & 0xffff);
			u64 += u16;
			thread_data->channel = u64;

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




void net_connection_control_func() {

   //проверка состояния читающих\записывающих потоков

	if (net_connections() < NET_MAX_CONNECTIONS_ALLOWED)
		for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
			if (net_thread_state[i].sock != INVALID_SOCKET) {
				//проверка на необходимость подчистить все после завершения потоков
				net_thread_state_t *data = &net_thread_state[i];
				if (atom_get_state(&data->thread_cnt_atomic) == 0) {
					// все мютексы должны быть свободны к данному моменту. их не использую
					// очищаю очередь приема канала
					RTKBool res;
					net_buf_t* net_buf;
					do {
						net_buf = NULL;
						res = RTKGetCond(data->read_mailbox, &net_buf);
						if (res && net_buf) {
							// добавить сообщение в общую очередь приема
							net_buf->net_msg.priority = (net_buf->net_msg.priority < NET_QUEUE_PRIORITY_NUM ? net_buf->net_msg.priority : NET_PRIORITY_BACKGROUND);
							if (net_add_to_main_queue(NET_MAIN_QUEUE_RECV, net_buf->net_msg.priority, net_buf->channel, false, net_buf) < 0)
								net_free_net_buf(net_buf);
						}
					} while (res == TRUE);

					// очищаю очередь на передачу
					do {
						net_buf = NULL;
						res = RTKGetCond(data->write_mailbox, &net_buf);
						if (res && net_buf) {
							net_free_net_buf(net_buf);
						}
					} while (res == TRUE);

					// закрываю сокет
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
					// добавить сообщение в общую очередь приема
					net_buf->net_msg.priority = (net_buf->net_msg.priority < NET_QUEUE_PRIORITY_NUM ? net_buf->net_msg.priority : NET_PRIORITY_BACKGROUND);
					if (net_add_to_main_queue(NET_MAIN_QUEUE_RECV, net_buf->net_msg.priority, net_buf->channel, false, net_buf) < 0) {
						net_free_net_buf(net_buf);
					}		
				}
			} while (res == TRUE);

		}

}

void net_write_send_queues() {

	net_buf_t* net_buf = NULL;
	int ch;
	RTKBool res;

	do {

		RTKWait(send_queue_mutex);

		  net_buf = net_remove_from_main_queue_by_priority(NET_MAIN_QUEUE_SEND);

		  if (net_buf) {
			  //поиск соответствующего канала
			  ch = -1;
			  for (int i = 0; i < NET_MAX_CONNECTIONS_ALLOWED; i++)
				  if (net_thread_state[i].sock != INVALID_SOCKET
					  && net_thread_state[i].channel == net_buf->channel)
					  ch = i;
			  
			  if (ch < 0) {
				  //канал не найден, удаляем сообщение
				  net_free_net_buf(net_buf);

			  } else {
				  
				  res = RTKPutCond(net_thread_state[ch].write_mailbox, net_buf);
				  if (res == FALSE) {
					  //возвращаем буфер в общую очередь
					  net_add_to_main_queue(NET_MAIN_QUEUE_SEND, net_buf->net_msg.priority, net_buf->channel, true, net_buf);
					  //без проверки возвращаемого значения, так как место точно должно быть - забрали только что из очереди буфер и удерживаем мютекс - никто не мог добавить новый на его место

					  //временно блокируем получения сообщений из очереди на этот канал, так как его очередь отправки забита
					  net_main_queue_skip_type(NET_MAIN_QUEUE_SEND, net_buf->channel);
				  }

			  }
		  }

		RTKSignal(send_queue_mutex);

	} while (net_buf);
	
	//удаляем фильтр выборки
	net_main_queue_skip_reset(NET_MAIN_QUEUE_SEND);
}


void net_dispatch_msgs() {

	net_buf_t* net_buf=NULL;


	do {

		//сохранить указатель на сообщение, передаваемый в обработчик, чтоб блокировать его удаление по завершению обработчика, если внутри него вызывались net_free_msg_buf или net_send_msg с этим же буфером
		net_buf = net_remove_from_main_queue_by_priority(NET_MAIN_QUEUE_RECV);

		if (net_buf) {

			save_callback_buf = (net_msg_t*)net_buf->net_msg.msg_data;
			
			if (net_callbacks[save_callback_buf->type] == NULL) {
				if (default_callback != NULL)
					default_callback(save_callback_buf, net_buf->channel);
			}
			else net_callbacks[save_callback_buf->type](save_callback_buf, net_buf->channel);

			//удаляю буфер после завершения обработчика
			if (save_callback_buf != NULL) {
				save_callback_buf = NULL;
				net_free_net_buf(net_buf);
			}
		}

	} while (net_buf);


}


//периодическая функция обработки
net_err_t net_update() {

	//проверить состояние всех потоков
	net_connection_control_func();

	//прочитать все текущие сообщения из входных очередей и записать в общую очередь приема согласно приоритетам
	net_read_recv_queues();
	
	//разобрать общую очередь на передачу и переместить в индивидуальные очереди на отправку для каждого канала согласно приоритетам
	net_write_send_queues();

	//вызвать обработчики для всех сообщений в общей очереди приема
	net_dispatch_msgs();


	return NET_ERR_NO_ERROR;
}


