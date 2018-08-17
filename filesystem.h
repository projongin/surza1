#pragma once

#include <rtfiles.h>


#define FILESYSTEM_NO_ERR          0

#define FILESYSTEM_NO_DIR       (-10)
#define FILESYSTEM_NO_FILE      (-11)
#define FILESYSTEM_ALLOC_ERR    (-12)
#define FILESYSTEM_IO_ERR       (-13)
#define FILESYSTEM_OPEN_ERR     (-14)



typedef struct {
	char* pointer;
	unsigned size;
} filesystem_fragment_t;



int filesystem_set_current_dir(char* dir_name);

int filesystem_read_file(char* filename, void** data, unsigned* size);  //function allocs memory

int filesystem_read_file_fragments(char* filename, filesystem_fragment_t* fragments, unsigned num);  //function allocs memory

int filesystem_write_file(char* filename, const void* data, unsigned size);

int filesystem_write_file_fragments(char* filename, const filesystem_fragment_t* fragments, unsigned num);

int filesystem_delete_file(char* filename);
