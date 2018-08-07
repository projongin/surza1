#pragma once



#define FILESYSTEM_NO_DIR       (-10)
#define FILESYSTEM_NO_FILE      (-11)
#define FILESYSTEM_ALLOC_ERR    (-12)
#define FILESYSTEM_IO_ERR       (-13)



int filesystem_set_current_dir(char* dir_name);

int read_file(char* filename, void** data, unsigned* size);

int write_file(char* filename, void* data, unsigned size);


