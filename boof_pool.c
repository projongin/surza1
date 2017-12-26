
#include <Rtk32.h>
#include <stdlib.h>

#include "buf_pool.h"


#ifdef BUF_POOL_DEBUG_PRINT
#include <stdio.h>
#endif



static bool buf_pool_init_ok = false;
static int buf_pool_num = 0;

typedef  struct {
	RTKSemaphore mutex;
	unsigned size;        //количество буферов
	size_t   buf_size;    //размер буферов
	unsigned stack_ptr;   //указатель стека буферов
	void* (*stack)[];     //стек указателей (выделяется под хранение укателей на буферы)
	void* pool;           //указатель на выделенную память под буферы
} buf_pool_t;


#define BUF_POOL_MAX_POOLS   10

static buf_pool_t buf_pools[BUF_POOL_MAX_POOLS];



//инициализация пула буферов

bool buf_pool_init() {
	if (buf_pool_init_ok) return false;

	for (int i = 0; i < BUF_POOL_MAX_POOLS; i++) {
		buf_pools[i].mutex = RTK_NO_SEMAPHORE;
		buf_pools[i].size = 0;
		buf_pools[i].stack_ptr = 0;
		buf_pools[i].stack = NULL;
		buf_pools[i].pool = NULL;
	}

	buf_pool_num = 0;
	buf_pool_init_ok = true;

	return true;
}


//создать пул буферов, возвращает дескриптор пула или <0 при ошибке
int buf_pool_add_pool(size_t buf_size, unsigned buf_count) {


	if (!buf_pool_init_ok || buf_pool_num >= BUF_POOL_MAX_POOLS) return -1;

	buf_pool_t* pool = &buf_pools[buf_pool_num];

	pool->stack = (void* (*)[])malloc(sizeof(void*)*buf_count);
	if (pool->stack == NULL)
		return -2;
	
	pool->pool = calloc(buf_size, buf_count);
	if (pool->pool == NULL) {
		free(pool->stack);
		return -3;
	}

	char* p = (char*)pool->pool;
	for (unsigned i = 0; i < buf_count; i++) {
		(*pool->stack)[i] = (void*)p;
		p += buf_size;
	}

	pool->size = buf_count;
	pool->buf_size = buf_size;
	pool->stack_ptr = 0;

	//создаю мьютекс
	pool->mutex = RTKOpenSemaphore(ST_MUTEX, 1, SF_COPY_NAME, "buf_pool_mutex");

	
	return buf_pool_num++;
}


//получить буфер из пула,   NULL  в случае ошибки
void* buf_pool_get(int pool_num) {

	if (!buf_pool_init_ok || pool_num>=buf_pool_num) return NULL;

	buf_pool_t* pool = &buf_pools[pool_num];

	void* p=NULL;

	RTKWait(pool->mutex);
	 if (pool->stack_ptr < pool->size) {
		 p = (*pool->stack)[pool->stack_ptr++];
	 }
	RTKSignal(pool->mutex);

		
#ifdef BUF_POOL_DEBUG_PRINT
	printf("buf_pool_get(%d) %s\n", pool_num, p?"ok":"failed");
#endif

	return p;
}



//отдать буфер
bool buf_pool_free(int pool_num, void* ptr) {

	if (!buf_pool_init_ok || pool_num >= buf_pool_num || ptr==NULL) return false;

	buf_pool_t* pool = &buf_pools[pool_num];

	bool ok = false;
	RTKWait(pool->mutex);
	  if (pool->stack_ptr > 0) {
		  (*pool->stack)[--pool->stack_ptr] = ptr;
		  ok = true;
	  }
	RTKSignal(pool->mutex);


#ifdef BUF_POOL_DEBUG_PRINT
	printf("buf_pool_free(%d)\n", pool_num);
#endif

	return ok;
}


//получить размер буфера, 0 в случае ошибки
size_t buf_pool_size(int pool_num) {
	if (!buf_pool_init_ok || pool_num >= buf_pool_num) return 0;

	return buf_pools[pool_num].buf_size;
}



//==============================================================================================

#ifdef BUF_POOL_DEBUG_PRINT
//тестовая функция
int buf_pool_test() {

	
	enum MAX_POOLS {MAX_POOLS= BUF_POOL_MAX_POOLS + 1, MAX_BUFS=5, TEST_BUF_SIZE=4096};
	int pools[MAX_POOLS];

	//проверяем создание
	for (int i = 0; i < MAX_POOLS; i++) {
		pools[i] = buf_pool_add_pool(TEST_BUF_SIZE, MAX_BUFS);
		if ((pools[i] < 0 && i != MAX_POOLS - 1)
			|| (pools[i] >= 0 && i == MAX_POOLS - 1))
			return -1;
	}

	//тест на исчерпание пула
	void* ptr[MAX_BUFS];
	for (int i = 0; i < MAX_BUFS; i++) {
		ptr[i] = buf_pool_get(0);
		if (ptr[i] == NULL) return -2;
	}

	//пробуем получить несуществующий буфер
	if (buf_pool_get(0) != NULL)
		return -3;

	//отдаем один буфер обратно
	buf_pool_free(0, ptr[0]);

	//пробуем повторно получить буфер
	ptr[0] = buf_pool_get(0);
	if (ptr[0] == NULL)
		return -5;


	printf("BUF_POOL TEST PASSED SUCCESSFULLY\n");

	return 0;
}
#endif
