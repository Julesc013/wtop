/* Native bounded-output fixtures. SPDX-License-Identifier: GPL-3.0-only */
#include "output_nt.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    WCHAR root[MAX_PATH + 1], directory[MAX_PATH + 80], path[MAX_PATH + 100];
    wtop_log_sink sink;
    FILE *record;
    HANDLE file;
    DWORD count;
    char bytes[8] = {0};
    assert(GetTempPathW(MAX_PATH, root));
    _snwprintf_s(directory, sizeof(directory) / sizeof(directory[0]), _TRUNCATE,
        L"%swtop-output-test-%lu-%llu", root, GetCurrentProcessId(), GetTickCount64());
    assert(CreateDirectoryW(directory, NULL));
    _snwprintf_s(path, sizeof(path) / sizeof(path[0]), _TRUNCATE, L"%s\\capture.jsonl", directory);
    assert(wtop_log_open(&sink, path, 5));
    record = wtop_log_begin(&sink); assert(record);
    assert(fputs("{}\n", record) >= 0);
    assert(wtop_log_commit(&sink) == WTOP_LOG_OK && sink.committed_bytes == 3);
    record = wtop_log_begin(&sink); assert(record);
    assert(fputs("{}\n", record) >= 0);
    assert(wtop_log_commit(&sink) == WTOP_LOG_CAPACITY && sink.committed_bytes == 3);
    assert(wtop_log_close(&sink));
    assert(!wtop_log_open(&sink, path, 4096)); /* An existing file is never truncated. */
    assert(sink.native_error == ERROR_FILE_EXISTS || sink.native_error == ERROR_ALREADY_EXISTS);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(ReadFile(file, bytes, sizeof(bytes), &count, NULL));
    assert(count == 3 && !memcmp(bytes, "{}\n", 3));
    assert(CloseHandle(file));
    assert(DeleteFileW(path)); assert(RemoveDirectoryW(directory));
    puts("native log: exact complete-record byte cap and existing-file preservation passed");
    return 0;
}
