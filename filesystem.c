
#include <rtfiles.h>

#include <stdbool.h>
#include <stdlib.h>

#include "filesystem.h"




int filesystem_set_current_dir(char* dir_name) {
	return RTFSetCurrentDir(dir_name);
}

int filesystem_delete_file(char* filename) {
	return RTFDelete(filename);
}



int filesystem_read_file_fragments(char* filename, filesystem_fragment_t* fragments, unsigned num) {

	RTFHANDLE f_handle;

	f_handle = RTFOpen(filename, RTF_READ_ONLY);
	if (f_handle < 0)
		return false;

	bool ok = true;
	int res;
	unsigned int bytes = 0;
	unsigned n = 0;

	while (n < num) {
		fragments[n].pointer = NULL;
		n++;
	}

	n = 0;

	while (ok &&  n<num) {

		if (fragments[n].size == 0) {

			//определ€ю все оставшиес€ в файле данные

			RTFFileInfoEx file_info;

			if (RTFGetFileInfoEx(f_handle, &file_info, 0) != RTF_NO_ERROR) {
				ok = false;
				continue;
			}

			fragments[n].size = file_info.FileSize.u.LowPart - file_info.FilePos.u.LowPart;
		}

		fragments[n].pointer = malloc(fragments[n].size);
		if (!fragments[n].pointer)
			ok = false;

		if (ok) {
			res = RTFRead(f_handle, fragments[n].pointer, fragments[n].size, &bytes);
			if (res != RTF_NO_ERROR || bytes != fragments[n].size)
				ok = false;
		}
		
		n++;
	}

	RTFClose(f_handle);

	if (!ok) {
		n = 0;
		while (n < num) {
			if (fragments[n].pointer)
				free(fragments[n].pointer);
			n++;
		}
	}

	return ok ? true : FILESYSTEM_IO_ERR;

}


int filesystem_read_file(char* filename, void** data, unsigned* size) {

	filesystem_fragment_t fr;

	fr.pointer = 0;
	fr.size = 0; // запросить чтение всего файла

	bool res = filesystem_read_file_fragments(filename, &fr, 1);
	if (res) {
		*data = fr.pointer;
		*size = fr.size;
	}

	return res ? true : FILESYSTEM_IO_ERR;
	
}




int filesystem_write_file_fragments(char* filename, const filesystem_fragment_t* fragments, unsigned num) {

	RTFHANDLE f_handle;

	f_handle = RTFOpen(filename, RTF_CREATE | RTF_CREATE_ALWAYS | RTF_READ_WRITE);
	if (f_handle < 0)
		return false;

	bool ok = true;
	int res;
	unsigned int bytes=0;
	unsigned n=0;

	while (ok &&  n<num) {

		res = RTFWrite(f_handle, fragments[n].pointer, fragments[n].size, &bytes);
		if (res != RTF_NO_ERROR || bytes != fragments[n].size)
			ok = false;

		n++;
	}

	RTFClose(f_handle);

	return ok ? true : FILESYSTEM_IO_ERR;
}


int filesystem_write_file(char* filename, const void* data, unsigned size) {

	filesystem_fragment_t fr;

	fr.pointer = (char*)data;
	fr.size = size;

	return filesystem_write_file_fragments(filename, &fr, 1);
}

