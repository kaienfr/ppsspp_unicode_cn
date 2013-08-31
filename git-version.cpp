// This is a generated file. 
#include <Windows.h>
char * ANSI2UTF8(char *string){
	// ANSI to UNICODE
	int size = MultiByteToWideChar(GetACP(),0,string,-1,NULL,0);
	wchar_t *uni_wchars = new wchar_t[size + 1];
	size = MultiByteToWideChar(CP_ACP, 0, string, -1, uni_wchars, size);
	uni_wchars[size]=0;


	// UNICODE to UTF8 Multibyte string
	size = WideCharToMultiByte(CP_UTF8,0,uni_wchars,-1,NULL,0,NULL,NULL);
	char *utf8_string = new char[size + 1];
	size = WideCharToMultiByte(CP_UTF8,0,uni_wchars, -1, utf8_string, size,NULL,NULL);
	utf8_string[size]=0;

	return utf8_string;
}

const char *PPSSPP_GIT_VERSION =ANSI2UTF8("v0.9.1 ÖÐÎÄÐÞ¶©°æ"); 
 
// If you don't want this file to update/recompile, change to 1. 
#define PPSSPP_GIT_VERSION_NO_UPDATE 1

