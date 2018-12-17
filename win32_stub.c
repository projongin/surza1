
#define _USER32_
#define _KERNEL32_
#include <windows.h> 


WINBASEAPI BOOL WINAPI EnumSystemLocalesW(LOCALE_ENUMPROCW lpLocaleEnumProc, DWORD dwFlags) {
	return EnumSystemLocales((LOCALE_ENUMPROCA)lpLocaleEnumProc, dwFlags);
	//return TRUE;
}

