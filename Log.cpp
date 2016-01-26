#include "stdafx.h"
#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "Log.h"

void LOG(const char* format, ...)
{
	SYSTEMTIME st;
	GetSystemTime(&st);

	printf("%02d%02d%02d.%03d: ", 
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}
