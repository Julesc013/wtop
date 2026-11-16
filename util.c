/* 
 * NTop - an htop clone for Windows
 * Copyright (c) 2019 Gian Sass
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"
#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

/* Current native profile is Unicode. Redirected output is UTF-8, never UTF-16 bytes. */
int WriteTText(HANDLE output, const TCHAR *text, size_t characters)
{
	DWORD mode, written;
	if(GetConsoleMode(output, &mode)) {
		while(characters) {
			DWORD count = characters > 16384 ? 16384 : (DWORD)characters;
			if(!WriteConsole(output, text, count, &written, NULL) || !written) return 0;
			text += written;
			characters -= written;
		}
		return 1;
	}
#ifdef UNICODE
	if(characters > INT_MAX) return 0;
	int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)characters, NULL, 0, NULL, NULL);
	if(!bytes) return characters == 0;
	char *buffer = malloc((size_t)bytes);
	if(!buffer) return 0;
	if(!WideCharToMultiByte(CP_UTF8, 0, text, (int)characters, buffer, bytes, NULL, NULL)) {
		free(buffer);
		return 0;
	}
	const char *cursor = buffer;
	size_t remaining = (size_t)bytes;
#else
	const char *cursor = text;
	size_t remaining = characters;
#endif
	int ok = 1;
	while(remaining) {
		DWORD count = remaining > 16384 ? 16384 : (DWORD)remaining;
		if(!WriteFile(output, cursor, count, &written, NULL) || !written) { ok = 0; break; }
		cursor += written;
		remaining -= written;
	}
#ifdef UNICODE
	free(buffer);
#endif
	return ok;
}

char *ToUtf8(const TCHAR *text)
{
#ifdef UNICODE
	int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
	if(!length) return NULL;
	char *result = malloc((size_t)length);
	if(result && !WideCharToMultiByte(CP_UTF8, 0, text, -1, result, length, NULL, NULL)) {
		free(result);
		return NULL;
	}
	return result;
#else
	return _strdup(text);
#endif
}

TCHAR *FromUtf8(const char *text)
{
#ifdef UNICODE
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
	if(!length) return NULL;
	TCHAR *result = malloc((size_t)length * sizeof(*result));
	if(result && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, length)) {
		free(result);
		return NULL;
	}
	return result;
#else
	return _strdup(text);
#endif
}

NORETURN void Die(TCHAR *Fmt, ...)
{
	TCHAR Buffer[1024];
	va_list VaList;

	va_start(VaList, Fmt);
	_vsntprintf_s(Buffer, _countof(Buffer), _TRUNCATE, Fmt, VaList);
	va_end(VaList);

	WriteTText(GetStdHandle(STD_ERROR_HANDLE), Buffer, _tcslen(Buffer));
	exit(EXIT_FAILURE);
}

extern HANDLE ConsoleHandle;

/*
 * Instant-fail memory allocators as I believe that helps keep the code clean
 */

void *xmalloc(size_t size)
{
	void *m = malloc(size);

	if(!m)
		Die(_T("malloc"));

	return m;
}

void *xrealloc(void *ptr, size_t size)
{
	void *m = realloc(ptr, size);

	if(!m)
		Die(_T("realloc"));

	return m;
}

void *xcalloc(size_t num, size_t size)
{
	void *m = calloc(num, size);

	if(!m)
		Die(_T("calloc"));

	return m;
}
