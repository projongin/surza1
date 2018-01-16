#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>


#include <rtk32.h>
#include <socket.h>
#include <Clock.h>


#include "net.h"
#include "buf_pool.h"

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

	Функция получения буфера должна вызываться под каждое отправляемое сообщение (кроме случая использования буфера полученного сообщения в обработчике сообщений.
	   Самостоятельно определяет тип буфера под разный размер сообщений (пул буферов или память из кучи).
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
	uint32_t          buf_size;   //размер буфера
	uint64_t          channel;    //канал(номер клиента) отправки,  NET_BROADCAST_CHANNEL  - отправка всем подключенным, формируется на основе ip адреса и порта подключенного клиента
	net_raw_msg_t     net_msg;
} net_buf_t;

#pragma pack(pop)






//максимальное количество сообщений  в очереди на отправку и получение для каждого потока
#define NET_THREAD_QUEUE_LENGTH   1000

//максимальное количество сообщений в общих очередях на отправку и получение
#define NET_MAIN_QUEUE_LENGTH     (NET_THREAD_QUEUE_LENGTH*NET_MAX_CONNECTIONS_ALLOWED)





#define NET_MSG_OFFSET         (offsetof(net_raw_msg_t, msg_data))
#define NET_RAW_MSG_OFFSET     (offsetof(net_buf_t, net_msg))

#define NET_BUF_TO_MSG_OFFSET  (NET_RAW_MSG_OFFSET + NET_RAW_MSG_OFFSET)

#define NET_BUF_OVERHEAD       (NET_BUF_TO_MSG_OFFSET + sizeof(net_msg_t) + 4)


#define NET_MAX_RAW_MSG_LENGTH   (NET_MAX_MSG_DATA_LENGTH + sizeof(net_msg_t) + 4)
#define NET_MAX_NET_BUF_SIZE     (NET_MAX_MSG_DATA_LENGTH + NET_BUF_OVERHEAD)


#define NET_BUF_SIZE           4096                          //размер буферов  в пуле буферов
#define NET_BUF_POOL_SIZE      (NET_MAIN_QUEUE_LENGTH*5)     //размер пула буферов




//--------------------------------------------------------------------------
// работа с буферами
//--------------------------------------------------------------------------

static int net_buf_pool;

static net_msg_t* save_callback_buf = NULL;


//иницализация
bool net_buf_pool_init() {
	
	net_buf_pool = buf_pool_add_pool(NET_BUF_SIZE, NET_BUF_POOL_SIZE);
	if (net_buf_pool < 0)
		return false;

	return true;
}



//получение нового буфера
net_buf_t* net_get_net_buf(size_t len) {

	net_buf_t* buf;

	if (len <= NET_BUF_SIZE) {
		//получаем новый буфер из пула
		buf = buf_pool_get(net_buf_pool);
		if (buf == NULL)
			return NULL;

		buf->buf_type = NET_BUF_TYPE_POOL;
		buf->buf_size = NET_BUF_SIZE;
	}
	else {
		//делаем новый буфер в куче
		
		buf = malloc(len);
		if (buf == NULL)
			return NULL;

		buf->buf_type = NET_BUF_TYPE_HEAP;
		buf->buf_size = len;
	}

	return buf;
}


//возврат буфера
void net_free_net_buf(net_buf_t* buf) {

	if (buf == NULL) return;

	if (buf->buf_type == NET_BUF_TYPE_POOL) {
		buf_pool_free(net_buf_pool, buf);
	}
	else if (buf->buf_type == NET_BUF_TYPE_HEAP) {
		free(buf);
	}
	
}

//получение нового буфера и инициализация копией buf,  возвращает  NULL при неудаче выделения нового буфера
net_buf_t* net_copy_net_buf(const net_buf_t* buf) {

	if (buf == NULL)
		return NULL;

	net_buf_t* buf_copy = net_get_net_buf(buf->buf_size);

	if (buf_copy != NULL)
		memcpy(buf_copy, buf, buf->buf_size);

	return buf_copy;
}



//получение net_buf_t* из net_msg_t*
net_buf_t* net_get_net_buf_from_msg(const net_msg_t* ptr) {
	return ptr==NULL?NULL:((net_buf_t*)((uint8_t*)ptr-NET_BUF_TO_MSG_OFFSET));
}

//получение нового буфера под сообщение с длиной данных len
net_msg_t* net_get_msg_buf(uint32_t len) {
	
	net_buf_t* buf = net_get_net_buf(NET_BUF_OVERHEAD + len);
	if (buf == NULL)
		return NULL;

	net_msg_t* msg = (net_msg_t*)(buf->net_msg.msg_data);

	msg->size = len;
	
	return msg;
}

//возврат буфера
void net_free_msg_buf(net_msg_t* buf) {

	if (buf == NULL)
		return;

	if (save_callback_buf == buf)
		save_callback_buf = NULL;

	net_free_net_buf(net_get_net_buf_from_msg(buf));
}

//получить максимально возможный размер данных для сохранения в сообщении
size_t net_msg_buf_get_available_space(const net_msg_t* buf) {

	if (buf == NULL)
		return 0;

	net_buf_t* net_buf = net_get_net_buf_from_msg(buf);

	return net_buf->buf_size - NET_BUF_OVERHEAD;
};


//---------------------------------------------------------------------------






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







#define DEVICE_ID     I82559_DEVICE
#define DEVICE_MINOR  0               // first device of type DEVICE_ID (use 1 for second, 2 for third, etc)


// The following values are ignored for PCI devices (the BIOS supplies
// them), but they must be set correctly for ISA systems

#define ED_IO_ADD     0//0x300              // I/O address of the device
#define ED_IRQ        (-1)//5               // IRQ         of the device
#define ED_MEM_ADD    0                  // Memory Window (only some devices)


static int interface = SOCKET_ERROR;


//сокет tcp сервера 
static SOCKET server_sock; 

//обработчик сообщений по-умолчанию
static net_msg_dispatcher default_callback = 0;  

//разрешение вызова обработчиков
volatile static int callback_en = 0;

//индивидуальные обработчики сообщений для каждого типа
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




//мютекс доступа к общей очереди отправки
RTKSemaphore send_queue_mutex;



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
	if (!net_buf_pool_init()) {
		LOG_AND_SCREEN("net_init(): buf_pool_init() failed");
		goto err;
	}


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





//посылка нового сообщения
net_err_t net_send_msg(net_msg_t* msg, net_msg_priority_t priority, unsigned channel) {

	if (save_callback_buf == msg)
		save_callback_buf = NULL;

	net_buf_t* net_buf = net_get_net_buf_from_msg(msg);

	//проверка длины
	if (net_buf->buf_size < NET_BUF_OVERHEAD + msg->size) {
		net_free_net_buf(net_buf);
		return NET_ERR_INVALID_MSG;
	}
		

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





enum net_reader_states_t {
	NET_READER_STATE_GET_BUF = 0,
	NET_READER_STATE_READ_SIZE,
	NET_READER_STATE_READ_MSG,
	NET_READER_STATE_CHECK_MSG,
	NET_READER_STATE_SAVE_MSG
};

 

void net_reader_thread_func(void* params) {

	net_thread_state_t* data = (net_thread_state_t*)params;

	
	int state = NET_READER_STATE_GET_BUF;
	net_buf_t* pool_buf=NULL;
	net_buf_t* heap_buf=NULL;

	net_buf_t* buf = NULL;
	net_msg_t* msg;


	int ret;
	
	uint32_t msg_size;
	uint8_t* data_ptr=NULL;
	int bytes_left;
	int bytes_read=0;

	uint32_t buf_len;

	enum net_buf_type buf_type;

	bool exit = false;


	while (!exit) {
		
		switch (state) {

		  case NET_READER_STATE_GET_BUF:   //получение буфера
			       
			       LOG_AND_SCREEN("label_0,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

			       if (pool_buf==NULL) {
					   pool_buf = net_get_net_buf(NET_BUF_SIZE);
					   if (!pool_buf) {
						   RTKDelay(CLKMilliSecsToTicks(200));
						   break;
					   }
				   }

				   bytes_left = 4;
				   data_ptr = (uint8_t*)&msg_size;
				   bytes_read = 0;

				   state = NET_READER_STATE_READ_SIZE;

				   LOG_AND_SCREEN("label_1,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));
			        
			  break;

		  case NET_READER_STATE_READ_SIZE:   //чтение общей длины сообщения
			       
			      ret = recv(data->sock, data_ptr, bytes_left, 0);
				  if (ret == SOCKET_ERROR) {
					  exit = true;
					  break;
				  }

				  LOG_AND_SCREEN("label_2,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				  bytes_read += ret;
				  bytes_left -= ret;
				  data_ptr += ret;

				  if (bytes_read < 4)
					  break;

				  LOG_AND_SCREEN("label_3,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				  //хватает данных на определение длины сообщения

				  if (msg_size < sizeof(net_raw_msg_t) + sizeof(net_msg_t) + 4) {
					  //если длина меньше минимально допустимой (не хватает даже на сообщение без данных), то выходим
					  exit = true;
					  break;
				  }

				  LOG_AND_SCREEN("label_4,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				  buf_len = msg_size + NET_RAW_MSG_OFFSET;
				  if (buf_len > NET_MAX_NET_BUF_SIZE) {
					  //если длина превышает максимально допустимую длину сообщения  - выходим
					  exit = true;
					  break;
				  }

				  LOG_AND_SCREEN("label_5,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				  //выделение буфера из кучи, если длины буфера из пула недостаточно
				  if (buf_len > NET_BUF_SIZE) {
					  heap_buf = net_get_net_buf(buf_len);
					  //при неудаче выделения буфера выходим
					  if (heap_buf == NULL) {
						  exit = true;
						  break;
					  }
					  buf = heap_buf;
				  }
				  else buf = pool_buf;

				  LOG_AND_SCREEN("label_6,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				  buf->net_msg.size = msg_size;
				  data_ptr = (uint8_t*)&buf->net_msg.size + 4;
				  bytes_left = msg_size - bytes_read;

				  state = NET_READER_STATE_READ_MSG;

			  break;
		  
		  case NET_READER_STATE_READ_MSG:   //получение всего тела сообщения с таймаутом

				   ret = recv(data->sock, data_ptr, bytes_left, 0);
				   if (ret == SOCKET_ERROR) {
					   exit = true;
					   break;
				   }

				   LOG_AND_SCREEN("label_7,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				   if (ret<0 || ret>bytes_left) {
					   exit = true;
					   break;
				   }

				   LOG_AND_SCREEN("label_8,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));
				   
				   data_ptr += ret;
				   bytes_left -= ret;

				   if (bytes_left == 0)
					   state = NET_READER_STATE_CHECK_MSG;

			  break;

		  case NET_READER_STATE_CHECK_MSG:   //проверка сообщения

			       LOG_AND_SCREEN("label_9,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

			        //если не проходит проверку - выходим
			       if (memcmp(&buf->net_msg.label, data_ptr-4, 4)) {
					   exit = true;
					   break;
					}

				   LOG_AND_SCREEN("label_10,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				   msg = (net_msg_t*)&buf->net_msg.msg_data;
				   if (msg_size != sizeof(net_raw_msg_t) + sizeof(net_msg_t) + msg->size + 4) {
					   //что-то не так с размерами сообщения, выходим
					   exit = true;
					   break;
				   }

				   LOG_AND_SCREEN("label_11,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

				   state = NET_READER_STATE_SAVE_MSG;

			  break;

		  case NET_READER_STATE_SAVE_MSG:   //добавляем сообщение в очередь с проверкой ее забитости
			         
			         LOG_AND_SCREEN("label_12,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

			         buf_type = buf->buf_type;
			  
			         if (RTKPutCond(data->read_mailbox, buf) == FALSE) {
						 RTKDelay(CLKMilliSecsToTicks(200));   //очередь забита, пробуем снова с задержкой
						 break;
					 }

					 LOG_AND_SCREEN("label_13,  time=%d", CLKTicksToMilliSecs(RTKGetTime()));

					 //сообщение успешно добавлено во входную очередь

					 if (buf_type == NET_BUF_TYPE_POOL)
						 pool_buf = NULL;
					 if (buf_type == NET_BUF_TYPE_HEAP)
						 heap_buf = NULL;

					 buf = NULL;

					 state = NET_READER_STATE_GET_BUF;

			  break;

		}


		if (atom_get_state(&data->thread_cnt_atomic) < 2)
			exit = true;

	}


	if (pool_buf) net_free_net_buf(pool_buf);
	if (heap_buf) net_free_net_buf(heap_buf);
	
	atom_dec(&data->thread_cnt_atomic);
}


enum net_writer_states_t {
	NET_WRITER_STATE_GET_MSG = 0,
	NET_WRITER_STATE_SEND_MSG
};


void net_writer_thread_func(void* params) {

	net_thread_state_t* data = (net_thread_state_t*)params;

	net_buf_t* buf = NULL;

	int bytes_left;
	uint8_t* ptr;
	int ret;
	

	int state = NET_WRITER_STATE_GET_MSG;

	bool exit = false;

	while (!exit) {

		switch (state) {
		case NET_WRITER_STATE_GET_MSG:

			if (!RTKGetTimed(data->write_mailbox, buf, CLKMilliSecsToTicks(1000)))
				break;
			if (buf == NULL) {
				exit = true;
				break;
			}

			//есть сообщения для передачи, подготавливаю для передачи
			net_msg_t* msg = (net_msg_t*)&buf->net_msg.msg_data;
			buf->net_msg.label = data->label;
			memcpy((uint8_t*)msg + sizeof(net_msg_t) + msg->size , &buf->net_msg.label, 4);
			buf->net_msg.size = sizeof(net_raw_msg_t) + sizeof(sizeof(net_msg_t)) + msg->size + 4;

			data->label++;

			ptr = (uint8_t*)&buf->net_msg;
			bytes_left = (int)buf->net_msg.size;

			state = NET_WRITER_STATE_SEND_MSG;

			break;

		case NET_WRITER_STATE_SEND_MSG:

			ret = net_send(data->sock, ptr, bytes_left);
			if (ret == SOCKET_ERROR || ret>bytes_left) {
				exit = true;
				break;
			}
			
			bytes_left -= ret;
			
			if (bytes_left == 0) {
				net_free_net_buf(buf);
				buf = NULL;
				state = NET_WRITER_STATE_GET_MSG;
			}

			break;
		}


		if (atom_get_state(&data->thread_cnt_atomic) < 2)
			exit = true;

	}


	if (buf) net_free_net_buf(buf);


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
		

				net_set_blocking(thread_data->sock);
				

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


