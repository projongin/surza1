#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#include "global_defines.h"

#define _BUF_POOL_DEBUG_PRINT



//--- принудительное отключение отладочной распечатки при глобальном запрете
#ifndef ALLOW_DEBUG_PRINT
#ifdef BUF_POOL_DEBUG_PRINT
#undef BUF_POOL_DEBUG_PRINT
#endif
#endif




//инициализация пула буферов
bool buf_pool_init();

//создать пул буферов, возвращает дескриптор пула или <0 при ошибке
//ф-ция только для однопоточного использования
int buf_pool_add_pool(size_t buf_size, unsigned buf_count);


//получить буфер из пула,   NULL  в случае ошибки
//ф-ция потокобезопасная (внутри мютекс)
void* buf_pool_get(int pool_num);
void* buf_pool_get_fast(int pool_num);  //быстрая версия без мьютекса

//отдать буфер
//ф-ция потокобезопасная (внутри мютекс)
bool buf_pool_free(int pool_num, void*);
bool buf_pool_free_fast(int pool_num, void*); //быстрая версия без мьютекса

//получить размер буфера, 0 в случае ошибки
size_t buf_pool_size(int pool_num);

//получить количество доступных буферов в пуле, <0 при ошибке
int buf_pool_bufs_available(int pool_num);
int buf_pool_bufs_available_fast(int pool_num); //быстрая версия без мьютекса

//----------------------
//тестовая функция
#ifdef BUF_POOL_DEBUG_PRINT
int buf_pool_test();
#endif
