/* Process detail, thread observations and identity-bound affinity actions.
 * SPDX-License-Identifier: GPL-3.0-only
 * Primary API contracts (reviewed 2026-11-18):
 * https://learn.microsoft.com/windows/win32/procthread/processor-groups
 * https://learn.microsoft.com/windows/win32/winprog64/processor-affinity
 * https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getprocessaffinitymask
 * https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask
 * https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes
 * https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessidofthread
 * A snapshot entry is an observation, not an authenticated target. Native
 * handles bind all subsequent reads/actions to one lifetime. Optional reads
 * run in the existing disposable provider job; no injected code is used.
 */
#include "features_nt.h"
#include <tlhelp32.h>
#include <string.h>

typedef BOOL (WINAPI *image_name_fn)(HANDLE, DWORD, LPWSTR, PDWORD);
typedef WORD (WINAPI *active_groups_fn)(void);
typedef DWORD (WINAPI *active_processors_fn)(WORD);
typedef BOOL (WINAPI *process_groups_fn)(HANDLE, PUSHORT, PUSHORT);
typedef DWORD (WINAPI *thread_process_fn)(HANDLE);

#define RESOLVE_FN(target, module, name) do { \
    FARPROC address = GetProcAddress(module, name); \
    memcpy(&(target), &address, sizeof(target)); \
} while(0)

static wtop_provider_status status_value(wtop_provider_state state, DWORD error)
{
    wtop_provider_status value;
    value.state = state;
    value.error_domain = error ? WTOP_PROVIDER_ERROR_WIN32 : WTOP_PROVIDER_ERROR_NONE;
    value.native_error = error;
    return value;
}
static wtop_provider_status win32_status(DWORD error)
{
    wtop_provider_state state = WTOP_PROVIDER_ERROR;
    if(error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
        state = WTOP_PROVIDER_DENIED;
    else if(error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND)
        state = WTOP_PROVIDER_UNAVAILABLE;
    return status_value(state, error);
}
static int time_equal(FILETIME left, FILETIME right)
{
    return left.dwLowDateTime == right.dwLowDateTime &&
        left.dwHighDateTime == right.dwHighDateTime;
}
static ULONGLONG time_units(FILETIME value)
{
    return ((ULONGLONG)value.dwHighDateTime << 32) | value.dwLowDateTime;
}
static int process_live(HANDLE process, FILETIME expected, wtop_provider_status *status)
{
    FILETIME created, exited, kernel, user;
    DWORD wait;
    if(!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        *status = win32_status(GetLastError()); return 0;
    }
    wait = WaitForSingleObject(process, 0);
    if(wait == WAIT_FAILED) { *status = win32_status(GetLastError()); return 0; }
    if(!time_equal(created, expected) || wait != WAIT_TIMEOUT) {
        *status = status_value(WTOP_PROVIDER_IDENTITY_CHANGED, ERROR_SUCCESS); return 0;
    }
    *status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS);
    return 1;
}
static HANDLE process_open(DWORD pid, FILETIME expected, DWORD access,
    wtop_provider_status *status)
{
    HANDLE process;
    if(!pid || !time_units(expected)) {
        *status = status_value(WTOP_PROVIDER_IDENTITY_UNAVAILABLE, ERROR_SUCCESS);
        return NULL;
    }
    process = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
        FALSE, pid);
    if(!process) { *status = win32_status(GetLastError()); return NULL; }
    if(!process_live(process, expected, status)) { CloseHandle(process); return NULL; }
    return process;
}
static void result_status(wtop_provider_result *result, wtop_provider_status status)
{
    result->state = status.state;
    result->error_domain = status.error_domain;
    result->native_error = status.native_error;
}

void wtop_feature_image_query(HANDLE process, WCHAR *path, DWORD capacity,
    wtop_provider_status *status)
{
    image_name_fn query = NULL;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    DWORD count = capacity;
    if(!path || !capacity || !status) return;
    path[0] = 0;
    if(kernel) RESOLVE_FN(query, kernel, "QueryFullProcessImageNameW");
    if(!query) { *status = status_value(WTOP_PROVIDER_UNSUPPORTED, ERROR_PROC_NOT_FOUND); return; }
    if(!query(process, 0, path, &count)) {
        DWORD error = GetLastError();
        path[0] = 0;
        *status = error == ERROR_INSUFFICIENT_BUFFER ?
            status_value(WTOP_PROVIDER_CAPACITY, error) : win32_status(error);
        return;
    }
    if(count >= capacity || path[count]) {
        path[0] = 0; *status = status_value(WTOP_PROVIDER_PROTOCOL, ERROR_INVALID_DATA); return;
    }
    *status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS);
}

const char *wtop_affinity_limit_name(wtop_affinity_limit limitation)
{
    switch(limitation) {
    case WTOP_AFFINITY_LIMIT_NONE: return "none";
    case WTOP_AFFINITY_LIMIT_MULTIPLE_GROUPS: return "multiple_processor_groups";
    case WTOP_AFFINITY_LIMIT_NATIVE_WIDTH: return "native_mask_width";
    case WTOP_AFFINITY_LIMIT_API: return "api_unavailable";
    default: return "unknown";
    }
}

void wtop_feature_affinity_query(HANDLE process, wtop_provider_affinity *affinity)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    active_groups_fn active_groups = NULL;
    active_processors_fn active_processors = NULL;
    process_groups_fn process_groups = NULL;
    DWORD_PTR process_mask = 0, system_mask = 0;
    SYSTEM_INFO native;
    USHORT groups[64], group_count = 64;
    DWORD processors;
    memset(affinity, 0, sizeof(*affinity));
    affinity->status = status_value(WTOP_PROVIDER_UNAVAILABLE, ERROR_SUCCESS);
    affinity->mask_bits = (DWORD)(sizeof(DWORD_PTR) * 8);
    affinity->group = MAXDWORD;
    if(kernel) {
        RESOLVE_FN(active_groups, kernel, "GetActiveProcessorGroupCount");
        RESOLVE_FN(active_processors, kernel, "GetActiveProcessorCount");
        RESOLVE_FN(process_groups, kernel, "GetProcessGroupAffinity");
    }
    GetNativeSystemInfo(&native);
    processors = native.dwNumberOfProcessors;
    affinity->system_group_count = active_groups ? active_groups() : 1;
    if(!affinity->system_group_count) {
        affinity->status = win32_status(GetLastError()); return;
    }
    if(process_groups) {
        if(!process_groups(process, &group_count, groups)) {
            DWORD error = GetLastError();
            affinity->process_group_count = group_count;
            if(error == ERROR_INSUFFICIENT_BUFFER) {
                affinity->limitation = WTOP_AFFINITY_LIMIT_MULTIPLE_GROUPS;
                affinity->status = status_value(WTOP_PROVIDER_UNSUPPORTED, error);
            } else affinity->status = win32_status(error);
            return;
        }
        affinity->process_group_count = group_count;
        if(group_count == 1) affinity->group = groups[0];
    } else if(affinity->system_group_count == 1) {
        affinity->process_group_count = 1; affinity->group = 0;
    }
    /* A primary-group mask is not a complete process affinity on current
       multi-group Windows. Do not expose it as complete or edit it. The scope
       rejection is conservative even for an explicitly single-group process. */
    if(affinity->system_group_count != 1 || affinity->process_group_count != 1) {
        affinity->limitation = WTOP_AFFINITY_LIMIT_MULTIPLE_GROUPS;
        affinity->status = status_value(WTOP_PROVIDER_UNSUPPORTED, ERROR_NOT_SUPPORTED);
        return;
    }
    if(active_processors) {
        processors = active_processors((WORD)affinity->group);
        if(!processors) { affinity->status = win32_status(GetLastError()); return; }
    }
    /* WOW64 folds high 32 bits into low 32 bits in affinity reads. Widening
       that returned DWORD_PTR would preserve the wrong mask, not recover it. */
    if(processors > affinity->mask_bits) {
        affinity->limitation = WTOP_AFFINITY_LIMIT_NATIVE_WIDTH;
        affinity->status = status_value(WTOP_PROVIDER_UNSUPPORTED, ERROR_NOT_SUPPORTED);
        return;
    }
    if(!GetProcessAffinityMask(process, &process_mask, &system_mask)) {
        affinity->status = win32_status(GetLastError()); return;
    }
    if(!process_mask || !system_mask || (process_mask & ~system_mask)) {
        affinity->status = status_value(WTOP_PROVIDER_UNAVAILABLE, ERROR_INVALID_DATA); return;
    }
    affinity->process_mask = (ULONGLONG)process_mask;
    affinity->system_mask = (ULONGLONG)system_mask;
    affinity->status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS);
}

int wtop_feature_affinity_mask_valid(ULONGLONG requested,
    const wtop_provider_affinity *affinity)
{
    if(!affinity || affinity->status.state != WTOP_PROVIDER_VALID || !requested ||
       affinity->limitation != WTOP_AFFINITY_LIMIT_NONE ||
       affinity->system_group_count != 1 || affinity->process_group_count != 1 ||
       (affinity->mask_bits != 32 && affinity->mask_bits != 64)) return 0;
    if(affinity->mask_bits == 32 && ((requested >> 32) || (affinity->system_mask >> 32))) return 0;
    return (requested & ~affinity->system_mask) == 0;
}

wtop_provider_state wtop_process_affinity_apply(DWORD pid, FILETIME creation,
    ULONGLONG requested_mask, int readonly, wtop_affinity_outcome *outcome)
{
    HANDLE process;
    if(!outcome) return WTOP_PROVIDER_ERROR;
    memset(outcome, 0, sizeof(*outcome));
    outcome->pid = pid; outcome->creation = creation;
    outcome->requested_mask = requested_mask;
    outcome->before.status = outcome->after.status =
        status_value(WTOP_PROVIDER_UNAVAILABLE, ERROR_SUCCESS);
    if(readonly) {
        outcome->readonly_blocked = 1;
        outcome->status = status_value(WTOP_PROVIDER_DENIED, ERROR_ACCESS_DENIED);
        return outcome->status.state;
    }
    process = process_open(pid, creation, PROCESS_SET_INFORMATION, &outcome->status);
    if(!process) return outcome->status.state;
    wtop_feature_affinity_query(process, &outcome->before);
    if(outcome->before.status.state != WTOP_PROVIDER_VALID) {
        outcome->status = outcome->before.status; goto done;
    }
    if(!wtop_feature_affinity_mask_valid(requested_mask, &outcome->before) ||
       (ULONGLONG)(DWORD_PTR)requested_mask != requested_mask) {
        outcome->status = status_value(WTOP_PROVIDER_ERROR, ERROR_INVALID_PARAMETER); goto done;
    }
    if(!process_live(process, creation, &outcome->status)) goto done;
    outcome->attempted = 1;
    if(!SetProcessAffinityMask(process, (DWORD_PTR)requested_mask)) {
        outcome->status = win32_status(GetLastError()); goto done;
    }
    outcome->applied = 1;
    /* Re-reading verifies an observation; another owner can change affinity
       immediately afterward. The original success is retained independently. */
    wtop_feature_affinity_query(process, &outcome->after);
    if(!process_live(process, creation, &outcome->status)) goto done;
    if(outcome->after.status.state != WTOP_PROVIDER_VALID) {
        outcome->status = outcome->after.status; goto done;
    }
    if(outcome->after.process_mask != requested_mask) {
        outcome->status = status_value(WTOP_PROVIDER_UNAVAILABLE, ERROR_RETRY); goto done;
    }
    outcome->verified = 1;
    outcome->status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS);
done:
    CloseHandle(process);
    return outcome->status.state;
}

static void collect_thread(DWORD pid, DWORD tid, thread_process_fn owner,
    wtop_provider_thread *row)
{
    HANDLE thread;
    FILETIME created, exited, kernel, user;
    DWORD owner_pid, wait;
    int priority;
    memset(row, 0, sizeof(*row));
    row->tid = tid; row->pid = pid;
    row->observed_ms = GetTickCount64();
    row->identity_status = row->times_status = row->priority_status =
        status_value(WTOP_PROVIDER_UNAVAILABLE, ERROR_SUCCESS);
    thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, tid);
    if(!thread) { row->identity_status = win32_status(GetLastError()); return; }
    owner_pid = owner(thread);
    if(!owner_pid) { row->identity_status = win32_status(GetLastError()); goto done; }
    if(owner_pid != pid) {
        row->identity_status = status_value(WTOP_PROVIDER_IDENTITY_CHANGED, ERROR_SUCCESS); goto done;
    }
    if(!GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
        row->identity_status = row->times_status = win32_status(GetLastError()); goto done;
    }
    row->creation = created;
    row->kernel_100ns = time_units(kernel); row->user_100ns = time_units(user);
    row->identity_status = row->times_status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS);
    priority = GetThreadPriority(thread);
    if(priority == THREAD_PRIORITY_ERROR_RETURN) row->priority_status = win32_status(GetLastError());
    else { row->priority = priority; row->priority_status = status_value(WTOP_PROVIDER_VALID, ERROR_SUCCESS); }
    wait = WaitForSingleObject(thread, 0);
    if(wait != WAIT_TIMEOUT) {
        row->identity_status = wait == WAIT_FAILED ? win32_status(GetLastError()) :
            status_value(WTOP_PROVIDER_IDENTITY_CHANGED, ERROR_SUCCESS);
        row->times_status = row->priority_status = row->identity_status;
        row->kernel_100ns = row->user_100ns = 0; row->priority = 0;
    }
done:
    row->observed_ms = GetTickCount64();
    CloseHandle(thread);
}

void wtop_feature_threads_collect(const wtop_provider_request *request,
    wtop_provider_result *result)
{
    wtop_provider_status status;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    thread_process_fn owner = NULL;
    HANDLE process = NULL, snapshot = INVALID_HANDLE_VALUE;
    THREADENTRY32 entry;
    DWORD scanned = 0, error, i;
    BOOL next;
    if(kernel) RESOLVE_FN(owner, kernel, "GetProcessIdOfThread");
    if(!owner) { result_status(result, status_value(WTOP_PROVIDER_UNSUPPORTED, ERROR_PROC_NOT_FOUND)); return; }
    process = process_open(request->pid, request->creation, 0, &status);
    if(!process) { result_status(result, status); return; }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if(snapshot == INVALID_HANDLE_VALUE) { result_status(result, win32_status(GetLastError())); goto done; }
    memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
    next = Thread32First(snapshot, &entry);
    if(!next) {
        error = GetLastError();
        if(error != ERROR_NO_MORE_FILES) { result_status(result, win32_status(error)); goto done; }
    }
    result->state = WTOP_PROVIDER_VALID;
    while(next) {
        if(++scanned > WTOP_PROVIDER_THREAD_SCAN_LIMIT) { result->state = WTOP_PROVIDER_CAPACITY; break; }
        if(entry.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID)) {
            result_status(result, status_value(WTOP_PROVIDER_PROTOCOL, ERROR_INVALID_DATA)); break;
        }
        if(entry.th32OwnerProcessID == request->pid) {
            ++result->total_count;
            if(result->count == WTOP_PROVIDER_MAX_THREADS) result->state = WTOP_PROVIDER_CAPACITY;
            else collect_thread(request->pid, entry.th32ThreadID, owner, &result->data.threads[result->count++]);
        }
        entry.dwSize = sizeof(entry);
        next = Thread32Next(snapshot, &entry);
        if(!next && (error = GetLastError()) != ERROR_NO_MORE_FILES) {
            result_status(result, win32_status(error)); break;
        }
    }
    if(!process_live(process, request->creation, &status)) {
        result_status(result, status);
        for(i = 0; i < result->count; ++i) {
            wtop_provider_thread *row = &result->data.threads[i];
            row->identity_status = row->times_status = row->priority_status = status;
            row->kernel_100ns = row->user_100ns = 0; row->priority = 0;
        }
    }
done:
    result->observed_ms = GetTickCount64();
    result->acquisition_ms = (double)(result->observed_ms - result->started_ms);
    if(snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    if(process) CloseHandle(process);
}
