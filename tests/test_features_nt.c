/* Real native feature fixtures operate only on owned children/threads.
   SPDX-License-Identifier: GPL-3.0-only */
#include "providers_nt.h"
#include "features_nt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sddl.h>

typedef struct owned_child {
    PROCESS_INFORMATION process;
    HANDLE job, ready, stop;
    FILETIME creation, thread_creation;
} owned_child;

static DWORD WINAPI wait_thread(void *context)
{
    WaitForSingleObject((HANDLE)context, 90000);
    return 0;
}
static DWORD WINAPI transient_thread(void *context)
{
    WaitForSingleObject((HANDLE)context, 1);
    return 0;
}
static DWORD WINAPI churn_thread(void *context)
{
    HANDLE stop = context;
    while(WaitForSingleObject(stop, 0) == WAIT_TIMEOUT) {
        HANDLE transient = CreateThread(NULL, 0, transient_thread, stop, 0, NULL);
        if(!transient) return 1;
        /* At most one transient thread is owned here. It can disappear between
           the collector's system snapshot and its later OpenThread call. */
        if(WaitForSingleObject(transient, 3000) != WAIT_OBJECT_0) { CloseHandle(transient); return 2; }
        CloseHandle(transient);
        if(WaitForSingleObject(stop, 1) != WAIT_TIMEOUT) break;
    }
    return 0;
}
static int child_main(const WCHAR *ready_name, const WCHAR *stop_name)
{
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name);
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, stop_name);
    HANDLE threads[4];
    DWORD i;
    if(!ready || !stop) return 3;
    for(i = 0; i < 3; ++i) {
        threads[i] = CreateThread(NULL, 0, wait_thread, stop, 0, NULL);
        if(!threads[i]) return 4;
    }
    threads[3] = CreateThread(NULL, 0, churn_thread, stop, 0, NULL);
    if(!threads[3]) return 4;
    SetEvent(ready);
    WaitForSingleObject(stop, 90000);
    for(i = 0; i < 4; ++i) {
        if(WaitForSingleObject(threads[i], 3000) != WAIT_OBJECT_0) return 5;
        CloseHandle(threads[i]);
    }
    CloseHandle(stop); CloseHandle(ready);
    return 0;
}
static void start_child(owned_child *child, const WCHAR *path, int deny_query)
{
    WCHAR ready_name[160], stop_name[160], *command;
    STARTUPINFOW startup;
    SECURITY_ATTRIBUTES attributes;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    FILETIME exited, kernel, user;
    static unsigned sequence;
    memset(child, 0, sizeof(*child));
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    child->job = CreateJobObjectW(NULL, NULL); assert(child->job);
    assert(SetInformationJobObject(child->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    ++sequence;
    _snwprintf_s(ready_name, 160, _TRUNCATE, L"Local\\wtop-feature-ready-%lu-%u", GetCurrentProcessId(), sequence);
    _snwprintf_s(stop_name, 160, _TRUNCATE, L"Local\\wtop-feature-stop-%lu-%u", GetCurrentProcessId(), sequence);
    child->ready = CreateEventW(NULL, TRUE, FALSE, ready_name); assert(child->ready);
    child->stop = CreateEventW(NULL, TRUE, FALSE, stop_name); assert(child->stop);
    command = calloc(32768, sizeof(*command)); assert(command);
    assert(_snwprintf_s(command, 32768, _TRUNCATE, L"\"%s\" --feature-child \"%s\" \"%s\"",
        path, ready_name, stop_name) > 0);
    memset(&attributes, 0, sizeof(attributes)); attributes.nLength = sizeof(attributes);
    if(deny_query) {
        /* Deny query and mutation to Everyone, allow the remaining ordinary
           rights. The already-returned creator handle remains owned for
           cleanup/identity. No unrelated target or administrator role is used. */
        assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(D;;0x00101400;;;WD)(A;;GA;;;WD)", SDDL_REVISION_1, &descriptor, NULL));
        attributes.lpSecurityDescriptor = descriptor;
    }
    assert(CreateProcessW(path, command, deny_query ? &attributes : NULL, NULL, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &startup, &child->process));
    assert(AssignProcessToJobObject(child->job, child->process.hProcess));
    assert(GetProcessTimes(child->process.hProcess, &child->creation, &exited, &kernel, &user));
    assert(GetThreadTimes(child->process.hThread, &child->thread_creation, &exited, &kernel, &user));
    assert(ResumeThread(child->process.hThread) != (DWORD)-1);
    assert(WaitForSingleObject(child->ready, 5000) == WAIT_OBJECT_0);
    LocalFree(descriptor); free(command);
}
static void stop_child(owned_child *child)
{
    DWORD exit_code;
    assert(SetEvent(child->stop));
    if(WaitForSingleObject(child->process.hProcess, 5000) != WAIT_OBJECT_0)
        assert(TerminateJobObject(child->job, 99));
    assert(WaitForSingleObject(child->process.hProcess, 3000) == WAIT_OBJECT_0);
    assert(GetExitCodeProcess(child->process.hProcess, &exit_code));
    assert(exit_code == 0);
    CloseHandle(child->process.hThread); CloseHandle(child->process.hProcess);
    CloseHandle(child->stop); CloseHandle(child->ready); CloseHandle(child->job);
    memset(child, 0, sizeof(*child));
}
static ULONGLONG filetime_units(FILETIME value)
{
    return ((ULONGLONG)value.dwHighDateTime << 32) | value.dwLowDateTime;
}
static wtop_provider_state collect(wtop_provider_session *session, const owned_child *child,
    wtop_provider_kind kind, DWORD flags, wtop_provider_result *result)
{
    wtop_provider_request request;
    wtop_provider_state state;
    memset(&request, 0, sizeof(request)); request.kind = kind;
    request.pid = child->process.dwProcessId; request.creation = child->creation;
    request.detail_flags = flags;
    state = wtop_provider_collect(session, &request, 10000, NULL, result);
    assert(result->retirement_confirmed);
    assert(!session->process && !session->job && !session->pipe);
    return state;
}
static void test_mask_boundaries(void)
{
    wtop_provider_affinity affinity;
    ULONGLONG high = (ULONGLONG)1 << 63;
    memset(&affinity, 0, sizeof(affinity));
    affinity.status.state = WTOP_PROVIDER_VALID;
    affinity.mask_bits = 64; affinity.system_group_count = affinity.process_group_count = 1;
    affinity.system_mask = high | 5;
    assert(wtop_feature_affinity_mask_valid(high, &affinity));
    assert(wtop_feature_affinity_mask_valid(high | 1, &affinity));
    assert(!wtop_feature_affinity_mask_valid(0, &affinity));
    assert(!wtop_feature_affinity_mask_valid(2, &affinity));
    affinity.mask_bits = 32;
    assert(!wtop_feature_affinity_mask_valid(high | 1, &affinity));
    assert(!wtop_feature_affinity_mask_valid(5, &affinity));
    affinity.system_mask = 5;
    assert(wtop_feature_affinity_mask_valid(5, &affinity));
    affinity.process_group_count = 2;
    assert(!wtop_feature_affinity_mask_valid(1, &affinity));
    affinity.process_group_count = 1; affinity.system_group_count = 2;
    assert(!wtop_feature_affinity_mask_valid(1, &affinity));
}
static void disable_debug_privilege(void)
{
    HANDLE token;
    TOKEN_PRIVILEGES privileges;
    assert(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token));
    memset(&privileges, 0, sizeof(privileges)); privileges.PrivilegeCount = 1;
    assert(LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &privileges.Privileges[0].Luid));
    /* An administrator's token might have this privilege enabled by a runner.
       Disable it explicitly so owned-child ACL denial tests the ordinary query
       contract rather than the unrelated debug-privilege bypass. */
    assert(AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL));
    CloseHandle(token);
}

static void test_threads(wtop_provider_session *session, owned_child *child,
    wtop_provider_result *result)
{
    DWORD i;
    FILETIME created, exited, kernel_before, user_before, kernel_after, user_after;
    int primary_found = 0;
    assert(GetThreadTimes(child->process.hThread, &created, &exited, &kernel_before, &user_before));
    assert(collect(session, child, WTOP_PROVIDER_THREADS, 0, result) == WTOP_PROVIDER_VALID);
    assert(GetThreadTimes(child->process.hThread, &created, &exited, &kernel_after, &user_after));
    assert(result->count >= 5 && result->count <= WTOP_PROVIDER_MAX_THREADS);
    for(i = 0; i < result->count; ++i) {
        const wtop_provider_thread *row = &result->data.threads[i];
        assert(row->pid == child->process.dwProcessId && row->tid);
        if(row->tid != child->process.dwThreadId) continue;
        primary_found = 1;
        assert(row->identity_status.state == WTOP_PROVIDER_VALID);
        assert(row->times_status.state == WTOP_PROVIDER_VALID);
        assert(row->priority_status.state == WTOP_PROVIDER_VALID);
        assert(filetime_units(row->creation) == filetime_units(child->thread_creation));
        assert(row->kernel_100ns >= filetime_units(kernel_before) && row->kernel_100ns <= filetime_units(kernel_after));
        assert(row->user_100ns >= filetime_units(user_before) && row->user_100ns <= filetime_units(user_after));
        assert(row->priority == GetThreadPriority(child->process.hThread));
        assert(row->observed_ms >= result->started_ms && row->observed_ms <= result->observed_ms);
    }
    assert(primary_found);
}
static void test_affinity(wtop_provider_session *session, owned_child *child,
    wtop_provider_result *result)
{
    wtop_affinity_outcome outcome;
    DWORD_PTR direct_process, direct_system;
    ULONGLONG original, highest, remaining;
    FILETIME wrong = child->creation;
    wtop_provider_state state;
    assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, 1, 1, &outcome) == WTOP_PROVIDER_DENIED);
    assert(outcome.readonly_blocked && !outcome.attempted && !outcome.applied);
    wrong.dwLowDateTime ^= 1;
    assert(wtop_process_affinity_apply(child->process.dwProcessId, wrong, 1, 0, &outcome) == WTOP_PROVIDER_IDENTITY_CHANGED);
    assert(!outcome.attempted && !outcome.applied);
    state = collect(session, child, WTOP_PROVIDER_AFFINITY, 0, result);
    printf("native affinity observation: %s, limitation=%s, width=%lu, system_groups=%lu\n",
        wtop_provider_state_name(state), wtop_affinity_limit_name(result->data.affinity.limitation),
        result->data.affinity.mask_bits, result->data.affinity.system_group_count);
    if(state == WTOP_PROVIDER_UNSUPPORTED) {
        assert(result->data.affinity.limitation != WTOP_AFFINITY_LIMIT_NONE);
        assert(!result->data.affinity.process_mask && !result->data.affinity.system_mask);
        assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, 1, 0, &outcome) == WTOP_PROVIDER_UNSUPPORTED);
        assert(!outcome.attempted && !outcome.applied);
        return;
    }
    assert(state == WTOP_PROVIDER_VALID);
    assert(GetProcessAffinityMask(child->process.hProcess, &direct_process, &direct_system));
    assert(result->data.affinity.process_mask == (ULONGLONG)direct_process);
    assert(result->data.affinity.system_mask == (ULONGLONG)direct_system);
    original = result->data.affinity.process_mask;
    highest = 1; remaining = original;
    while(remaining >>= 1) highest <<= 1;
    assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, highest, 0, &outcome) == WTOP_PROVIDER_VALID);
    assert(outcome.attempted && outcome.applied && outcome.verified && outcome.requested_mask == highest);
    assert(outcome.before.process_mask == original && outcome.after.process_mask == highest);
    assert(GetProcessAffinityMask(child->process.hProcess, &direct_process, &direct_system));
    assert((ULONGLONG)direct_process == highest);
    assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, 0, 0, &outcome) == WTOP_PROVIDER_ERROR);
    assert(!outcome.attempted && !outcome.applied);
    if(sizeof(DWORD_PTR) == 4) {
        assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, ((ULONGLONG)1 << 63) | highest, 0, &outcome) == WTOP_PROVIDER_ERROR);
        assert(!outcome.attempted && !outcome.applied);
    }
    assert(wtop_process_affinity_apply(child->process.dwProcessId, child->creation, original, 0, &outcome) == WTOP_PROVIDER_VALID);
    assert(GetProcessAffinityMask(child->process.hProcess, &direct_process, &direct_system));
    assert((ULONGLONG)direct_process == original);
}
static void test_identity_and_cancellation(wtop_provider_session *session, owned_child *child,
    wtop_provider_result *result)
{
    const wtop_provider_kind kinds[] = { WTOP_PROVIDER_PROCESS_DETAILS, WTOP_PROVIDER_THREADS, WTOP_PROVIDER_AFFINITY };
    wtop_provider_request request;
    HANDLE cancelled = CreateEventW(NULL, TRUE, TRUE, NULL);
    DWORD sequence;
    unsigned i;
    assert(cancelled);
    for(i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        memset(&request, 0, sizeof(request)); request.kind = kinds[i];
        request.pid = child->process.dwProcessId; request.creation = child->creation;
        if(kinds[i] == WTOP_PROVIDER_PROCESS_DETAILS) request.detail_flags = WTOP_DETAIL_IMAGE_PATH;
        request.creation.dwLowDateTime ^= 1;
        assert(wtop_provider_collect(session, &request, 5000, NULL, result) == WTOP_PROVIDER_IDENTITY_CHANGED);
        assert(!result->count && result->retirement_confirmed);
        memset(&request.creation, 0, sizeof(request.creation));
        assert(wtop_provider_collect(session, &request, 5000, NULL, result) == WTOP_PROVIDER_IDENTITY_UNAVAILABLE);
        assert(result->retirement_confirmed);
        request.creation = child->creation;
        sequence = session->sequence;
        assert(wtop_provider_collect(session, &request, 5000, cancelled, result) == WTOP_PROVIDER_CANCELLED);
        assert(session->sequence == sequence && result->retirement_confirmed);
        if(kinds[i] == WTOP_PROVIDER_PROCESS_DETAILS) {
            assert(result->data.details.image_status.state == WTOP_PROVIDER_UNAVAILABLE);
            assert(!result->data.details.image_path[0]);
        } else if(kinds[i] == WTOP_PROVIDER_AFFINITY) {
            assert(result->data.affinity.status.state == WTOP_PROVIDER_UNAVAILABLE);
            assert(!result->data.affinity.process_mask && !result->data.affinity.system_mask);
        }
    }
    request.kind = WTOP_PROVIDER_PROCESS_DETAILS; request.detail_flags = WTOP_DETAIL_ALL | 0x10UL;
    sequence = session->sequence;
    assert(wtop_provider_collect(session, &request, 5000, NULL, result) == WTOP_PROVIDER_CAPACITY);
    assert(sequence == session->sequence);
    request.kind = WTOP_PROVIDER_THREADS; request.detail_flags = WTOP_DETAIL_IMAGE_PATH;
    assert(wtop_provider_collect(session, &request, 5000, NULL, result) == WTOP_PROVIDER_CAPACITY);
    assert(sequence == session->sequence);
    CloseHandle(cancelled);
}
int wmain(int argc, WCHAR **argv)
{
    wtop_provider_session session;
    wtop_provider_result *result;
    wtop_provider_state state;
    wtop_affinity_outcome outcome;
    owned_child child, denied;
    WCHAR *path;
    DWORD i, before, after;
    int exit_code;
    if(wtop_provider_worker_dispatch(argc, argv, &exit_code)) return exit_code;
    if(argc == 4 && !wcscmp(argv[1], L"--feature-child")) return child_main(argv[2], argv[3]);
    disable_debug_privilege();
    test_mask_boundaries();
    path = calloc(32768, sizeof(*path)); result = calloc(1, sizeof(*result));
    assert(path && result);
    assert(GetModuleFileNameW(NULL, path, 32768) < 32768 && path[0]);
    wtop_provider_session_init(&session);
    start_child(&child, path, 0);
    assert(collect(&session, &child, WTOP_PROVIDER_PROCESS_DETAILS, WTOP_DETAIL_IMAGE_PATH, result) == WTOP_PROVIDER_VALID);
    assert(result->data.details.image_status.state == WTOP_PROVIDER_VALID);
    assert(!_wcsicmp(result->data.details.image_path, path));
    assert(result->data.details.command_status.state == WTOP_PROVIDER_UNAVAILABLE && !result->data.details.command_line[0]);
    assert(result->data.details.services_status.state == WTOP_PROVIDER_UNAVAILABLE && !result->data.details.service_count);
    assert(result->data.details.affinity.status.state == WTOP_PROVIDER_UNAVAILABLE);
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    for(i = 0; i < 6; ++i) {
        test_threads(&session, &child, result);
        assert(GetProcessHandleCount(GetCurrentProcess(), &after));
        assert(before == after);
    }
    test_affinity(&session, &child, result);
    test_identity_and_cancellation(&session, &child, result);
    state = collect(&session, &child, WTOP_PROVIDER_PROCESS_DETAILS, WTOP_DETAIL_ALL, result);
    printf("native full details bundle: %s; image=%s, command=%s, services=%s, affinity=%s\n",
        wtop_provider_state_name(state), wtop_provider_state_name(result->data.details.image_status.state),
        wtop_provider_state_name(result->data.details.command_status.state),
        wtop_provider_state_name(result->data.details.services_status.state),
        wtop_provider_state_name(result->data.details.affinity.status.state));
    assert(state == WTOP_PROVIDER_VALID || state == WTOP_PROVIDER_TIMEOUT);
    if(state == WTOP_PROVIDER_VALID) {
        assert(result->data.details.image_status.state == WTOP_PROVIDER_VALID);
        if(result->data.details.command_status.state == WTOP_PROVIDER_VALID)
            assert(wcsstr(result->data.details.command_line, L"--feature-child"));
        if(result->data.details.services_status.state == WTOP_PROVIDER_VALID)
            assert(!result->data.details.service_count);
    }
    /* DACL denial is deterministic for this owned child, independently of
       whether the test runner's token belongs to the Administrators group. */
    start_child(&denied, path, 1);
    assert(collect(&session, &denied, WTOP_PROVIDER_THREADS, 0, result) == WTOP_PROVIDER_DENIED);
    assert(wtop_process_affinity_apply(denied.process.dwProcessId, denied.creation, 1, 0, &outcome) == WTOP_PROVIDER_DENIED);
    assert(!outcome.attempted && !outcome.applied);
    stop_child(&denied);
    /* Authenticate the real old lifetime after its termination; an exited
       retained handle/PID must never permit a mutation. */
    assert(SetEvent(child.stop));
    assert(WaitForSingleObject(child.process.hProcess, 5000) == WAIT_OBJECT_0);
    assert(wtop_process_affinity_apply(child.process.dwProcessId, child.creation, 1, 0, &outcome) == WTOP_PROVIDER_IDENTITY_CHANGED);
    assert(!outcome.attempted && !outcome.applied);
    stop_child(&child);
    assert(wtop_provider_session_close(&session, 3000));
    free(result); free(path);
    puts("native details/thread/affinity identity and ownership fixtures passed");
    return 0;
}
