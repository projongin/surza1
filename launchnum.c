
#include <stdint.h>

#include "filesystem.h"
#include "crc32.h"




#define LAUNCHNUM_FILENAME   "launchnum.dat"


static uint32_t launchnum_num;

#pragma pack(push)
#pragma pack(1) 
typedef struct {
	uint32_t launchnum;
	uint32_t crc32;
} launchnum_t;
#pragma pack(pop)


bool launchnum_read(uint32_t* num) {

	//чтение файла

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR)
		return false;

	launchnum_t s;

	if (filesystem_read(LAUNCHNUM_FILENAME, &s, sizeof(s)) != FILESYSTEM_NO_ERR)
		return false;

	if (!crc32_check((const char*)&s, 4, s.crc32))
		return false;

	*num = s.launchnum;

	return true;
}


bool launchnum_write(uint32_t num) {


	//запись файла

	if (filesystem_set_current_dir("C:\\") != FILESYSTEM_NO_ERR)
		return false;

	launchnum_t s;

	s.launchnum = num;
	s.crc32 = crc32((const char*)&s, 4);

	if (filesystem_write(LAUNCHNUM_FILENAME, &s, sizeof(s)) != FILESYSTEM_NO_ERR)
		return false;

	return true;
}


void launchnum_init() {

	if (!launchnum_read(&launchnum_num))
		launchnum_num = 1;

	launchnum_write(launchnum_num + 1);

}



uint32_t launchnum_get() {
	return launchnum_num;
}

