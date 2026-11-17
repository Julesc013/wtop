/* Explicit bounded log delivery. SPDX-License-Identifier: GPL-3.0-only */
#include "output_nt.h"
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>

static FILE *owned_stream(HANDLE handle)
{
    int descriptor = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
    FILE *stream;
    if(descriptor < 0) { CloseHandle(handle); return NULL; }
    stream = _fdopen(descriptor, "w+b");
    if(!stream) _close(descriptor);
    return stream;
}

int wtop_log_open(wtop_log_sink *sink, const WCHAR *path, unsigned long limit)
{
    WCHAR directory[MAX_PATH + 1], temporary[MAX_PATH + 100];
    DWORD length;
    HANDLE handle = INVALID_HANDLE_VALUE;
    unsigned attempt;
    memset(sink, 0, sizeof(*sink));
    sink->previous_stdout_mode = -1;
    sink->limit = limit;
    if(!path || !*path || !limit) { sink->native_error = ERROR_INVALID_PARAMETER; return 0; }
    length = GetTempPathW((DWORD)(sizeof(directory) / sizeof(directory[0])), directory);
    if(!length || length >= sizeof(directory) / sizeof(directory[0])) { sink->native_error = GetLastError(); return 0; }
    for(attempt = 0; attempt < 16; ++attempt) {
        _snwprintf_s(temporary, sizeof(temporary) / sizeof(temporary[0]), _TRUNCATE,
            L"%swtop-log-%lu-%llu-%u.tmp", directory, GetCurrentProcessId(), GetTickCount64(), attempt);
        handle = CreateFileW(temporary, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if(handle != INVALID_HANDLE_VALUE) break;
        if(GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if(handle == INVALID_HANDLE_VALUE) { sink->native_error = GetLastError(); return 0; }
    sink->record = owned_stream(handle);
    if(!sink->record) { sink->native_error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
    if(!wcscmp(path, L"-")) {
        sink->previous_stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
        if(sink->previous_stdout_mode < 0) { sink->native_error = ERROR_INVALID_HANDLE; wtop_log_close(sink); return 0; }
        sink->output = stdout;
    } else {
        handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if(handle == INVALID_HANDLE_VALUE) { sink->native_error = GetLastError(); wtop_log_close(sink); return 0; }
        sink->output = owned_stream(handle);
        sink->owns_output = 1;
        if(!sink->output) { sink->native_error = ERROR_NOT_ENOUGH_MEMORY; wtop_log_close(sink); return 0; }
    }
    return 1;
}

FILE *wtop_log_begin(wtop_log_sink *sink)
{
    if(!sink->record || fflush(sink->record) || _fseeki64(sink->record, 0, SEEK_SET) ||
       _chsize_s(_fileno(sink->record), 0)) {
        sink->native_error = ERROR_WRITE_FAULT; return NULL;
    }
    clearerr(sink->record);
    return sink->record;
}

wtop_log_result wtop_log_commit(wtop_log_sink *sink)
{
    __int64 length;
    char buffer[8192];
    size_t count;
    if(!sink->record || !sink->output || fflush(sink->record) || ferror(sink->record)) goto failed;
    length = _ftelli64(sink->record);
    if(length < 0) goto failed;
    if((ULONGLONG)length > sink->limit - sink->committed_bytes) return WTOP_LOG_CAPACITY;
    if(_fseeki64(sink->record, 0, SEEK_SET)) goto failed;
    while((count = fread(buffer, 1, sizeof(buffer), sink->record)) != 0) {
        if(fwrite(buffer, 1, count, sink->output) != count) goto failed;
    }
    if(ferror(sink->record) || fflush(sink->output) || ferror(sink->output)) goto failed;
    sink->committed_bytes += (ULONGLONG)length;
    return WTOP_LOG_OK;
failed:
    sink->native_error = ERROR_WRITE_FAULT;
    /* Recover the last complete file boundary where possible. A broken stdout
       pipe or an interrupted filesystem cannot be rolled back. */
    if(sink->owns_output && sink->output) {
        fflush(sink->output);
        _chsize_s(_fileno(sink->output), sink->committed_bytes);
    }
    return WTOP_LOG_ERROR;
}

int wtop_log_close(wtop_log_sink *sink)
{
    int ok = 1;
    if(sink->record) { if(fclose(sink->record)) ok = 0; sink->record = NULL; }
    if(sink->output) {
        if(sink->owns_output) { if(fclose(sink->output)) ok = 0; }
        else if(fflush(sink->output)) ok = 0;
        sink->output = NULL;
    }
    if(sink->previous_stdout_mode >= 0) {
        if(_setmode(_fileno(stdout), sink->previous_stdout_mode) < 0) ok = 0;
        sink->previous_stdout_mode = -1;
    }
    return ok;
}
