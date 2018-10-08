#pragma once

#include <Rtk32.h>

#include <stddef.h>
#include <stdint.h>


#define _NET_DEBUG_PRINT

//--- принудительное отключение отладочной распечатки при глобальном запрете
#ifndef ALLOW_DEBUG_PRINT
#ifdef NET_DEBUG_PRINT
#undef NET_DEBUG_PRINT
#endif
#endif



//максимально возможное число одновременных подключений
#define  NET_MAX_CONNECTIONS_ALLOWED  5

//максимально допустимое количество данных в передаваемом сообщении
#define  NET_MAX_MSG_DATA_LENGTH    (32*1024*1024)



//номер канала для отправки всем подключенным клиентам
#define NET_BROADCAST_CHANNEL          UINT64_MAX



typedef enum {
	NET_ERR_NO_ERROR = 0,      //нет ошибок
	NET_ERR_ANY,               //любая, неопределенная ошибка (когда код ошибки не важен)
	NET_ERR_QUEUE_OVERFLOW,    //переполнение очереди сообщений
	NET_ERR_MEM_ALLOC,         //ошибка выделения памяти
	NET_ERR_NO_CONNECTIONS,    //нет активных соединений
	NET_ERR_INVALID_MSG        //некорректное сообщение
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
	uint8_t  type;           //типа сообщения
	uint8_t  subtype;        //подтип
	uint32_t size;           //длина данных
	uint8_t  data[];         //данные сообщения
} net_msg_t;
#pragma pack(pop)


//тип обработчика сообщений
typedef void(*net_msg_dispatcher)(net_msg_t* msg, uint64_t channel);

//тип обработчика данных реального времени, вызывается из прерывания сетевого контроллера
typedef void(*net_realtime_callback)(const void* data, int length);


//инициализация контроллера и создание tcp сервера
//колбэк сообщений по-умолчанию и колбэк реального времени могут быть  NULL если не используются
net_err_t net_init(net_msg_dispatcher default_dispatcher, net_realtime_callback real_callback);


//разрешение получения сообщений
void net_start(void);
//запрет получения сообщений
void net_stop(void);

//добавление обработчика сообщений определенного типа
void net_add_dispatcher(uint8_t type, net_msg_dispatcher dispatcher);


//возвращает количество подключенных клиентов, =0 если нет соединений
volatile unsigned net_connections();


//получение нового буфера под сообщение с длиной данных len
net_msg_t* net_get_msg_buf(uint32_t len);

//получить максимально возможный размер данных для сохранения в сообщении
size_t net_msg_buf_get_available_space(const net_msg_t* buf);

//возврат буфера
void net_free_msg_buf(net_msg_t* buf);

//посылка нового сообщения
//при любом исходе функция сама освобождает переданный ей буфер с сообщением
net_err_t net_send_msg(net_msg_t* msg, net_msg_priority_t priority, uint64_t channel);

//периодическая функция обработки
net_err_t net_update(void);










