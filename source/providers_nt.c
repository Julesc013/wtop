/* Optional NT providers and their bounded child ownership.
 * SPDX-License-Identifier: GPL-3.0-only
 * API contracts: Microsoft Learn, Win32_Process, UpdateProcThreadAttribute,
 * AssignProcessToJobObject, PdhAddEnglishCounterW and EnumServicesStatusExW.
 * Local child pipes carry no unbounded text or pointers. The private wire ABI
 * is versioned and valid only between the same executable/architecture.
 */
#define COBJMACROS
#include "providers_nt.h"
#include <wbemidl.h>
#include <oleauto.h>
#include <sddl.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <float.h>
#include <errno.h>

#define WIRE_MAGIC 0x57545031UL
#define WIRE_VERSION 1UL
#define WIRE_MAX_BYTES 1048576UL
#define RETIRE_GRACE_MS 250UL
#define REQUEST_PIPE_BYTES 4096UL
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct request_packet {
    DWORD magic, version, bytes, sequence;
    wtop_provider_request request;
} request_packet;
typedef struct response_packet {
    DWORD magic, version, bytes, sequence, checksum;
    wtop_provider_result result;
} response_packet;
typedef char bounded_response[(sizeof(response_packet) <= WIRE_MAX_BYTES) ? 1 : -1];
typedef char bounded_request[(sizeof(request_packet) <= REQUEST_PIPE_BYTES) ? 1 : -1];

static size_t wide_length(const WCHAR *text, size_t capacity)
{
    size_t i;
    for(i = 0; i < capacity && text[i]; ++i) {}
    return i;
}
static int copy_wide(WCHAR *target, size_t capacity, const WCHAR *source)
{
    size_t length = wide_length(source, capacity);
    if(length >= capacity) { if(capacity) target[0] = 0; return 0; }
    memcpy(target, source, (length + 1) * sizeof(WCHAR));
    return 1;
}
static DWORD checksum_bytes(const void *data, size_t count)
{
    const unsigned char *bytes = data;
    DWORD value = 2166136261UL;
    size_t i;
    for(i = 0; i < count; ++i) value = (value ^ bytes[i]) * 16777619UL;
    return value;
}
static int valid_kind(wtop_provider_kind kind)
{
    if(kind >= WTOP_PROVIDER_COMMAND_LINE && kind <= WTOP_PROVIDER_ACCOUNT) return 1;
#ifdef WTOP_PROVIDER_TESTING
    if(kind >= WTOP_PROVIDER_TEST_VALID && kind <= WTOP_PROVIDER_TEST_EXTRA) return 1;
#endif
    return 0;
}
static int valid_request(const wtop_provider_request *request)
{
    if(!request || !valid_kind(request->kind)) return 0;
    if(request->kind == WTOP_PROVIDER_PER_CORE && (request->interval_ms < 50 || request->interval_ms > 60000)) return 0;
    if(request->kind == WTOP_PROVIDER_ACCOUNT &&
       (!request->sid[0] || wide_length(request->sid, ARRAY_COUNT(request->sid)) == ARRAY_COUNT(request->sid))) return 0;
    return 1;
}
const char *wtop_provider_state_name(wtop_provider_state state)
{
    static const char *names[] = { "valid", "warming_up", "unsupported", "denied",
        "unavailable", "error", "timeout", "cancelled", "capacity", "protocol_error",
        "identity_changed", "identity_unavailable", "retirement_pending", "admission_closed" };
    return state >= WTOP_PROVIDER_VALID && state <= WTOP_PROVIDER_ADMISSION_CLOSED ? names[state] : "error";
}
void wtop_provider_session_init(wtop_provider_session *session)
{
    if(session) memset(session, 0, sizeof(*session));
}
static void release_exited(wtop_provider_session *session)
{
    if(session->pipe) CloseHandle(session->pipe);
    if(session->process) CloseHandle(session->process);
    if(session->job) CloseHandle(session->job);
    session->pipe = session->process = session->job = NULL;
    session->job_assigned = 0;
}
int wtop_provider_session_close(wtop_provider_session *session, DWORD grace_ms)
{
    if(!session) return 0;
    if(!session->process) { release_exited(session); return 1; }
    if(WaitForSingleObject(session->process, 0) != WAIT_OBJECT_0) {
#ifdef WTOP_PROVIDER_TESTING
        if(session->test_defer_retirement) { session->admission_closed = 1; return 0; }
#endif
        /* An unassigned child has never been resumed. Never run an optional
           provider outside the owned job because assignment was unavailable. */
        if(session->job_assigned) TerminateJobObject(session->job, 124);
        else TerminateProcess(session->process, 124);
        if(WaitForSingleObject(session->process, grace_ms) != WAIT_OBJECT_0) {
            session->admission_closed = 1;
            return 0;
        }
    }
    release_exited(session);
    return 1;
}
int wtop_provider_session_rearm(wtop_provider_session *session)
{
    if(!session) return 0;
    if(session->process && WaitForSingleObject(session->process, 0) != WAIT_OBJECT_0) return 0;
    release_exited(session);
    session->admission_closed = 0;
    return 1;
}
static void failure(wtop_provider_result *result, wtop_provider_state state,
                    wtop_provider_error_domain domain, DWORD error)
{
    result->state = state; result->error_domain = domain; result->native_error = error;
}
static void win32_failure(wtop_provider_result *result, DWORD error)
{
    failure(result, error == ERROR_ACCESS_DENIED ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_ERROR,
        WTOP_PROVIDER_ERROR_WIN32, error);
}
static int write_exact(HANDLE pipe, const void *buffer, DWORD count)
{
    const BYTE *cursor = buffer;
    while(count) {
        DWORD written = 0;
        if(!WriteFile(pipe, cursor, count, &written, NULL)) return 0;
        if(!written) { SetLastError(ERROR_WRITE_FAULT); return 0; }
        cursor += written; count -= written;
    }
    return 1;
}
static int read_exact(HANDLE pipe, void *buffer, DWORD count)
{
    BYTE *cursor = buffer;
    while(count) {
        DWORD got = 0;
        if(!ReadFile(pipe, cursor, count, &got, NULL)) return 0;
        if(!got) { SetLastError(ERROR_BROKEN_PIPE); return 0; }
        cursor += got; count -= got;
    }
    return 1;
}

/* This is synchronous polling of a documented nonblocking byte pipe, not
   overlapped I/O: no callbacks or pending buffers survive collect(). */
static int make_response_pipe(wtop_provider_session *session, HANDLE *child_write,
                              SECURITY_ATTRIBUTES *security)
{
    WCHAR name[160];
    DWORD error;
    _snwprintf_s(name, ARRAY_COUNT(name), _TRUNCATE,
        L"\\\\.\\pipe\\wtop-%lu-%llu-%lu", GetCurrentProcessId(),
        (unsigned long long)GetTickCount64(), session->sequence);
    session->pipe = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, NULL);
    if(session->pipe == INVALID_HANDLE_VALUE) { session->pipe = NULL; return 0; }
    if(!ConnectNamedPipe(session->pipe, NULL)) {
        error = GetLastError();
        if(error != ERROR_PIPE_LISTENING) return 0;
    }
    *child_write = CreateFileW(name, GENERIC_WRITE, 0, security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(*child_write == INVALID_HANDLE_VALUE) { *child_write = NULL; return 0; }
    if(ConnectNamedPipe(session->pipe, NULL)) return 1;
    return GetLastError() == ERROR_PIPE_CONNECTED;
}

/* Only the two protocol pipe handles are inheritable in this child. The
   explicit executable path is separate from the quoted command line; no shell,
   PATH search, command-line user data or credentials enter process creation. */
static int launch_worker(wtop_provider_session *session, const request_packet *packet,
                         wtop_provider_result *result)
{
    WCHAR *path = NULL, *command = NULL;
    HANDLE child_read = NULL, parent_write = NULL, child_write = NULL;
    SECURITY_ATTRIBUTES security = { sizeof(security), NULL, TRUE };
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SIZE_T attribute_bytes = 0;
    HANDLE allowed[2];
    DWORD error = ERROR_NOT_ENOUGH_MEMORY, written;
    int attributes_initialized = 0, ok = 0;
    memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    path = calloc(32768, sizeof(*path)); command = calloc(32768, sizeof(*command));
    if(!path || !command) goto done;
    written = GetModuleFileNameW(NULL, path, 32768);
    if(!written || written >= 32600) { error = written ? ERROR_BUFFER_OVERFLOW : GetLastError(); goto done; }
    if(!CreatePipe(&child_read, &parent_write, &security, REQUEST_PIPE_BYTES) ||
       !SetHandleInformation(parent_write, HANDLE_FLAG_INHERIT, 0) ||
       !make_response_pipe(session, &child_write, &security)) { error = GetLastError(); goto done; }
    session->job = CreateJobObjectW(NULL, NULL);
    if(!session->job) { error = GetLastError(); goto done; }
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.ProcessMemoryLimit = (SIZE_T)256 * 1024 * 1024;
    if(!SetInformationJobObject(session->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { error = GetLastError(); goto done; }
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    if(!attribute_bytes || attribute_bytes > 65536) { error = ERROR_INVALID_DATA; goto done; }
    startup.lpAttributeList = malloc(attribute_bytes);
    if(!startup.lpAttributeList) goto done;
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes)) { error = GetLastError(); goto done; }
    attributes_initialized = 1;
    allowed[0] = child_read; allowed[1] = child_write;
    if(!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        allowed, sizeof(allowed), NULL, NULL)) { error = GetLastError(); goto done; }
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_read;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = child_write;
    if(_snwprintf_s(command, 32768, _TRUNCATE, L"\"%s\" --wtop-provider-v1 %llu %llu", path,
        (unsigned long long)(ULONG_PTR)child_read, (unsigned long long)(ULONG_PTR)child_write) < 0) { error = ERROR_BUFFER_OVERFLOW; goto done; }
    if(!CreateProcessW(path, command, NULL, NULL, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL, &startup.StartupInfo, &process)) { error = GetLastError(); goto done; }
    session->process = process.hProcess;
    if(!AssignProcessToJobObject(session->job, session->process)) {
        error = GetLastError();
        /* No resume on failure, including Windows 7 nested-job constraints. */
        failure(result, WTOP_PROVIDER_UNSUPPORTED, WTOP_PROVIDER_ERROR_WIN32, error);
        goto done;
    }
    session->job_assigned = 1;
    /* Bounded request fits wholly in the explicitly sized empty request pipe. */
    if(!write_exact(parent_write, packet, sizeof(*packet))) { error = GetLastError(); goto done; }
    CloseHandle(parent_write); parent_write = NULL;
    if(ResumeThread(process.hThread) == (DWORD)-1) { error = GetLastError(); goto done; }
    ok = 1;
done:
    if(process.hThread) CloseHandle(process.hThread);
    if(child_read) CloseHandle(child_read);
    if(child_write) CloseHandle(child_write);
    if(parent_write) CloseHandle(parent_write);
    if(attributes_initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList); free(path); free(command);
    if(!ok) {
        if(result->state != WTOP_PROVIDER_UNSUPPORTED) win32_failure(result, error);
        result->retirement_confirmed = wtop_provider_session_close(session, RETIRE_GRACE_MS);
    }
    return ok;
}

static int valid_response(const response_packet *packet, const request_packet *request)
{
    const wtop_provider_result *result = &packet->result;
    DWORD i;
    if(packet->magic != WIRE_MAGIC || packet->version != WIRE_VERSION ||
       packet->bytes != sizeof(*packet) || packet->sequence != request->sequence ||
       packet->checksum != checksum_bytes(result, sizeof(*result)) ||
       result->kind != request->request.kind || result->pid != request->request.pid ||
       result->creation.dwHighDateTime != request->request.creation.dwHighDateTime ||
       result->creation.dwLowDateTime != request->request.creation.dwLowDateTime ||
       result->retirement_confirmed || result->response_received ||
       result->state < WTOP_PROVIDER_VALID || result->state > WTOP_PROVIDER_ADMISSION_CLOSED ||
       result->error_domain < WTOP_PROVIDER_ERROR_NONE || result->error_domain > WTOP_PROVIDER_ERROR_EXIT_CODE ||
       result->started_ms > result->observed_ms || result->count > result->total_count || !(result->elapsed_ms >= 0) || result->elapsed_ms > DBL_MAX ||
       !(result->acquisition_ms >= 0) || result->acquisition_ms > DBL_MAX) return 0;
    if(result->kind == WTOP_PROVIDER_COMMAND_LINE &&
       wide_length(result->data.command_line, WTOP_PROVIDER_COMMAND_CHARS) == WTOP_PROVIDER_COMMAND_CHARS) return 0;
    if(result->kind == WTOP_PROVIDER_SERVICES) {
        if(result->count > WTOP_PROVIDER_MAX_SERVICES) return 0;
        for(i = 0; i < result->count; ++i) {
            const wtop_provider_service *row = &result->data.services[i];
            if(wide_length(row->name, WTOP_PROVIDER_NAME_CHARS) == WTOP_PROVIDER_NAME_CHARS ||
               wide_length(row->display_name, WTOP_PROVIDER_NAME_CHARS) == WTOP_PROVIDER_NAME_CHARS ||
               row->identity_state < WTOP_PROVIDER_VALID || row->identity_state > WTOP_PROVIDER_ADMISSION_CLOSED) return 0;
        }
    }
    if(result->kind == WTOP_PROVIDER_PER_CORE) {
        if(result->count > WTOP_PROVIDER_MAX_CORES) return 0;
        for(i = 0; i < result->count; ++i) {
            const wtop_provider_core *row = &result->data.cores[i];
            if(wide_length(row->instance, ARRAY_COUNT(row->instance)) == ARRAY_COUNT(row->instance) ||
               row->state < WTOP_PROVIDER_VALID || row->state > WTOP_PROVIDER_ADMISSION_CLOSED ||
               (row->state == WTOP_PROVIDER_VALID && (!(row->busy_percent >= 0) || row->busy_percent > DBL_MAX))) return 0;
        }
    }
    if(result->kind == WTOP_PROVIDER_ACCOUNT &&
       (wide_length(result->data.account.name, ARRAY_COUNT(result->data.account.name)) == ARRAY_COUNT(result->data.account.name) ||
        wide_length(result->data.account.domain, ARRAY_COUNT(result->data.account.domain)) == ARRAY_COUNT(result->data.account.domain) ||
        wide_length(result->data.account.sid, ARRAY_COUNT(result->data.account.sid)) == ARRAY_COUNT(result->data.account.sid))) return 0;
    return 1;
}

wtop_provider_state wtop_provider_collect(wtop_provider_session *session,
    const wtop_provider_request *request, DWORD timeout_ms, HANDLE cancel_event,
    wtop_provider_result *result)
{
    request_packet packet;
    response_packet *response;
    ULONGLONG started;
    DWORD received = 0;
    int response_validated = 0;
    wtop_provider_state state;
    if(!result) return WTOP_PROVIDER_ERROR;
    memset(result, 0, sizeof(*result)); result->state = WTOP_PROVIDER_ERROR;
    result->retirement_confirmed = 1;
    if(request) { result->kind = request->kind; result->pid = request->pid; result->creation = request->creation; }
    if(!session || !valid_request(request) || !timeout_ms || timeout_ms > WTOP_PROVIDER_MAX_TIMEOUT_MS) {
        failure(result, WTOP_PROVIDER_CAPACITY, WTOP_PROVIDER_ERROR_WIN32, ERROR_INVALID_PARAMETER); return result->state;
    }
    if(session->process || session->admission_closed) {
        result->retirement_confirmed = session->process == NULL;
        return result->state = session->process ? WTOP_PROVIDER_RETIREMENT_PENDING : WTOP_PROVIDER_ADMISSION_CLOSED;
    }
    if(cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) return result->state = WTOP_PROVIDER_CANCELLED;
    response = calloc(1, sizeof(*response));
    if(!response) { win32_failure(result, ERROR_NOT_ENOUGH_MEMORY); return result->state; }
    memset(&packet, 0, sizeof(packet));
    packet.magic = WIRE_MAGIC; packet.version = WIRE_VERSION; packet.bytes = sizeof(packet);
    packet.sequence = ++session->sequence; packet.request = *request;
    started = GetTickCount64();
    if(!launch_worker(session, &packet, result)) { free(response); return result->state; }
#ifdef WTOP_PROVIDER_TESTING
    if(request->kind == WTOP_PROVIDER_TEST_RETIRE_PENDING) session->test_defer_retirement = 1;
#endif
    for(;;) {
        DWORD got = 0, pipe_error = ERROR_SUCCESS;
        DWORD remaining = (DWORD)sizeof(*response) - received;
        BYTE extra;
        BOOL pipe_ok = ReadFile(session->pipe, remaining ? (BYTE *)response + received : &extra,
            remaining ? remaining : 1, &got, NULL);
        if(!pipe_ok) pipe_error = GetLastError();
        if(!pipe_ok && pipe_error != ERROR_NO_DATA && pipe_error != ERROR_BROKEN_PIPE) {
            win32_failure(result, pipe_error); break;
        }
        if(got && !remaining) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
        received += got;
        if(received == sizeof(*response) && !response_validated) {
            if(!valid_response(response, &packet)) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
            response_validated = 1;
            result->response_received = 1;
        }
        if(cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) { result->state = WTOP_PROVIDER_CANCELLED; break; }
        if(WaitForSingleObject(session->process, 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            if(!GetExitCodeProcess(session->process, &exit_code) || exit_code) {
                failure(result, WTOP_PROVIDER_ERROR, WTOP_PROVIDER_ERROR_EXIT_CODE, exit_code ? exit_code : GetLastError()); break;
            }
            /* Once exit is signalled, every byte is either already available
               or absent. A final nonblocking read closes the read/exit race. */
            remaining = (DWORD)sizeof(*response) - received;
            got = 0;
            pipe_ok = ReadFile(session->pipe, remaining ? (BYTE *)response + received : &extra,
                remaining ? remaining : 1, &got, NULL);
            if(got && !remaining) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
            received += got;
            if(received != sizeof(*response)) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
            got = 0;
            if(ReadFile(session->pipe, &extra, 1, &got, NULL) && got) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
            if(!valid_response(response, &packet)) { result->state = WTOP_PROVIDER_PROTOCOL; break; }
            *result = response->result;
            result->retirement_confirmed = 1;
            result->response_received = 1;
            release_exited(session);
            free(response);
            return result->state;
        }
        if(GetTickCount64() - started >= timeout_ms) { result->state = WTOP_PROVIDER_TIMEOUT; break; }
        if(got) continue; /* Bounded reply bytes are available; drain without an artificial delay. */
        if(cancel_event) WaitForSingleObject(cancel_event, 5);
        else WaitForSingleObject(session->process, 5);
    }
    state = result->state;
    result->retirement_confirmed = wtop_provider_session_close(session, RETIRE_GRACE_MS);
    result->state = state;
    free(response);
    return result->state;
}

static int same_time(FILETIME a, FILETIME b)
{
    return a.dwHighDateTime == b.dwHighDateTime && a.dwLowDateTime == b.dwLowDateTime;
}
/* SYNCHRONIZE is required for inspection identity, so an exited old handle
   cannot authenticate information WMI later associates with a recycled PID. */
static HANDLE open_identity(DWORD pid, FILETIME expected, wtop_provider_result *result)
{
    HANDLE process;
    FILETIME created, exited, kernel, user;
    if(!pid || (!expected.dwHighDateTime && !expected.dwLowDateTime)) {
        result->state = WTOP_PROVIDER_IDENTITY_UNAVAILABLE; return NULL;
    }
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if(!process) { win32_failure(result, GetLastError()); return NULL; }
    if(!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        win32_failure(result, GetLastError()); CloseHandle(process); return NULL;
    }
    if(!same_time(created, expected) || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        result->state = WTOP_PROVIDER_IDENTITY_CHANGED; CloseHandle(process); return NULL;
    }
    return process;
}
static int revalidate_identity(HANDLE process, FILETIME expected)
{
    FILETIME created, exited, kernel, user;
    return GetProcessTimes(process, &created, &exited, &kernel, &user) && same_time(created, expected) &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}
static void hresult_failure(wtop_provider_result *result, HRESULT hr)
{
    wtop_provider_state state = WTOP_PROVIDER_ERROR;
    if(hr == E_ACCESSDENIED || hr == (HRESULT)WBEM_E_ACCESS_DENIED) state = WTOP_PROVIDER_DENIED;
    else if(hr == REGDB_E_CLASSNOTREG || hr == (HRESULT)WBEM_E_INVALID_NAMESPACE || hr == (HRESULT)WBEM_E_INVALID_CLASS) state = WTOP_PROVIDER_UNSUPPORTED;
    else if(hr == WBEM_S_TIMEDOUT) state = WTOP_PROVIDER_TIMEOUT;
    failure(result, state, WTOP_PROVIDER_ERROR_HRESULT, (DWORD)hr);
}

/* WMI's COM DLLs are optional too; an absent component must not prevent
   ordinary TUI startup or a process-only snapshot from reaching capability checks. */
typedef struct com_api {
    HMODULE ole, automation;
    HRESULT (WINAPI *initialize)(LPVOID, DWORD);
    HRESULT (WINAPI *security)(PSECURITY_DESCRIPTOR, LONG, SOLE_AUTHENTICATION_SERVICE *, void *, DWORD, DWORD, void *, DWORD, void *);
    HRESULT (WINAPI *create)(const CLSID *, IUnknown *, DWORD, const IID *, void **);
    HRESULT (WINAPI *blanket)(IUnknown *, DWORD, DWORD, OLECHAR *, DWORD, DWORD, RPC_AUTH_IDENTITY_HANDLE, DWORD);
    void (WINAPI *uninitialize)(void);
    BSTR (WINAPI *string)(const OLECHAR *);
    void (WINAPI *free_string)(BSTR);
    UINT (WINAPI *string_length)(BSTR);
    HRESULT (WINAPI *clear_variant)(VARIANTARG *);
} com_api;
static HMODULE load_system_library(const WCHAR *name)
{
    WCHAR path[MAX_PATH + 32];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if(!length || length >= MAX_PATH) return NULL;
    path[length++] = L'\\';
    if(!copy_wide(path + length, ARRAY_COUNT(path) - length, name)) return NULL;
    return LoadLibraryW(path);
}
static int load_com(com_api *api)
{
    memset(api, 0, sizeof(*api));
    api->ole = load_system_library(L"ole32.dll");
    api->automation = load_system_library(L"oleaut32.dll");
    if(!api->ole || !api->automation) goto fail;
#define COM_RESOLVE(field, module, symbol) do { FARPROC address = GetProcAddress(module, symbol); memcpy(&api->field, &address, sizeof(api->field)); } while(0)
    COM_RESOLVE(initialize, api->ole, "CoInitializeEx");
    COM_RESOLVE(security, api->ole, "CoInitializeSecurity");
    COM_RESOLVE(create, api->ole, "CoCreateInstance");
    COM_RESOLVE(blanket, api->ole, "CoSetProxyBlanket");
    COM_RESOLVE(uninitialize, api->ole, "CoUninitialize");
    COM_RESOLVE(string, api->automation, "SysAllocString");
    COM_RESOLVE(free_string, api->automation, "SysFreeString");
    COM_RESOLVE(string_length, api->automation, "SysStringLen");
    COM_RESOLVE(clear_variant, api->automation, "VariantClear");
#undef COM_RESOLVE
    if(api->initialize && api->security && api->create && api->blanket && api->uninitialize &&
       api->string && api->free_string && api->string_length && api->clear_variant) return 1;
fail:
    if(api->ole) FreeLibrary(api->ole);
    if(api->automation) FreeLibrary(api->automation);
    memset(api, 0, sizeof(*api));
    return 0;
}

static void command_line_provider(const wtop_provider_request *request, wtop_provider_result *result)
{
    com_api api;
    HANDLE process = open_identity(request->pid, request->creation, result);
    IWbemLocator *locator = NULL;
    IWbemServices *services = NULL;
    IEnumWbemClassObject *enumerator = NULL;
    IWbemClassObject *object = NULL;
    BSTR namespace_name = NULL, language = NULL, query = NULL;
    WCHAR query_text[128];
    VARIANT value;
    HRESULT hr;
    ULONG returned = 0;
    int initialized = 0;
    if(!process) return;
    if(!load_com(&api)) { CloseHandle(process); failure(result, WTOP_PROVIDER_UNSUPPORTED, WTOP_PROVIDER_ERROR_WIN32, ERROR_MOD_NOT_FOUND); return; }
    memset(&value, 0, sizeof(value));
    hr = api.initialize(NULL, COINIT_MULTITHREADED);
    if(FAILED(hr)) goto failed;
    initialized = 1;
    hr = api.security(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if(FAILED(hr) && hr != RPC_E_TOO_LATE) goto failed;
    hr = api.create(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&locator);
    if(FAILED(hr)) goto failed;
    namespace_name = api.string(L"ROOT\\CIMV2"); language = api.string(L"WQL");
    _snwprintf_s(query_text, ARRAY_COUNT(query_text), _TRUNCATE,
        L"SELECT CommandLine FROM Win32_Process WHERE ProcessId=%lu", request->pid);
    query = api.string(query_text);
    if(!namespace_name || !language || !query) { hr = E_OUTOFMEMORY; goto failed; }
    hr = IWbemLocator_ConnectServer(locator, namespace_name, NULL, NULL, NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT, NULL, NULL, &services);
    if(FAILED(hr)) goto failed;
    hr = api.blanket((IUnknown *)services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if(FAILED(hr)) goto failed;
    hr = IWbemServices_ExecQuery(services, language, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &enumerator);
    if(FAILED(hr)) goto failed;
    hr = IEnumWbemClassObject_Next(enumerator, 1000, 1, &object, &returned);
    result->observed_ms = GetTickCount64();
    if(hr == WBEM_S_TIMEDOUT || FAILED(hr)) goto failed;
    if(!returned || !object) { result->state = WTOP_PROVIDER_UNAVAILABLE; goto done; }
    hr = IWbemClassObject_Get(object, L"CommandLine", 0, &value, NULL, NULL);
    if(FAILED(hr)) goto failed;
    if(V_VT(&value) != VT_BSTR || !V_BSTR(&value)) { result->state = WTOP_PROVIDER_UNAVAILABLE; goto done; }
    if(api.string_length(V_BSTR(&value)) >= WTOP_PROVIDER_COMMAND_CHARS) { result->state = WTOP_PROVIDER_CAPACITY; goto done; }
    if(wide_length(V_BSTR(&value), (size_t)api.string_length(V_BSTR(&value)) + 1) != api.string_length(V_BSTR(&value))) {
        result->state = WTOP_PROVIDER_PROTOCOL; goto done;
    }
    copy_wide(result->data.command_line, WTOP_PROVIDER_COMMAND_CHARS, V_BSTR(&value));
    result->state = WTOP_PROVIDER_VALID;
    goto done;
failed:
    hresult_failure(result, hr);
done:
    if(!revalidate_identity(process, request->creation)) {
        result->state = WTOP_PROVIDER_IDENTITY_CHANGED;
        result->data.command_line[0] = 0;
    }
    api.clear_variant(&value);
    if(object) IWbemClassObject_Release(object);
    if(enumerator) IEnumWbemClassObject_Release(enumerator);
    if(services) IWbemServices_Release(services);
    if(locator) IWbemLocator_Release(locator);
    api.free_string(namespace_name); api.free_string(language); api.free_string(query);
    if(initialized) api.uninitialize();
    CloseHandle(process);
    FreeLibrary(api.ole); FreeLibrary(api.automation);
}

static void account_provider(const wtop_provider_request *request, wtop_provider_result *result)
{
    PSID sid = NULL;
    SID_NAME_USE use;
    DWORD name_chars = ARRAY_COUNT(result->data.account.name);
    DWORD domain_chars = ARRAY_COUNT(result->data.account.domain);
    copy_wide(result->data.account.sid, ARRAY_COUNT(result->data.account.sid), request->sid);
    if(!ConvertStringSidToSidW(request->sid, &sid)) { win32_failure(result, GetLastError()); return; }
    if(!IsValidSid(sid)) { win32_failure(result, ERROR_INVALID_SID); LocalFree(sid); return; }
    if(LookupAccountSidW(NULL, sid, result->data.account.name, &name_chars,
        result->data.account.domain, &domain_chars, &use)) result->state = WTOP_PROVIDER_VALID;
    else {
        DWORD error = GetLastError();
        if(error == ERROR_INSUFFICIENT_BUFFER) failure(result, WTOP_PROVIDER_CAPACITY, WTOP_PROVIDER_ERROR_WIN32, error);
        else if(error == ERROR_NONE_MAPPED) failure(result, WTOP_PROVIDER_UNAVAILABLE, WTOP_PROVIDER_ERROR_WIN32, error);
        else win32_failure(result, error);
        result->data.account.name[0] = result->data.account.domain[0] = 0;
    }
    result->observed_ms = GetTickCount64();
    LocalFree(sid);
}

static int service_pid_state(DWORD state)
{
    return state == SERVICE_RUNNING || state == SERVICE_PAUSE_PENDING ||
        state == SERVICE_PAUSED || state == SERVICE_CONTINUE_PENDING;
}
/* Open the lifetime before re-querying the service, then require it alive
   afterward. This authenticates the current association even when the initial
   enumeration raced a restart; no wall-clock ordering assumption is needed. */
static wtop_provider_state service_identity(SC_HANDLE manager, const WCHAR *service_name,
    DWORD pid, FILETIME *identity, DWORD *service_state)
{
    HANDLE process;
    SC_HANDLE service;
    FILETIME created, exited, kernel, user;
    SERVICE_STATUS_PROCESS current;
    DWORD bytes;
    wtop_provider_state state = WTOP_PROVIDER_IDENTITY_UNAVAILABLE;
    if(!pid) return WTOP_PROVIDER_UNAVAILABLE;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if(!process) return GetLastError() == ERROR_ACCESS_DENIED ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_IDENTITY_UNAVAILABLE;
    if(!GetProcessTimes(process, &created, &exited, &kernel, &user)) goto done;
    if(WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        state = WTOP_PROVIDER_IDENTITY_CHANGED; goto done;
    }
    service = OpenServiceW(manager, service_name, SERVICE_QUERY_STATUS);
    if(!service) { state = GetLastError() == ERROR_ACCESS_DENIED ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_UNAVAILABLE; goto done; }
    if(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&current, sizeof(current), &bytes)) {
        *service_state = current.dwCurrentState;
        if(!service_pid_state(current.dwCurrentState)) state = WTOP_PROVIDER_UNAVAILABLE;
        else if(current.dwProcessId == pid && revalidate_identity(process, created)) {
            *identity = created; *service_state = current.dwCurrentState; state = WTOP_PROVIDER_VALID;
        } else state = WTOP_PROVIDER_IDENTITY_CHANGED;
    } else state = GetLastError() == ERROR_ACCESS_DENIED ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_UNAVAILABLE;
    CloseServiceHandle(service);
done:
    CloseHandle(process);
    return state;
}

static void services_provider(const wtop_provider_request *request, wtop_provider_result *result)
{
    const DWORD capacity = 256UL * 1024UL;
    SC_HANDLE manager = NULL;
    HANDLE target = NULL;
    BYTE *buffer = NULL;
    DWORD bytes = 0, count = 0, resume = 0, error = ERROR_SUCCESS, i;
    BOOL ok;
    if(request->pid) { target = open_identity(request->pid, request->creation, result); if(!target) return; }
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if(!manager) { win32_failure(result, GetLastError()); goto done; }
    buffer = calloc(1, capacity);
    if(!buffer) { win32_failure(result, ERROR_NOT_ENOUGH_MEMORY); goto done; }
    ok = EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
        buffer, capacity, &bytes, &count, &resume, NULL);
    if(!ok) error = GetLastError();
    result->observed_ms = GetTickCount64();
    if(!ok && error != ERROR_MORE_DATA) { win32_failure(result, error); goto done; }
    if(count > capacity / sizeof(ENUM_SERVICE_STATUS_PROCESSW)) { result->state = WTOP_PROVIDER_PROTOCOL; goto done; }
    result->state = ok ? WTOP_PROVIDER_VALID : WTOP_PROVIDER_CAPACITY;
    for(i = 0; i < count; ++i) {
        const ENUM_SERVICE_STATUS_PROCESSW *entry = &((ENUM_SERVICE_STATUS_PROCESSW *)buffer)[i];
        wtop_provider_service *row;
        DWORD pid = service_pid_state(entry->ServiceStatusProcess.dwCurrentState) ? entry->ServiceStatusProcess.dwProcessId : 0;
        if(request->pid && pid != request->pid) continue;
        ++result->total_count;
        if(result->count == WTOP_PROVIDER_MAX_SERVICES) { result->state = WTOP_PROVIDER_CAPACITY; continue; }
        row = &result->data.services[result->count++];
        row->pid = pid;
        row->service_state = entry->ServiceStatusProcess.dwCurrentState;
        if(!copy_wide(row->name, ARRAY_COUNT(row->name), entry->lpServiceName) ||
           !copy_wide(row->display_name, ARRAY_COUNT(row->display_name), entry->lpDisplayName)) {
            result->state = row->identity_state = WTOP_PROVIDER_CAPACITY; continue;
        }
        row->identity_state = service_identity(manager, row->name, row->pid, &row->creation, &row->service_state);
        if(!service_pid_state(row->service_state)) row->pid = 0;
        if(request->pid && row->identity_state == WTOP_PROVIDER_VALID && !same_time(row->creation, request->creation))
            row->identity_state = WTOP_PROVIDER_IDENTITY_CHANGED;
    }
    if(target && !revalidate_identity(target, request->creation)) {
        result->state = WTOP_PROVIDER_IDENTITY_CHANGED;
        for(i = 0; i < result->count; ++i) result->data.services[i].identity_state = WTOP_PROVIDER_IDENTITY_CHANGED;
    }
done:
    free(buffer);
    if(manager) CloseServiceHandle(manager);
    if(target) CloseHandle(target);
}

/* Optional PDH remains optional at the loader boundary. English paths are
   localized then expanded, following PdhAddEnglishCounterW's documented
   wildcard procedure; each concrete instance receives its own handle. */
typedef PDH_STATUS (WINAPI *pdh_open_fn)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
typedef PDH_STATUS (WINAPI *pdh_add_fn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
typedef PDH_STATUS (WINAPI *pdh_info_fn)(PDH_HCOUNTER, BOOLEAN, LPDWORD, PPDH_COUNTER_INFO_W);
typedef PDH_STATUS (WINAPI *pdh_expand_fn)(LPCWSTR, LPCWSTR, LPWSTR, LPDWORD, DWORD);
typedef PDH_STATUS (WINAPI *pdh_parse_fn)(LPCWSTR, PPDH_COUNTER_PATH_ELEMENTS_W, LPDWORD, DWORD);
typedef PDH_STATUS (WINAPI *pdh_collect_fn)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *pdh_value_fn)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS (WINAPI *pdh_remove_fn)(PDH_HCOUNTER);
typedef PDH_STATUS (WINAPI *pdh_close_fn)(PDH_HQUERY);
typedef struct pdh_api {
    HMODULE module;
    pdh_open_fn open;
    pdh_add_fn english, add;
    pdh_info_fn info;
    pdh_expand_fn expand;
    pdh_parse_fn parse;
    pdh_collect_fn collect;
    pdh_value_fn value;
    pdh_remove_fn remove;
    pdh_close_fn close;
} pdh_api;
static int load_pdh(pdh_api *api)
{
    memset(api, 0, sizeof(*api));
    api->module = load_system_library(L"pdh.dll");
    if(!api->module) return 0;
#define PDH_RESOLVE(field, type, symbol) api->field = (type)GetProcAddress(api->module, symbol)
    PDH_RESOLVE(open, pdh_open_fn, "PdhOpenQueryW");
    PDH_RESOLVE(english, pdh_add_fn, "PdhAddEnglishCounterW");
    PDH_RESOLVE(add, pdh_add_fn, "PdhAddCounterW");
    PDH_RESOLVE(info, pdh_info_fn, "PdhGetCounterInfoW");
    PDH_RESOLVE(expand, pdh_expand_fn, "PdhExpandWildCardPathW");
    PDH_RESOLVE(parse, pdh_parse_fn, "PdhParseCounterPathW");
    PDH_RESOLVE(collect, pdh_collect_fn, "PdhCollectQueryData");
    PDH_RESOLVE(value, pdh_value_fn, "PdhGetFormattedCounterValue");
    PDH_RESOLVE(remove, pdh_remove_fn, "PdhRemoveCounter");
    PDH_RESOLVE(close, pdh_close_fn, "PdhCloseQuery");
#undef PDH_RESOLVE
    if(api->open && api->english && api->add && api->info && api->expand &&
       api->parse && api->collect && api->value && api->remove && api->close) return 1;
    FreeLibrary(api->module); api->module = NULL;
    return 0;
}
static wtop_provider_state pdh_state(PDH_STATUS status)
{
    if(status == (PDH_STATUS)PDH_CSTATUS_NO_OBJECT || status == (PDH_STATUS)PDH_CSTATUS_NO_COUNTER ||
       status == (PDH_STATUS)PDH_FUNCTION_NOT_FOUND) return WTOP_PROVIDER_UNSUPPORTED;
    if(status == (PDH_STATUS)PDH_ACCESS_DENIED) return WTOP_PROVIDER_DENIED;
    if(status == (PDH_STATUS)PDH_CSTATUS_NO_INSTANCE) return WTOP_PROVIDER_UNAVAILABLE;
    if(status == (PDH_STATUS)PDH_NO_DATA || status == (PDH_STATUS)PDH_INVALID_DATA ||
       status == (PDH_STATUS)PDH_CSTATUS_INVALID_DATA) return WTOP_PROVIDER_WARMING_UP;
    if(status == (PDH_STATUS)PDH_MORE_DATA) return WTOP_PROVIDER_CAPACITY;
    return WTOP_PROVIDER_ERROR;
}
static void pdh_failure(wtop_provider_result *result, PDH_STATUS status)
{
    failure(result, pdh_state(status), WTOP_PROVIDER_ERROR_PDH, (DWORD)status);
}
static int decimal_part(const WCHAR *start, const WCHAR *end, DWORD *number)
{
    ULONGLONG value = 0;
    if(start == end) return 0;
    while(start != end) {
        if(*start < L'0' || *start > L'9') return 0;
        value = value * 10 + (*start++ - L'0');
        if(value > MAXDWORD) return 0;
    }
    *number = (DWORD)value;
    return 1;
}
static void parse_core_identity(wtop_provider_core *core, int legacy)
{
    const WCHAR *comma = wcschr(core->instance, L',');
    const WCHAR *end = core->instance + wcslen(core->instance);
    core->group = core->index = MAXDWORD;
    if(comma && !legacy) {
        if(!decimal_part(core->instance, comma, &core->group) || !decimal_part(comma + 1, end, &core->index))
            core->group = core->index = MAXDWORD;
    } else if(legacy) decimal_part(core->instance, end, &core->index);
}

static void core_provider(const wtop_provider_request *request, wtop_provider_result *result)
{
    pdh_api api;
    PDH_HQUERY query = NULL;
    PDH_HCOUNTER wildcard = NULL;
    PDH_HCOUNTER counters[WTOP_PROVIDER_MAX_CORES];
    PDH_COUNTER_INFO_W *info = NULL;
    WCHAR *paths = NULL;
    PDH_STATUS status;
    DWORD bytes = 0, chars = 0, i;
    size_t position;
    LARGE_INTEGER frequency = {0}, first_before = {0}, first_after = {0};
    LARGE_INTEGER last_before = {0}, last_after = {0};
    int timing = 0, capped = 0;
    memset(counters, 0, sizeof(counters));
    if(!load_pdh(&api)) { failure(result, WTOP_PROVIDER_UNSUPPORTED, WTOP_PROVIDER_ERROR_WIN32, GetLastError()); return; }
    status = api.open(NULL, 0, &query);
    if(status != ERROR_SUCCESS) goto failed;
    status = api.english(query, L"\\Processor Information(*)\\% Processor Time", 0, &wildcard);
    if(status == (PDH_STATUS)PDH_CSTATUS_NO_OBJECT || status == (PDH_STATUS)PDH_CSTATUS_NO_COUNTER) {
        result->legacy_processor_scope = 1;
        status = api.english(query, L"\\Processor(*)\\% Processor Time", 0, &wildcard);
    }
    if(status != ERROR_SUCCESS) goto failed;
    status = api.info(wildcard, FALSE, &bytes, NULL);
    if(status != (PDH_STATUS)PDH_MORE_DATA || !bytes || bytes > 65536) {
        if(bytes > 65536) status = (PDH_STATUS)PDH_MORE_DATA;
        goto failed;
    }
    info = calloc(1, bytes);
    if(!info) { win32_failure(result, ERROR_NOT_ENOUGH_MEMORY); goto done; }
    status = api.info(wildcard, FALSE, &bytes, info);
    if(status != ERROR_SUCCESS) goto failed;
    status = api.expand(NULL, info->szFullPath, NULL, &chars, PDH_REFRESHCOUNTERS);
    if(status != (PDH_STATUS)PDH_MORE_DATA || chars < 2 || chars > 65536) {
        if(chars > 65536) status = (PDH_STATUS)PDH_MORE_DATA;
        goto failed;
    }
    paths = calloc(chars, sizeof(*paths));
    if(!paths) { win32_failure(result, ERROR_NOT_ENOUGH_MEMORY); goto done; }
    status = api.expand(NULL, info->szFullPath, paths, &chars, PDH_REFRESHCOUNTERS);
    if(status != ERROR_SUCCESS) goto failed;
    api.remove(wildcard); wildcard = NULL;
    for(position = 0; position < chars && paths[position];) {
        size_t length = wide_length(paths + position, chars - position);
        PDH_COUNTER_PATH_ELEMENTS_W *elements;
        wtop_provider_core *row;
        DWORD element_bytes = 0;
        if(length >= chars - position) { result->state = WTOP_PROVIDER_PROTOCOL; goto done; }
        status = api.parse(paths + position, NULL, &element_bytes, 0);
        if(status != (PDH_STATUS)PDH_MORE_DATA || !element_bytes || element_bytes > 65536) goto failed;
        elements = calloc(1, element_bytes);
        if(!elements) { win32_failure(result, ERROR_NOT_ENOUGH_MEMORY); goto done; }
        status = api.parse(paths + position, elements, &element_bytes, 0);
        if(status != ERROR_SUCCESS) { free(elements); goto failed; }
        if(!elements->szInstanceName || wcsstr(elements->szInstanceName, L"_Total")) {
            free(elements); position += length + 1; continue;
        }
        ++result->total_count;
        if(result->count >= WTOP_PROVIDER_MAX_CORES) {
            capped = 1; free(elements); position += length + 1; continue;
        }
        row = &result->data.cores[result->count];
        row->state = WTOP_PROVIDER_WARMING_UP;
        if(!copy_wide(row->instance, ARRAY_COUNT(row->instance), elements->szInstanceName)) {
            free(elements); result->state = WTOP_PROVIDER_CAPACITY; goto done;
        }
        parse_core_identity(row, result->legacy_processor_scope);
        free(elements);
        status = api.add(query, paths + position, 0, &counters[result->count]);
        if(status != ERROR_SUCCESS) goto failed;
        ++result->count;
        position += length + 1;
    }
    if(!result->count) { result->state = WTOP_PROVIDER_UNSUPPORTED; goto done; }
    timing = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&first_before);
    status = api.collect(query);
    timing = QueryPerformanceCounter(&first_after) && timing;
    if(status != ERROR_SUCCESS) goto failed;
    Sleep(request->interval_ms < 1000 ? 1000 : request->interval_ms); /* Documented PDH rate minimum; child-owned. */
    timing = QueryPerformanceCounter(&last_before) && timing;
    status = api.collect(query);
    timing = QueryPerformanceCounter(&last_after) && timing;
    result->observed_ms = GetTickCount64();
    if(status != ERROR_SUCCESS) goto failed;
    if(timing && first_before.QuadPart >= 0 && first_after.QuadPart >= first_before.QuadPart &&
       last_before.QuadPart >= first_after.QuadPart && last_after.QuadPart >= last_before.QuadPart) {
        LONGLONG first = first_before.QuadPart + (first_after.QuadPart - first_before.QuadPart) / 2;
        LONGLONG last = last_before.QuadPart + (last_after.QuadPart - last_before.QuadPart) / 2;
        result->elapsed_ms = (double)(last - first) / (double)frequency.QuadPart * 1000.0;
        result->acquisition_ms = ((double)(first_after.QuadPart - first_before.QuadPart) +
            (double)(last_after.QuadPart - last_before.QuadPart)) / (double)frequency.QuadPart * 1000.0;
    }
    if(!(result->elapsed_ms > 0)) { result->state = WTOP_PROVIDER_ERROR; goto done; }
    result->state = capped ? WTOP_PROVIDER_CAPACITY : WTOP_PROVIDER_VALID;
    for(i = 0; i < result->count; ++i) {
        PDH_FMT_COUNTERVALUE value;
        wtop_provider_core *row = &result->data.cores[i];
        memset(&value, 0, sizeof(value));
        status = api.value(counters[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, NULL, &value);
        if(status != ERROR_SUCCESS || (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA)) {
            row->native_error = status != ERROR_SUCCESS ? (DWORD)status : value.CStatus;
            row->state = pdh_state((PDH_STATUS)row->native_error);
        }
        else if(!(value.doubleValue >= 0) || value.doubleValue > DBL_MAX) row->state = WTOP_PROVIDER_ERROR;
        else { row->busy_percent = value.doubleValue; row->state = WTOP_PROVIDER_VALID; }
    }
    goto done;
failed:
    pdh_failure(result, status);
done:
    if(query) api.close(query);
    free(info); free(paths);
    FreeLibrary(api.module);
}

static int parse_handle(const WCHAR *text, HANDLE *handle)
{
    ULONGLONG value = 0;
    size_t i, length = wide_length(text, 32);
    if(!length || length == 32) return 0;
    for(i = 0; i < length; ++i) {
        unsigned digit;
        if(text[i] < L'0' || text[i] > L'9') return 0;
        digit = (unsigned)(text[i] - L'0');
        if(value > ((ULONGLONG)(ULONG_PTR)-1 - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    if(!value || (ULONG_PTR)value == (ULONG_PTR)INVALID_HANDLE_VALUE) return 0;
    *handle = (HANDLE)(ULONG_PTR)value;
    return GetFileType(*handle) == FILE_TYPE_PIPE;
}
int wtop_provider_worker_dispatch(int argc, WCHAR *const *argv, int *exit_code)
{
    HANDLE input = NULL, output = NULL;
    request_packet request;
    response_packet *response = NULL;
    int ok = 0;
    if(argc < 2 || wcscmp(argv[1], L"--wtop-provider-v1")) return 0;
    *exit_code = 2;
    if(argc != 4 || !parse_handle(argv[2], &input) || !parse_handle(argv[3], &output) || input == output) return 1;
    memset(&request, 0, sizeof(request));
    if(!read_exact(input, &request, sizeof(request)) || request.magic != WIRE_MAGIC ||
       request.version != WIRE_VERSION || request.bytes != sizeof(request) || !valid_request(&request.request)) goto done;
    response = calloc(1, sizeof(*response));
    if(!response) { *exit_code = 1; goto done; }
    response->magic = WIRE_MAGIC; response->version = WIRE_VERSION;
    response->bytes = sizeof(*response); response->sequence = request.sequence;
    response->result.kind = request.request.kind;
    response->result.pid = request.request.pid;
    response->result.creation = request.request.creation;
    response->result.state = WTOP_PROVIDER_ERROR;
    response->result.started_ms = GetTickCount64();
    switch(request.request.kind) {
    case WTOP_PROVIDER_COMMAND_LINE: command_line_provider(&request.request, &response->result); break;
    case WTOP_PROVIDER_SERVICES: services_provider(&request.request, &response->result); break;
    case WTOP_PROVIDER_PER_CORE: core_provider(&request.request, &response->result); break;
    case WTOP_PROVIDER_ACCOUNT: account_provider(&request.request, &response->result); break;
#ifdef WTOP_PROVIDER_TESTING
    case WTOP_PROVIDER_TEST_DELAY:
    case WTOP_PROVIDER_TEST_RETIRE_PENDING: Sleep(60000); response->result.state = WTOP_PROVIDER_VALID; break;
    case WTOP_PROVIDER_TEST_CRASH: ExitProcess(23); break;
    case WTOP_PROVIDER_TEST_VALID:
    case WTOP_PROVIDER_TEST_BAD_PROTOCOL:
    case WTOP_PROVIDER_TEST_PARTIAL:
    case WTOP_PROVIDER_TEST_EXTRA:
    case WTOP_PROVIDER_TEST_RESPONSE_THEN_BLOCK: response->result.state = WTOP_PROVIDER_VALID; break;
#endif
    default: response->result.state = WTOP_PROVIDER_UNSUPPORTED; break;
    }
    if(!response->result.observed_ms) response->result.observed_ms = GetTickCount64();
    response->checksum = checksum_bytes(&response->result, sizeof(response->result));
#ifdef WTOP_PROVIDER_TESTING
    if(request.request.kind == WTOP_PROVIDER_TEST_BAD_PROTOCOL) response->checksum ^= 1;
    if(request.request.kind == WTOP_PROVIDER_TEST_PARTIAL) {
        ok = write_exact(output, response, sizeof(*response) / 2); goto done;
    }
#endif
    ok = write_exact(output, response, sizeof(*response));
#ifdef WTOP_PROVIDER_TESTING
    if(request.request.kind == WTOP_PROVIDER_TEST_RESPONSE_THEN_BLOCK) Sleep(60000);
    if(request.request.kind == WTOP_PROVIDER_TEST_EXTRA) { const BYTE extra = 1; ok = ok && write_exact(output, &extra, 1); }
#endif
done:
    free(response);
    if(input) CloseHandle(input);
    if(output) CloseHandle(output);
    *exit_code = ok ? 0 : 1;
    return 1;
}
