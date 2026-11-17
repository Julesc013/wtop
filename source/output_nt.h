/* Explicit bounded log delivery. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_OUTPUT_NT_H
#define WTOP_OUTPUT_NT_H
#include <stdio.h>
#include <windows.h>
typedef enum wtop_log_result { WTOP_LOG_OK, WTOP_LOG_CAPACITY, WTOP_LOG_ERROR } wtop_log_result;
typedef struct wtop_log_sink {
    FILE *output, *record;
    unsigned long limit;
    ULONGLONG committed_bytes;
    int owns_output, previous_stdout_mode;
    DWORD native_error;
} wtop_log_sink;
/* Explicit log destinations are created exclusively. '-' selects stdout.
   One record is staged in a private delete-on-close temporary file so a byte
   cap never intentionally emits half a JSON record. Native filesystem/pipe
   calls themselves have no hard deadline; cancellation is owned by the host. */
int wtop_log_open(wtop_log_sink *sink, const WCHAR *path, unsigned long limit);
FILE *wtop_log_begin(wtop_log_sink *sink);
wtop_log_result wtop_log_commit(wtop_log_sink *sink);
int wtop_log_close(wtop_log_sink *sink);
#endif
