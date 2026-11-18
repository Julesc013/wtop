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

#include <windows.h>
#include <psapi.h>
#include <sddl.h>
#include <lmcons.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <assert.h>
#include <conio.h>
#include <pdh.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include "core.h"
#include "settings.h"
#include "settings_store_nt.h"
#include "text.h"
#include "ui.h"
#include "metrics.h"
#include "actions.h"
#include "providers_nt.h"
#include "output_nt.h"
#include "console_frame_nt.h"
#include <io.h>
#include <fcntl.h>
#include "version.h"
#include "ntop.h"
#include "util.h"
#include "vi.h"

#define SCROLL_INTERVAL 20ULL
#define INPUT_LOOP_DELAY 30
#define CARET_INTERVAL 500
#define MAX_NAMEPARTS 10
#define MAX_NAMEPARTSIZE 255
#define SHUTDOWN_GRACE_MS 3000
#define EXIT_FORCED_SHUTDOWN 124

static int Width;
static int Height;
static int OldWidth;
static int OldHeight;
static int ConsoleOriginX, ConsoleOriginY;
static int RenderColumn, RenderRow;
static WORD CurrentColor, ColorOverride;
static wtop_settings Settings;
static wtop_console_frame Screen;
static wtop_ui_chrome Chrome;
static volatile LONG CollectionPaused;
static BOOL Paused;
static int FooterStarts[10], FooterWidths[10];
static int SizeX;
static int SizeY;
static int ProcessWindowPosY = 6;
static int ProcessHeaderRow;
static int TabsRow = -1;
static int StatusRow = -1;
static int FunctionBarRow = -1;
static int MouseEnabled;
static int ConfigDirty;
static int RenderInvalidated = 1;
static DWORD ProcessWindowHeight;
static DWORD VisibleProcessCount;
static WORD SavedAttributes;
HANDLE ConsoleHandle;
static HANDLE OldConsoleHandle;
static BOOL InteractiveMode = TRUE;
static CRITICAL_SECTION SyncLock;
static HANDLE ShutdownEvent;
static HANDLE CleanupDoneEvent;
static HANDLE ProcessListThread, PerCoreThread;
static wtop_provider_result *PendingCoreObservation;
static ULONGLONG CollectionDeadline, LogStartedTick;
static volatile LONG ShutdownRequested;
static DWORD CollectorError;
static ULONGLONG LastSampleElapsed;
static ULONGLONG LastSampleStartedTick, LastSampleObservedTick, LastSamplePublishedTick;
static ULONGLONG SampleSequence;
static DWORD LastSampleInterval;
static volatile LONG CollectorInterval;
static DWORD ObservedProcessCount;
static DWORD UnresolvedOwnerFilterCount;
static BOOL SampleComplete = TRUE;
static BOOL CpuMachine, ReadOnlyMode, CollectPerCore, SkipProcessRows;
static wtop_provider_session CoreProvider;
static wtop_provider_result *CoreObservation;
static ULONGLONG CoreObservedTick;
static double PerformanceFrequency;
static unsigned DisplayTextFlags;
int WtopIsReadOnly(void) { return ReadOnlyMode != FALSE; }


void RequestShutdown(void)
{
    InterlockedExchange(&ShutdownRequested, 1);
    if(ShutdownEvent) SetEvent(ShutdownEvent);
}

static BOOL ShouldStop(void)
{
    return InterlockedCompareExchange(&ShutdownRequested, 0, 0) != 0;
}

/* The qualified baseline uses one printable ASCII cell per Unicode scalar.
   Raw strings remain in the model; only this projection substitutes. A richer
   Unicode host profile needs independent cell-width and console evidence. */
static size_t NativeAscii(const TCHAR *Text, char *Output, size_t Capacity)
{
    size_t Simple = 0;
    while(Text[Simple] && Text[Simple] <= 126 && Text[Simple] >= 0) {
        if(Simple + 1 >= Capacity) {
            if(Capacity) Output[Simple] = 0;
            DisplayTextFlags |= WTOP_TEXT_CLIPPED;
            return Simple;
        }
        if(Text[Simple] < 32) { Output[Simple] = '?'; DisplayTextFlags |= WTOP_TEXT_CONTROL | WTOP_TEXT_LOSSY; }
        else Output[Simple] = (char)Text[Simple];
        ++Simple;
    }
    if(!Text[Simple]) { if(Capacity) Output[Simple] = 0; return Simple; }
    unsigned Flags = 0;
    char *Utf8 = ToUtf8WithStatus(Text, &Flags);
    if(!Utf8) {
        if(Capacity) Output[0] = 0;
        DisplayTextFlags |= WTOP_TEXT_INVALID | WTOP_TEXT_LOSSY;
        return 0;
    }
    wtop_text_result Rendered = wtop_text_ascii(Utf8, strlen(Utf8),
        Capacity ? Capacity - 1 : 0, Output, Capacity);
    DisplayTextFlags |= Flags | Rendered.flags;
    free(Utf8);
    return Rendered.cells;
}

static size_t NativeCells(const TCHAR *Text, TCHAR *Output, size_t Capacity)
{
    size_t input = 0, cells = 0;
    if(!Capacity) return 0;
    while(Text[input] && cells + 1 < Capacity) {
        unsigned long Scalar = (unsigned short)Text[input++];
#ifdef UNICODE
        if(Scalar >= 0xd800 && Scalar <= 0xdbff && Text[input] >= 0xdc00 && Text[input] <= 0xdfff)
            Scalar = 0x10000UL + ((Scalar - 0xd800UL) << 10) + ((unsigned short)Text[input++] - 0xdc00UL);
#endif
        Output[cells++] = (TCHAR)wtop_text_cell_glyph(Scalar, Settings.character_set == WTOP_CHARSET_UNICODE, &DisplayTextFlags);
    }
    Output[cells] = 0;
    if(Text[input]) DisplayTextFlags |= WTOP_TEXT_CLIPPED;
    return cells;
}

static int ConPrintf(const TCHAR *Fmt, ...)
{
    TCHAR Buffer[4096], Cells[4096];
    va_list Args;
    va_start(Args, Fmt);
    int Written = _vsntprintf_s(Buffer, _countof(Buffer), _TRUNCATE, Fmt, Args);
    va_end(Args);
    if(Written < 0) Written = (int)_tcslen(Buffer);
    if(!InteractiveMode) {
        if(!WriteTText(ConsoleHandle, Buffer, (size_t)Written)) RequestShutdown();
        return Written;
    }
    Written = (int)NativeCells(Buffer, Cells, _countof(Cells));
    Written = min(Written, max(0, Width - RenderColumn));
    for(int i = 0; i < Written; ++i)
        wtop_frame_put(&Screen.cells, RenderColumn + i, RenderRow, (unsigned short)Cells[i], CurrentColor);
    RenderColumn += Written;
    return Written;
}

static void DrawPaddedText(const TCHAR *Text, int Cells)
{
    TCHAR Display[4096];
    NativeCells(Text, Display, _countof(Display));
    ConPrintf(_T("%-*.*s"), Cells, Cells, Display);
}

static void ConPutc(TCHAR c)
{
    if(RenderColumn >= Width) return;
    wtop_frame_put(&Screen.cells, RenderColumn++, RenderRow,
        wtop_text_cell_glyph((unsigned short)c, Settings.character_set == WTOP_CHARSET_UNICODE, &DisplayTextFlags), CurrentColor);
}

#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_CYAN (FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_CYAN (BACKGROUND_INTENSITY | BACKGROUND_GREEN | BACKGROUND_BLUE)

static wtop_config_document ConfigDocument;
static wtop_settings_store ConfigStore;
static unsigned long SettingsRevision = 1;
static DWORD OriginalInputMode;

static wtop_tab *ActiveTab(void) { return &Settings.tabs[Settings.active_tab]; }

static void MarkSettingsChanged(void)
{
    ConfigDirty = TRUE;
    /* Saves are currently synchronous on the UI thread. Keep a revision anyway
       so a future asynchronous store cannot clear edits made after capture. */
    if(SettingsRevision < ULONG_MAX) ++SettingsRevision;
}

static BOOL ResolveConfigStore(const wtop_options *Options, wtop_store_result *Result)
{
    wtop_store_request Request;
    memset(&Request, 0, sizeof(Request));
    Request.explicit_path = Options->config_path;
    Request.portable_root = Options->portable_root;
    Request.portable = Options->portable;
    Request.no_config = Options->no_config;
    return wtop_store_resolve(&ConfigStore, &Request, Result);
}

static BOOL ReadConfigFile(const wtop_options *Options)
{
    wtop_store_result Result;
    if(!ResolveConfigStore(Options, &Result) ||
       !wtop_store_load(&ConfigStore, &Settings, &ConfigDocument, &Result)) {
        fprintf(stderr, "wtop: configuration %s at line %lu: %s (Win32 %lu)\n",
            wtop_store_outcome_name(Result.outcome), Result.error_line,
            Result.message ? Result.message : "failed", Result.native_error);
        return FALSE;
    }
    return TRUE;
}

/* Full recovery names go to the invoking diagnostic stream even after a
   successful verified save retained an original/cleanup warning. In TUI mode
   the caller's stream survives our alternate screen and is readable on return. */
static void ReportSettingsRecovery(const wtop_settings_store *Store, const wtop_store_result *Result)
{
    if(Result->outcome == WTOP_STORE_OK && Result->native_error)
        fprintf(stderr, "wtop: settings saved with warning: %s (Win32 %lu)\n", Result->message, Result->native_error);
    if(Store->recovery_temporary[0] || Store->recovery_backup[0]) {
        char *Temporary = ToUtf8(Store->recovery_temporary), *Backup = ToUtf8(Store->recovery_backup);
        fprintf(stderr, "wtop: recoverable temporary=%s backup=%s\n", Temporary ? Temporary : "", Backup ? Backup : "");
        free(Temporary); free(Backup);
    }
}

static BOOL ExportConfigFile(const char *Path, wtop_store_result *Result)
{
    if(ReadOnlyMode) {
        memset(Result, 0, sizeof(*Result));
        Result->outcome = WTOP_STORE_DENIED;
        Result->native_error = ERROR_ACCESS_DENIED;
        Result->message = "configuration writes disabled by --readonly";
        return FALSE;
    }
    wtop_settings_store *Export = calloc(1, sizeof(*Export));
    if(!Export) {
        memset(Result, 0, sizeof(*Result));
        Result->outcome = WTOP_STORE_UNCHANGED_FAILURE;
        Result->native_error = ERROR_NOT_ENOUGH_MEMORY;
        Result->message = "cannot allocate settings store";
        return FALSE;
    }
    wtop_config_document Document = {0};
    const char *Error = NULL;
    BOOL Ok = wtop_config_document_clone(&Document, &ConfigDocument, &Error);
    if(!Ok) {
        memset(Result, 0, sizeof(*Result));
        Result->outcome = WTOP_STORE_UNCHANGED_FAILURE;
        Result->native_error = ERROR_NOT_ENOUGH_MEMORY;
        Result->message = Error;
    } else {
        Ok = wtop_store_export_target(Export, Path, Result);
        if(Ok) {
            wcscpy_s(Export->read_path, _countof(Export->read_path), ConfigStore.read_path);
            Ok = wtop_store_save(Export, &Settings, &Document, SettingsRevision, Result);
        }
    }
    ReportSettingsRecovery(Export, Result);
    wtop_config_document_free(&Document);
    wtop_store_dispose(Export);
    free(Export);
    return Ok;
}


static void SetColor(WORD Color)
{
	Color |= ColorOverride;
	CurrentColor = Color;

}

static void SetConCursorPos(SHORT X, SHORT Y)
{
    RenderColumn = X;
    RenderRow = Y;
}

typedef wtop_metric_state metric_state;
#define METRIC_OK WTOP_METRIC_VALID
#define METRIC_DENIED WTOP_METRIC_DENIED
#define METRIC_UNAVAILABLE WTOP_METRIC_UNSUPPORTED
#define METRIC_WARMING WTOP_METRIC_WARMING_UP
#define METRIC_DISCONTINUITY WTOP_METRIC_RESET

static const char *MetricStateName(metric_state State)
{
    return wtop_metric_state_name(State);
}

static metric_state LastMetricError(void)
{
    return GetLastError() == ERROR_ACCESS_DENIED ? METRIC_DENIED : WTOP_METRIC_ERROR;
}

static wtop_counter CounterValue(ULONGLONG Value)
{
    wtop_counter Result;
    Result.high = (unsigned long)(Value >> 32);
    Result.low = (unsigned long)(Value & 0xffffffffUL);
    return Result;
}

static ULONGLONG NativeCounter(wtop_counter Value)
{
    return ((ULONGLONG)Value.high << 32) | Value.low;
}
static metric_state FreshMetricState(metric_state State, ULONGLONG Observed, ULONGLONG Now);

typedef struct process {
	HANDLE Handle;
	DWORD ID;
	TCHAR UserName[UNLEN + 1];
    WCHAR OwnerSid[WTOP_PROVIDER_SID_CHARS];
    wtop_provider_state OwnerNameState;
    ULONGLONG OwnerNameObservedTick;
    BOOL OwnerNameStale;
	DWORD BasePriority;
	double PercentProcessorTime;
	unsigned __int64 UsedMemory;
	DWORD ThreadCount;
	ULONGLONG UpTime;
	TCHAR ExeName[MAX_PATH];
	DWORD ParentPID;
	ULONGLONG DiskOperationsPrev;
	ULONGLONG DiskOperations;
	double DiskUsage;
    ULONGLONG CreationTime;
    wtop_cpu_observation PreviousCpuObservation;
    double CpuElapsedMs, CpuAcquisitionMs;
    ULONGLONG CpuObservedTick, ObservedTick;
    ULONGLONG PreviousRead;
    ULONGLONG PreviousWrite;
    ULONGLONG IoReadTotal;
    ULONGLONG IoWriteTotal;
    ULONGLONG SampleTick;
    BOOL PreviousIoValid;
    metric_state OwnerState, CpuState, MemoryState, IoState, AgeState;
	DWORD TreeDepth;

	struct process *Next;
	struct process *Parent;
	struct process *FirstChild;
	struct process *LastChild;
} process;

#define PROCLIST_BUF_INCREASE 64
#define MAX_PROCESS_ROWS 65536UL
#define MAX_TAGGED_IDENTITIES 4096UL

static process *ProcessList;
static DWORD ProcessListSize = PROCLIST_BUF_INCREASE;

/* The collector owns a private sample until this single-slot publication.
   The UI owns its adopted sample. No OS query, rendering, action, allocation or
   destruction runs while the publication lock is held; superseded pending
   samples are freed outside it. Memory is bounded by front/pending/in-flight. */
typedef struct pending_sample {
    process *rows;
    DWORD count, observed_count, unresolved_owner_filter, running, capacity, interval;
    BOOL complete;
    metric_state cpu_state;
    double cpu_busy;
    ULONGLONG cpu_observed, started, observed, published, elapsed, sequence;
} pending_sample;
static pending_sample *PendingSample;
static ULONGLONG CollectorSequence;

static void FreePendingSample(pending_sample *Sample)
{
    if(Sample) { free(Sample->rows); free(Sample); }
}

typedef struct process_identity { DWORD ID; ULONGLONG CreationTime; } process_identity;
static process_identity *TaggedProcessList;
static DWORD TaggedProcessListCount;
static DWORD TaggedProcessListSize = PROCLIST_BUF_INCREASE;

static void ToggleTaggedProcess(const process *Selected)
{
    DWORD ID = Selected->ID;
    if(!Selected->CreationTime) { SetViMessage(VI_ERROR, _T("Cannot tag a process with unknown creation identity")); return; }
    for(DWORD i = 0; i < TaggedProcessListCount; ++i) {
        if(TaggedProcessList[i].ID == ID && TaggedProcessList[i].CreationTime == Selected->CreationTime) {
            for(; i + 1 < TaggedProcessListCount; ++i) TaggedProcessList[i] = TaggedProcessList[i + 1];
            --TaggedProcessListCount;
            return;
        }
    }
    if(TaggedProcessListCount >= MAX_TAGGED_IDENTITIES) {
        SetViMessage(VI_ERROR, _T("Tag limit reached (4096 identities); clear tags with U before adding more"));
        return;
    }
    if(TaggedProcessListCount == TaggedProcessListSize) {
        DWORD Capacity = min(MAX_TAGGED_IDENTITIES, TaggedProcessListSize + PROCLIST_BUF_INCREASE);
        process_identity *Larger = realloc(TaggedProcessList, (size_t)Capacity * sizeof(*Larger));
        if(!Larger) { SetViMessage(VI_ERROR, _T("Cannot tag process: out of memory")); return; }
        TaggedProcessList = Larger; TaggedProcessListSize = Capacity;
    }
    TaggedProcessList[TaggedProcessListCount].ID = ID;
    TaggedProcessList[TaggedProcessListCount++].CreationTime = Selected->CreationTime;
}

static BOOL IsProcessTagged(const process *Selected)
{
	for(DWORD i = 0; i < TaggedProcessListCount; i++) {
		if(TaggedProcessList[i].ID == Selected->ID && TaggedProcessList[i].CreationTime == Selected->CreationTime)
			return TRUE;
	}
	return FALSE;
}

static DWORD ProcessCount = 0;
static DWORD RunningProcessCount = 0;
static DWORD ProcessIndex = 0;
static DWORD SelectedProcessIndex = 0;
static DWORD SelectedViewIndex;
static DWORD *ViewIndices;
static DWORD ViewCount, ViewCapacity;
static process_identity *RenderedRows;
static DWORD RenderedRowCount, RenderedRowCapacity;
static wtop_ui_column_span RenderedColumns[WTOP_MAX_COLUMNS];
static int RenderedColumnCount;
static int RenderedTabStart[WTOP_MAX_TABS], RenderedTabWidth[WTOP_MAX_TABS];
static DWORD DisplayedProcessID = 0;
static ULONGLONG DisplayedCreationTime;
static BOOL FollowProcess = FALSE;
static DWORD FollowProcessID = 0;
static ULONGLONG FollowCreationTime;

typedef enum sort_order {
	ASCENDING,
	DESCENDING
} sort_order;

static sort_order SortOrder = DESCENDING;

typedef int (*process_sort_fn_t)(const void *, const void *);

/* A frozen comparison timestamp keeps qsort's ordering transitive even if the
   sample ages across the sort. Missing values always sort after known values. */
static ULONGLONG SortObservedTick;
static int CompareIdentity(const process *Left, const process *Right)
{
    if(Left->ID != Right->ID) return Left->ID < Right->ID ? -1 : 1;
    return (Left->CreationTime > Right->CreationTime) - (Left->CreationTime < Right->CreationTime);
}
static int DirectedCompare(int Compare, const process *Left, const process *Right)
{
    return Compare ? (SortOrder == ASCENDING ? Compare : -Compare) : CompareIdentity(Left, Right);
}

#define SORT_PROCESS_BY_INTEGER(Attribute) \
static int SortProcessBy##Attribute(const void *A, const void *B) \
{ \
    const process *Left = A, *Right = B; \
    int Compare = (Left->Attribute > Right->Attribute) - (Left->Attribute < Right->Attribute); \
    return DirectedCompare(Compare, Left, Right); \
}

#define SORT_PROCESS_BY_METRIC(Attribute, State, Tick) \
static int SortProcessBy##Attribute(const void *A, const void *B) \
{ \
    const process *Left = A, *Right = B; \
    metric_state L = FreshMetricState(Left->State, Left->Tick, SortObservedTick); \
    metric_state R = FreshMetricState(Right->State, Right->Tick, SortObservedTick); \
    int Available = wtop_metric_sort_availability(L, R); \
    if(Available) return Available; \
    if(L != METRIC_OK) return CompareIdentity(Left, Right); \
    return DirectedCompare((Left->Attribute > Right->Attribute) - (Left->Attribute < Right->Attribute), Left, Right); \
}

#define SORT_PROCESS_BY_STRING(Attribute, MaxLength) \
static int SortProcessBy##Attribute(const void *A, const void *B) \
{ \
    const process *Left = A, *Right = B; \
    return DirectedCompare(_tcsncicmp(Left->Attribute, Right->Attribute, MaxLength), Left, Right); \
}

SORT_PROCESS_BY_INTEGER(ID);
SORT_PROCESS_BY_METRIC(PercentProcessorTime, CpuState, CpuObservedTick);
SORT_PROCESS_BY_METRIC(UsedMemory, MemoryState, ObservedTick);
SORT_PROCESS_BY_METRIC(UpTime, AgeState, CpuObservedTick);
SORT_PROCESS_BY_INTEGER(BasePriority);
SORT_PROCESS_BY_INTEGER(ThreadCount);
SORT_PROCESS_BY_INTEGER(ParentPID);
SORT_PROCESS_BY_METRIC(DiskUsage, IoState, ObservedTick);
SORT_PROCESS_BY_STRING(ExeName, MAX_PATH);
SORT_PROCESS_BY_STRING(UserName, UNLEN);

static process_sort_type ProcessSortType = SORT_BY_ID;

static DWORD CPUCoreCount;
static double CPUUsage;
static BOOL CPUUsageKnown;
static metric_state CPUUsageState = WTOP_METRIC_WARMING_UP;
static DWORD ActiveCpuCapacity;
static ULONGLONG CpuSystemObservedTick;
/* Native API resolver stays in this adapter; core callbacks never carry handles. */
typedef DWORD (WINAPI *active_cpu_fn)(WORD);
static active_cpu_fn GetAllActiveProcessors;
static DWORD CurrentCpuCapacity(void)
{
    return GetAllActiveProcessors ? GetAllActiveProcessors(0xffff) : 0;
}



void AddChildProcess(process *ParentProcess, process *Process)
{
    if(ParentProcess == Process) return;
    if(ParentProcess->LastChild) ParentProcess->LastChild->Next = Process;
    else ParentProcess->FirstChild = Process;
    ParentProcess->LastChild = Process;
    Process->Parent = ParentProcess;
}

/* The "root" process which does not actually exist, acts as process tree root */
static process RootProcess;
/* These counts qualify tree presentation without inventing parent identity.
   Rows with absent/equal/newer/unknown parent creation remain visible roots. */
static DWORD TreeUnresolvedParentCount;
static BOOL TreeIndexUnavailable;
#ifdef WTOP_TREE_TESTING
static BOOL TreeFailIndexAllocation;
#endif

static int CompareTreeProcessId(const void *Left, const void *Right)
{
    const process *A = *(process *const *)Left;
    const process *B = *(process *const *)Right;
    return (A->ID > B->ID) - (A->ID < B->ID);
}

static process *FindIndexedParent(process *const *Index, DWORD Id)
{
    DWORD Begin = 0, End = ProcessCount;
    while(Begin < End) {
        DWORD Middle = Begin + (End - Begin) / 2;
        if(Index[Middle]->ID < Id) Begin = Middle + 1;
        else End = Middle;
    }
    if(Begin == ProcessCount || Index[Begin]->ID != Id) return NULL;
    /* Duplicate PIDs in a source sample are ambiguous, never an arbitrary match. */
    if(Begin + 1 < ProcessCount && Index[Begin + 1]->ID == Id) return NULL;
    return Index[Begin];
}

/* O(n log n) indexed joins plus O(n) adjacency and depth traversal. A strict
   creation ordering proves acyclicity, including malformed PID parent graphs;
   no per-node ancestry scan or recursion is proportional to the tree depth.
   FILETIME equality does not prove who came first, so those rows stay roots. */
void FindParentChildProcesses(void)
{
    process **Index = NULL;
    memset(&RootProcess, 0, sizeof(RootProcess));
    TreeUnresolvedParentCount = 0;
    TreeIndexUnavailable = FALSE;
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].TreeDepth = 0;
        ProcessList[i].Next = ProcessList[i].Parent = ProcessList[i].FirstChild = ProcessList[i].LastChild = NULL;
    }
    if(!ProcessCount) return;
#ifdef WTOP_TREE_TESTING
    if(!TreeFailIndexAllocation)
#endif
        Index = malloc((size_t)ProcessCount * sizeof(*Index));
    if(!Index) {
        /* Keep every row, preserving iteration order. No partial tree is
           published and allocation failure does not terminate the monitor. */
        TreeIndexUnavailable = TRUE;
        for(DWORD i = 0; i < ProcessCount; ++i) {
            AddChildProcess(&RootProcess, &ProcessList[i]);
            if(ProcessList[i].ParentPID) ++TreeUnresolvedParentCount;
        }
        SetViMessage(VI_ERROR, _T("Tree index unavailable; all processes remain visible as roots"));
        return;
    }
    for(DWORD i = 0; i < ProcessCount; ++i) Index[i] = &ProcessList[i];
    qsort(Index, ProcessCount, sizeof(*Index), CompareTreeProcessId);
    for(DWORD i = 0; i < ProcessCount; ++i) {
        process *Child = &ProcessList[i];
        process *Parent = &RootProcess;
        if(Child->ParentPID) {
            process *Candidate = FindIndexedParent(Index, Child->ParentPID);
            if(Candidate && Candidate != Child && Candidate->CreationTime &&
               Child->CreationTime && Candidate->CreationTime < Child->CreationTime) Parent = Candidate;
            else ++TreeUnresolvedParentCount;
        }
        AddChildProcess(Parent, Child);
    }
    free(Index);
    process *Node = RootProcess.FirstChild;
    DWORD Depth = 0, Visited = 0;
    while(Node && Visited < ProcessCount) {
        Node->TreeDepth = Depth;
        ++Visited;
        if(Node->FirstChild) { Node = Node->FirstChild; ++Depth; }
        else {
            while(Node && Node != &RootProcess && !Node->Next) {
                Node = Node->Parent;
                if(Depth) --Depth;
            }
            Node = Node && Node != &RootProcess ? Node->Next : NULL;
        }
    }
}

/* Iterative traversal avoids recursion proportional to the process count. */
void ProcessTreeToList(process *Root, process *Dest, int *Index)
{
    process *Node = Root->FirstChild;
    while(Node && (DWORD)*Index < ProcessCount) {
        Dest[(*Index)++] = *Node;
        if(Node->FirstChild) Node = Node->FirstChild;
        else {
            while(Node && Node != Root && !Node->Next) Node = Node->Parent;
            Node = Node && Node != Root ? Node->Next : NULL;
        }
    }
}

static void SortProcessList(void)
{
    if(!ProcessCount) return;
    SortObservedTick = GetTickCount64();
	if(ProcessSortType != SORT_BY_TREE)
	{
		process_sort_fn_t SortFn = 0;

		switch(ProcessSortType) {
		case SORT_BY_ID:
			SortFn = SortProcessByID;
			break;
		case SORT_BY_PROCESS:
			SortFn = SortProcessByExeName;
			break;
		case SORT_BY_USER_NAME:
			SortFn = SortProcessByUserName;
			break;
		case SORT_BY_PROCESSOR_TIME:
			SortFn = SortProcessByPercentProcessorTime;
			break;
		case SORT_BY_USED_MEMORY:
			SortFn = SortProcessByUsedMemory;
			break;
		case SORT_BY_UPTIME:
			SortFn = SortProcessByUpTime;
			break;
		case SORT_BY_PRIORITY:
			SortFn = SortProcessByBasePriority;
			break;
		case SORT_BY_THREAD_COUNT:
			SortFn = SortProcessByThreadCount;
			break;
		case SORT_BY_DISK_USAGE:
			SortFn = SortProcessByDiskUsage;
			break;
        default:
            break;
		}

		if(SortFn) {
			qsort(ProcessList, ProcessCount, sizeof(*ProcessList), SortFn);
		}

        /* Flat views do not need the potentially expensive ancestry graph. */
	} else {
		SortOrder = ASCENDING;
		qsort(ProcessList, ProcessCount, sizeof(*ProcessList), SortProcessByParentPID);

		FindParentChildProcesses();

		process *TreeProcessList = malloc((size_t)ProcessCount * sizeof(*ProcessList));
        if(!TreeProcessList) { CollectorError = ERROR_NOT_ENOUGH_MEMORY; RequestShutdown(); return; }
		int Index = 0;
		ProcessTreeToList(&RootProcess, TreeProcessList, &Index);

		ProcessCount = Index;
		memcpy(ProcessList, TreeProcessList, ProcessCount * sizeof(*ProcessList));

		free(TreeProcessList);
	}
}

static BOOL FilterByUserName = FALSE;
static TCHAR FilterUserName[UNLEN + 1];

static BOOL FilterByPID = FALSE;
static DWORD PidFilterList[1024];
static DWORD PidFilterCount;

static BOOL FilterByName = FALSE;
static TCHAR NameFilterList[MAX_NAMEPARTS][MAX_NAMEPARTSIZE+1];
static DWORD NameFilterCount;

static BOOL ContainsInsensitive(const TCHAR *Text, const TCHAR *Pattern)
{
    size_t Length = _tcslen(Pattern);
    if(!Length) return TRUE;
    for(; *Text; ++Text) if(!_tcsnicmp(Text, Pattern, Length)) return TRUE;
    return FALSE;
}

/* A display index is separate from collected process storage. Filtering never
   changes the collector's set or races its static command-line filters. */
static void RebuildView(void)
{
    if(ProcessCount > ViewCapacity) {
        DWORD *Larger = realloc(ViewIndices, (size_t)ProcessCount * sizeof(*Larger));
        if(!Larger) { ViewCount = 0; CollectorError = ERROR_NOT_ENOUGH_MEMORY; RequestShutdown(); return; }
        ViewIndices = Larger; ViewCapacity = ProcessCount;
    }
    TCHAR *Filter = FromUtf8(ActiveTab()->filter);
    if(!Filter) { ViewCount = 0; CollectorError = ERROR_NOT_ENOUGH_MEMORY; RequestShutdown(); return; }
    ViewCount = 0;
    DWORD Found = MAXDWORD;
    for(DWORD i = 0; i < ProcessCount; ++i) {
        if(!ContainsInsensitive(ProcessList[i].ExeName, Filter)) continue;
        ViewIndices[ViewCount] = i;
        if(i == SelectedProcessIndex) Found = ViewCount;
        ++ViewCount;
    }
    free(Filter);
    if(!ViewCount) { SelectedProcessIndex = SelectedViewIndex = ProcessIndex = 0; return; }
    SelectedViewIndex = Found == MAXDWORD ? min(SelectedViewIndex, ViewCount - 1) : Found;
    SelectedProcessIndex = ViewIndices[SelectedViewIndex];
}

static void SelectView(DWORD Index)
{
    if(!ViewCount) { ProcessIndex = SelectedViewIndex = SelectedProcessIndex = 0; return; }
    DWORD Visible = max(VisibleProcessCount, 1);
    SelectedViewIndex = min(Index, ViewCount - 1);
    SelectedProcessIndex = ViewIndices[SelectedViewIndex];
    if(SelectedViewIndex < ProcessIndex) ProcessIndex = SelectedViewIndex;
    if(SelectedViewIndex - ProcessIndex >= Visible) ProcessIndex = SelectedViewIndex - Visible + 1;
}

static void SelectProcess(DWORD Index)
{
    for(DWORD i = 0; i < ViewCount; ++i) if(ViewIndices[i] == Index) { SelectView(i); return; }
}

static void ReadjustCursor(void)
{
    RebuildView();
    if(!ViewCount) { FollowProcess = FALSE; return; }
    DWORD Visible = max(VisibleProcessCount, 1);
    ProcessIndex = min(ProcessIndex, ViewCount > Visible ? ViewCount - Visible : 0);
    if(FollowProcess) {
        BOOL Found = FALSE;
        for(DWORD i = 0; i < ViewCount; ++i) {
            const process *Process = &ProcessList[ViewIndices[i]];
            if(Process->ID == FollowProcessID && Process->CreationTime == FollowCreationTime) {
                SelectView(i); Found = TRUE; break;
            }
        }
        if(!Found) FollowProcess = FALSE;
    } else SelectView(SelectedViewIndex);
}

void SetPresentationFilter(const TCHAR *Pattern)
{
    char *Utf8 = ToUtf8(Pattern);
    if(!Utf8 || strlen(Utf8) >= sizeof(ActiveTab()->filter)) {
        free(Utf8); SetViMessage(VI_ERROR, _T("Filter is too long (maximum 127 UTF-8 bytes)")); return;
    }
    strcpy_s(ActiveTab()->filter, sizeof(ActiveTab()->filter), Utf8);
    free(Utf8);
    MarkSettingsChanged();
    ProcessIndex = SelectedViewIndex = 0;
    ReadjustCursor();
}

static TCHAR SearchPattern[256];
static BOOLEAN CaseInsensitiveSearch = FALSE;
static BOOLEAN SearchActive;

static void SearchNext(void);

void StartSearch(const TCHAR *Pattern)
{
    if(_tcslen(Pattern) >= _countof(SearchPattern)) {
        SetViMessage(VI_ERROR, _T("Search pattern is too long (maximum 255 characters)"));
        return;
    }
    _tcscpy_s(SearchPattern, _countof(SearchPattern), Pattern);

	// if the whole string is lower case use case insensitive search
	CaseInsensitiveSearch = TRUE;
	for (int i = 0; i < 256; i++)
	{
		if (SearchPattern[i] == 0)
		{
			break;
		}
		if (_istupper(IntFromTChar(SearchPattern[i])))
		{
			CaseInsensitiveSearch = FALSE;
			break;
		}
	}

	SearchActive = TRUE;
	SearchNext();
}

static BOOLEAN SearchMatchesProcess(const process *Process)
{
	TCHAR TempProcessName[MAX_PATH];
	_tcsncpy_s(TempProcessName, MAX_PATH, Process->ExeName, MAX_PATH);
	if (CaseInsensitiveSearch) // which means that the SearchPattern is already lower case,
							   // so we convert the process name to lower case to make the
							   // search case insensitive
	{
		for (int i = 0; i < MAX_PATH; i++)
		{
			if (TempProcessName[i] == 0)
				break;
			TempProcessName[i] = (TCHAR)_totlower(IntFromTChar(TempProcessName[i]));
		}
	}

	return _tcsstr(TempProcessName, SearchPattern) != 0;
}

static void SearchNext(void)
{
    if(!SearchActive || !ViewCount) return;
    for(DWORD Step = 1; Step <= ViewCount; ++Step) {
        DWORD View = (SelectedViewIndex + Step) % ViewCount;
        if(SearchMatchesProcess(&ProcessList[ViewIndices[View]])) { SelectView(View); return; }
    }
    SetViMessage(VI_ERROR, _T("Pattern not found: %s"), SearchPattern);
}

static void SearchPrevious(void)
{
    if(!SearchActive || !ViewCount) return;
    for(DWORD Step = 1; Step <= ViewCount; ++Step) {
        DWORD View = (SelectedViewIndex + ViewCount - Step) % ViewCount;
        if(SearchMatchesProcess(&ProcessList[ViewIndices[View]])) { SelectView(View); return; }
    }
    SetViMessage(VI_ERROR, _T("Pattern not found: %s"), SearchPattern);
}

static ULONGLONG FileTimeValue(const FILETIME *Value)
{
    return ((ULONGLONG)Value->dwHighDateTime << 32) | Value->dwLowDateTime;
}

/* The acquisition interval is attached to this exact process handle. QPC is
   sampled immediately around GetProcessTimes, never around the whole sweep. */
static BOOL ObserveProcessCpu(process *Process, wtop_cpu_observation *Observation)
{
    FILETIME Created, Exited, Kernel, User;
    LARGE_INTEGER Before, After;
    memset(Observation, 0, sizeof(*Observation));
    Observation->epoch = 1;
    Observation->state = WTOP_METRIC_ERROR;
    BOOL HaveBefore = QueryPerformanceCounter(&Before);
    BOOL Ok = GetProcessTimes(Process->Handle, &Created, &Exited, &Kernel, &User);
    DWORD Error = Ok ? ERROR_SUCCESS : GetLastError();
    BOOL HaveAfter = QueryPerformanceCounter(&After);
    Process->CpuObservedTick = GetTickCount64();
    if(!Ok) {
        Observation->state = Error == ERROR_ACCESS_DENIED ? METRIC_DENIED : WTOP_METRIC_ERROR;
        return FALSE;
    }
    Observation->lifetime = CounterValue(FileTimeValue(&Created));
    Observation->kernel = CounterValue(FileTimeValue(&Kernel));
    Observation->user = CounterValue(FileTimeValue(&User));
    if(HaveBefore && HaveAfter && Before.QuadPart >= 0 && After.QuadPart >= 0 && PerformanceFrequency > 0) {
        Observation->before = CounterValue((ULONGLONG)Before.QuadPart);
        Observation->after = CounterValue((ULONGLONG)After.QuadPart);
        Observation->state = METRIC_OK;
    }
    return TRUE;
}

static void CloseSampleHandles(process *Sample, DWORD Count)
{
    for(DWORD i = 0; i < Count; ++i) {
        if(Sample[i].Handle) { CloseHandle(Sample[i].Handle); Sample[i].Handle = NULL; }
    }
}

static BOOL MatchesStaticFilter(const process *Process)
{
    if(FilterByPID) {
        BOOL Found = FALSE;
        for(DWORD i = 0; i < PidFilterCount; ++i) if(PidFilterList[i] == Process->ID) Found = TRUE;
        if(!Found) return FALSE;
    }
    if(FilterByName) {
        BOOL Found = FALSE;
        for(DWORD i = 0; i < NameFilterCount; ++i)
            if(_tcsstr(Process->ExeName, NameFilterList[i])) Found = TRUE;
        if(!Found) return FALSE;
    }
    return TRUE;
}

static DWORD RemainingCollectionWait(DWORD Requested)
{
    if(!CollectionDeadline) return Requested;
    ULONGLONG Now = GetTickCount64();
    if(Now >= CollectionDeadline) return 0;
    return (DWORD)min((ULONGLONG)Requested, CollectionDeadline - Now);
}

static wtop_provider_result *CollectCoreObservation(DWORD Interval)
{
    wtop_provider_result *Result = calloc(1, sizeof(*Result));
    if(!Result) return NULL;
    wtop_provider_request Request;
    memset(&Request, 0, sizeof(Request));
    Request.kind = WTOP_PROVIDER_PER_CORE; Request.interval_ms = Interval;
    DWORD Timeout = RemainingCollectionWait(min(60000UL, max(3000UL, Interval + 2000UL)));
    if(!Timeout) { Result->kind = Request.kind; Result->state = WTOP_PROVIDER_TIMEOUT; Result->retirement_confirmed = 1; }
    else wtop_provider_collect(&CoreProvider, &Request, Timeout, ShutdownEvent, Result);
    return Result;
}

static DWORD WINAPI PollCoreThreadProc(LPVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    unsigned Failures = 0;
    while(!ShouldStop()) {
        if(InterlockedCompareExchange(&CollectionPaused, 0, 0)) { WaitForSingleObject(ShutdownEvent, 50); continue; }
        DWORD Interval = (DWORD)InterlockedCompareExchange(&CollectorInterval, 0, 0);
        wtop_provider_result *Result = CollectCoreObservation(Interval);
        if(!Result) return ERROR_NOT_ENOUGH_MEMORY;
        wtop_provider_state State = Result->state;
        EnterCriticalSection(&SyncLock);
        wtop_provider_result *Superseded = PendingCoreObservation;
        PendingCoreObservation = Result;
        LeaveCriticalSection(&SyncLock);
        free(Superseded);
        if(State == WTOP_PROVIDER_VALID || State == WTOP_PROVIDER_CAPACITY) Failures = 0;
        else ++Failures;
        if(State == WTOP_PROVIDER_UNSUPPORTED || State == WTOP_PROVIDER_DENIED ||
           CoreProvider.admission_closed || Failures >= 3) break;
        if(Failures && WaitForSingleObject(ShutdownEvent, max(1000UL, Interval)) != WAIT_TIMEOUT) break;
    }
    return 0;
}

static BOOL AdoptPendingCore(void)
{
    EnterCriticalSection(&SyncLock);
    wtop_provider_result *Next = PendingCoreObservation;
    PendingCoreObservation = NULL;
    LeaveCriticalSection(&SyncLock);
    if(!Next) return FALSE;
    free(CoreObservation); CoreObservation = Next;
    CoreObservedTick = Next->observed_ms;
    RenderInvalidated = TRUE;
    return TRUE;
}

#define OWNER_CACHE_ENTRIES 32
static struct owner_cache_entry {
    WCHAR sid[WTOP_PROVIDER_SID_CHARS];
    WCHAR name[UNLEN + 1];
    wtop_provider_state state;
    ULONGLONG observed, attempted;
} OwnerCache[OWNER_CACHE_ENTRIES];
static unsigned OwnerCacheNext;
static unsigned OwnerLookupAllowance;
static ULONGLONG OwnerLookupDeadline;
static wtop_provider_session OwnerProvider;

static void ResolveObservedOwner(process *Process)
{
    Process->OwnerNameState = WTOP_PROVIDER_WARMING_UP;
    if(!Process->OwnerSid[0]) return;
    ULONGLONG Now = GetTickCount64();
    struct owner_cache_entry *Entry = NULL;
    for(unsigned i = 0; i < OWNER_CACHE_ENTRIES; ++i) {
        if(!wcscmp(OwnerCache[i].sid, Process->OwnerSid)) { Entry = &OwnerCache[i]; break; }
    }
    BOOL Expired = !Entry || Now - Entry->attempted > 60000;
    if(Expired && OwnerLookupAllowance && Now < OwnerLookupDeadline && !ShouldStop() && !OwnerProvider.admission_closed) {
        --OwnerLookupAllowance;
        wtop_provider_request Request;
        memset(&Request, 0, sizeof(Request));
        Request.kind = WTOP_PROVIDER_ACCOUNT;
        wcscpy_s(Request.sid, _countof(Request.sid), Process->OwnerSid);
        wtop_provider_result *Result = calloc(1, sizeof(*Result));
        if(Result) {
            DWORD Budget = (DWORD)min(200ULL, OwnerLookupDeadline - Now);
            wtop_provider_collect(&OwnerProvider, &Request, max(1UL, Budget), ShutdownEvent, Result);
            if(!Entry) Entry = &OwnerCache[OwnerCacheNext++ % OWNER_CACHE_ENTRIES];
            memset(Entry, 0, sizeof(*Entry));
            wcscpy_s(Entry->sid, _countof(Entry->sid), Process->OwnerSid);
            Entry->attempted = GetTickCount64(); Entry->observed = Result->observed_ms; Entry->state = Result->state;
            if(Result->state == WTOP_PROVIDER_VALID)
                wcsncpy_s(Entry->name, _countof(Entry->name), Result->data.account.name, _TRUNCATE);
            free(Result);
        }
    }
    if(Entry) {
        Process->OwnerNameState = Entry->state;
        Process->OwnerNameObservedTick = Entry->observed;
        Process->OwnerNameStale = Entry->state == WTOP_PROVIDER_VALID &&
            (!Entry->observed || GetTickCount64() - Entry->observed > 60000);
        if(Entry->state == WTOP_PROVIDER_VALID)
            wcscpy_s(Process->UserName, _countof(Process->UserName), Entry->name);
    }
}

/* Private sample handles and buffers never escape the collector. */
static BOOL PollProcessList(DWORD UpdateTime)
{
    ULONGLONG ObservationStarted = GetTickCount64();
    OwnerLookupAllowance = 2;
    OwnerLookupDeadline = ObservationStarted + RemainingCollectionWait(400);
    FILETIME SnapshotStarted;
    GetSystemTimeAsFileTime(&SnapshotStarted);
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(Snapshot == INVALID_HANDLE_VALUE) { CollectorError = GetLastError(); return FALSE; }
    DWORD Capacity = PROCLIST_BUF_INCREASE, Count = 0, ObservedCount = 0, UnresolvedOwners = 0;
    BOOL Complete = TRUE;
    process *Sample = calloc(Capacity, sizeof(*Sample));
    if(!Sample) { CloseHandle(Snapshot); CollectorError = ERROR_NOT_ENOUGH_MEMORY; return FALSE; }
    PROCESSENTRY32 Entry;
    memset(&Entry, 0, sizeof(Entry));
    Entry.dwSize = sizeof(Entry);
    BOOL More = Process32First(Snapshot, &Entry);
    if(!More && GetLastError() != ERROR_NO_MORE_FILES) {
        CollectorError = GetLastError(); CloseHandle(Snapshot); free(Sample); return FALSE;
    }
    for(; More && !ShouldStop(); More = Process32Next(Snapshot, &Entry)) {
        process Process;
        memset(&Process, 0, sizeof(Process));
        Process.ID = Entry.th32ProcessID;
        if(!Process.ID) continue;
        if(ObservedCount < MAXDWORD) ++ObservedCount;
        if(SkipProcessRows) continue;
        Process.ThreadCount = Entry.cntThreads;
        Process.BasePriority = Entry.pcPriClassBase;
        Process.ParentPID = Entry.th32ParentProcessID;
        _tcsncpy_s(Process.ExeName, _countof(Process.ExeName), Entry.szExeFile, _TRUNCATE);
        _tcscpy_s(Process.UserName, _countof(Process.UserName), _T("?"));
        Process.OwnerState = Process.CpuState = Process.MemoryState = Process.IoState = Process.AgeState = METRIC_UNAVAILABLE;
        Process.OwnerNameState = WTOP_PROVIDER_UNAVAILABLE;
        if(!MatchesStaticFilter(&Process)) continue;
        if(Count >= MAX_PROCESS_ROWS) { Complete = FALSE; continue; }
        Process.Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Process.ID);
        metric_state OpenState = Process.Handle ? METRIC_OK : LastMetricError();
        if(Process.Handle) {
            FILETIME Created, Exited, Kernel, User;
            if(!GetProcessTimes(Process.Handle, &Created, &Exited, &Kernel, &User)) {
                OpenState = LastMetricError();
                CloseHandle(Process.Handle); Process.Handle = NULL;
            } else if(FileTimeValue(&Created) >= FileTimeValue(&SnapshotStarted)) {
                /* Conservatively exclude processes born after enumeration began:
                   a reused PID must not acquire an earlier row's name/parent. */
                CloseHandle(Process.Handle);
                continue;
            } else Process.CreationTime = FileTimeValue(&Created);
        }
        if(Process.Handle) {
            HANDLE Token;
            if(OpenProcessToken(Process.Handle, TOKEN_QUERY, &Token)) {
                DWORD Size = 0;
                GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
                if(GetLastError() == ERROR_INSUFFICIENT_BUFFER && Size && Size <= 65536UL) {
                    TOKEN_USER *User = malloc(Size);
                    if(User && GetTokenInformation(Token, TokenUser, User, Size, &Size)) {
                        LPWSTR Sid = NULL;
                        if(IsValidSid(User->User.Sid) && ConvertSidToStringSidW(User->User.Sid, &Sid)) {
                            if(wcslen(Sid) < _countof(Process.OwnerSid)) {
                                wcscpy_s(Process.OwnerSid, _countof(Process.OwnerSid), Sid);
                                wcscpy_s(Process.UserName, _countof(Process.UserName), Sid);
                                Process.OwnerState = METRIC_OK;
                                ResolveObservedOwner(&Process);
                            } else Process.OwnerState = WTOP_METRIC_CAPACITY;
                            LocalFree(Sid);
                        } else Process.OwnerState = LastMetricError();
                    } else Process.OwnerState = User ? LastMetricError() : METRIC_UNAVAILABLE;
                    free(User);
                } else Process.OwnerState = LastMetricError();
                CloseHandle(Token);
            } else Process.OwnerState = LastMetricError();
            if(Process.OwnerState != METRIC_OK) _tcscpy_s(Process.UserName, _countof(Process.UserName), _T("?"));
        } else {
            Process.OwnerState = Process.CpuState = Process.MemoryState = Process.IoState = Process.AgeState = OpenState;
        }
        if(FilterByUserName) {
            BOOL SidSelector = !_tcsnicmp(FilterUserName, _T("S-1-"), 4);
            BOOL Known = Process.OwnerState == METRIC_OK &&
                (SidSelector || (Process.OwnerNameState == WTOP_PROVIDER_VALID && !Process.OwnerNameStale));
            if(!Known && UnresolvedOwners < MAXDWORD) ++UnresolvedOwners;
            if(!Known || _tcsicmp(SidSelector ? Process.OwnerSid : Process.UserName, FilterUserName)) {
                if(Process.Handle) CloseHandle(Process.Handle);
                continue;
            }
        }
        if(Count == Capacity) {
            DWORD NewCapacity = Capacity + PROCLIST_BUF_INCREASE;
            process *Larger = realloc(Sample, (size_t)NewCapacity * sizeof(*Sample));
            if(!Larger) {
                if(Process.Handle) CloseHandle(Process.Handle);
                CloseSampleHandles(Sample, Count); free(Sample); CloseHandle(Snapshot);
                CollectorError = ERROR_NOT_ENOUGH_MEMORY; return FALSE;
            }
            Sample = Larger; Capacity = NewCapacity;
        }
        Sample[Count++] = Process;
    }
    DWORD EnumerationError = More || ShouldStop() ? ERROR_SUCCESS : GetLastError();
    CloseHandle(Snapshot);
    if(EnumerationError != ERROR_SUCCESS && EnumerationError != ERROR_NO_MORE_FILES) {
        CloseSampleHandles(Sample, Count); free(Sample); CollectorError = EnumerationError; return FALSE;
    }
    if(ShouldStop()) { CloseSampleHandles(Sample, Count); free(Sample); return FALSE; }

    FILETIME PrevIdle, PrevKernel, PrevUser;
    DWORD CapacityBefore = CurrentCpuCapacity();
    BOOL HaveSystemBefore = GetSystemTimes(&PrevIdle, &PrevKernel, &PrevUser);
    ULONGLONG Started = GetTickCount64();
    for(DWORD i = 0; i < Count && !ShouldStop(); ++i) {
        process *Process = &Sample[i];
        if(!Process->Handle) continue;
        ObserveProcessCpu(Process, &Process->PreviousCpuObservation);
        Process->CpuState = Process->PreviousCpuObservation.state == METRIC_OK ? METRIC_WARMING : Process->PreviousCpuObservation.state;
        IO_COUNTERS Io;
        if(GetProcessIoCounters(Process->Handle, &Io)) {
            Process->PreviousRead = Io.ReadTransferCount;
            Process->PreviousWrite = Io.WriteTransferCount;
            Process->PreviousIoValid = TRUE;
            Process->IoState = METRIC_WARMING;
        } else Process->IoState = LastMetricError();
        Process->SampleTick = GetTickCount64();
    }
    if(WaitForSingleObject(ShutdownEvent, RemainingCollectionWait(UpdateTime)) != WAIT_TIMEOUT) {
        CloseSampleHandles(Sample, Count); free(Sample); return FALSE;
    }
    FILETIME Idle, Kernel, User;
    BOOL HaveSystemAfter = GetSystemTimes(&Idle, &Kernel, &User);
    ULONGLONG SystemObserved = GetTickCount64();
    double HostBusy = 0;
    metric_state HostState = WTOP_METRIC_ERROR;
    if(HaveSystemBefore && HaveSystemAfter)
        HostState = wtop_host_cpu(CounterValue(FileTimeValue(&PrevKernel)), CounterValue(FileTimeValue(&Kernel)),
            CounterValue(FileTimeValue(&PrevUser)), CounterValue(FileTimeValue(&User)),
            CounterValue(FileTimeValue(&PrevIdle)), CounterValue(FileTimeValue(&Idle)), &HostBusy);
    DWORD MachineCapacity = CapacityBefore;
    DWORD Running = 0;
    for(DWORD i = 0; i < Count && !ShouldStop(); ++i) {
        process *Process = &Sample[i];
        if(!Process->Handle) continue;
        wtop_cpu_observation Current;
        wtop_cpu_value Cpu;
        if(ObserveProcessCpu(Process, &Current)) {
            FILETIME Now;
            GetSystemTimeAsFileTime(&Now);
            /* ExitTime is undefined for a running process. AGE is explicitly
               age at observation, never a guessed termination duration. */
            if(FileTimeValue(&Now) >= Process->CreationTime) {
                Process->UpTime = (FileTimeValue(&Now) - Process->CreationTime) / 10000;
                Process->AgeState = METRIC_OK;
            } else Process->AgeState = METRIC_DISCONTINUITY;
            Process->CpuState = wtop_process_cpu(&Process->PreviousCpuObservation, &Current,
                PerformanceFrequency, MachineCapacity, &Cpu);
            Process->CpuElapsedMs = Cpu.elapsed_ms;
            Process->CpuAcquisitionMs = Cpu.acquisition_ms;
            if(Process->CpuState == METRIC_OK && CpuMachine && !MachineCapacity)
                Process->CpuState = WTOP_METRIC_UNSUPPORTED;
            if(Process->CpuState == METRIC_OK) {
                Process->PercentProcessorTime = CpuMachine ? Cpu.machine_percent : Cpu.core_percent;
                if(Process->PercentProcessorTime >= 0.01) ++Running;
            }
        } else Process->CpuState = Process->AgeState = Current.state;
        PROCESS_MEMORY_COUNTERS Memory;
        if(GetProcessMemoryInfo(Process->Handle, &Memory, sizeof(Memory))) {
            Process->UsedMemory = Memory.WorkingSetSize;
            Process->MemoryState = METRIC_OK;
        } else Process->MemoryState = LastMetricError();
        IO_COUNTERS Io;
        if(GetProcessIoCounters(Process->Handle, &Io)) {
            ULONGLONG Elapsed = GetTickCount64() - Process->SampleTick;
            Process->IoReadTotal = Io.ReadTransferCount;
            Process->IoWriteTotal = Io.WriteTransferCount;
            if(Process->PreviousIoValid && Io.ReadTransferCount >= Process->PreviousRead && Io.WriteTransferCount >= Process->PreviousWrite && Elapsed) {
                double Delta = (double)(Io.ReadTransferCount - Process->PreviousRead) + (double)(Io.WriteTransferCount - Process->PreviousWrite);
                wtop_counter_rate(0, Delta, (double)Elapsed, &Process->DiskUsage);
                Process->IoState = METRIC_OK;
            } else Process->IoState = Process->PreviousIoValid ? METRIC_DISCONTINUITY : METRIC_WARMING;
        } else Process->IoState = LastMetricError();
        Process->ObservedTick = GetTickCount64();
        CloseHandle(Process->Handle);
        Process->Handle = NULL;
    }
    CloseSampleHandles(Sample, Count);
    if(ShouldStop()) { free(Sample); return FALSE; }
    DWORD CapacityAfter = CurrentCpuCapacity();
    if(!CapacityBefore || CapacityBefore != CapacityAfter) {
        MachineCapacity = 0;
        if(CpuMachine) for(DWORD i = 0; i < Count; ++i)
            if(Sample[i].CpuState == METRIC_OK) Sample[i].CpuState = WTOP_METRIC_RESET;
    }
    ULONGLONG ObservationFinished = GetTickCount64();
    ULONGLONG SampleElapsed = ObservationFinished - Started;
    pending_sample *Published = calloc(1, sizeof(*Published));
    if(!Published) { free(Sample); CollectorError = ERROR_NOT_ENOUGH_MEMORY; return FALSE; }
    Published->rows = Sample;
    Published->count = Count; Published->observed_count = ObservedCount;
    Published->unresolved_owner_filter = UnresolvedOwners;
    Published->running = Running; Published->capacity = MachineCapacity;
    Published->complete = Complete; Published->interval = UpdateTime;
    Published->cpu_state = HostState; Published->cpu_busy = HostBusy;
    Published->cpu_observed = SystemObserved; Published->started = ObservationStarted;
    Published->observed = ObservationFinished; Published->elapsed = SampleElapsed;
    Published->sequence = ++CollectorSequence;
    Published->published = GetTickCount64();
    EnterCriticalSection(&SyncLock);
    pending_sample *Superseded = PendingSample;
    PendingSample = Published;
    LeaveCriticalSection(&SyncLock);
    FreePendingSample(Superseded);
    return !ShouldStop();
}

static BOOL AdoptPendingSample(void)
{
    EnterCriticalSection(&SyncLock);
    pending_sample *Sample = PendingSample;
    PendingSample = NULL;
    LeaveCriticalSection(&SyncLock);
    if(!Sample) return FALSE;
    DWORD SelectedId = ProcessCount && SelectedProcessIndex < ProcessCount ? ProcessList[SelectedProcessIndex].ID : 0;
    ULONGLONG SelectedCreated = ProcessCount && SelectedProcessIndex < ProcessCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
    free(ProcessList);
    ProcessList = Sample->rows; Sample->rows = NULL;
    ProcessCount = Sample->count;
    ObservedProcessCount = Sample->observed_count;
    UnresolvedOwnerFilterCount = Sample->unresolved_owner_filter;
    SampleComplete = Sample->complete;
    RunningProcessCount = Sample->running;
    CPUUsageState = Sample->cpu_state;
    CPUUsageKnown = CPUUsageState == METRIC_OK;
    if(CPUUsageKnown) CPUUsage = Sample->cpu_busy;
    ActiveCpuCapacity = Sample->capacity;
    CpuSystemObservedTick = Sample->cpu_observed;
    LastSampleElapsed = Sample->elapsed;
    LastSampleStartedTick = Sample->started;
    LastSampleObservedTick = Sample->observed;
    LastSamplePublishedTick = Sample->published;
    LastSampleInterval = Sample->interval;
    SampleSequence = Sample->sequence;
    free(Sample);
    SortProcessList();
    ReadjustCursor();
    if(!FollowProcess && SelectedId) {
        for(DWORD i = 0; i < ProcessCount; ++i) {
            if(ProcessList[i].ID == SelectedId && ProcessList[i].CreationTime == SelectedCreated) {
                SelectProcess(i); break;
            }
        }
    }
    RenderInvalidated = TRUE;
    return TRUE;
}

static void DisableCursor(void)
{
	CONSOLE_CURSOR_INFO CursorInfo;
	GetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
	CursorInfo.bVisible = FALSE; /* Note: this has to be set every time console buffer resizes! */
	SetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
}



/*
 * Returns TRUE on screen buffer resize.
 */
static BOOL PollConsoleInfo(void)
{
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    if(!GetConsoleScreenBufferInfo(ConsoleHandle, &Csbi)) { RequestShutdown(); return FALSE; }
    Width = Csbi.srWindow.Right - Csbi.srWindow.Left + 1;
    Height = Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1;
    BOOL Changed = Width != OldWidth || Height != OldHeight ||
        ConsoleOriginX != Csbi.srWindow.Left || ConsoleOriginY != Csbi.srWindow.Top;
    ConsoleOriginX = Csbi.srWindow.Left;
    ConsoleOriginY = Csbi.srWindow.Top;
    if(Changed) { DisableCursor(); OldWidth = Width; OldHeight = Height; RenderInvalidated = TRUE; wtop_frame_invalidate(&Screen.cells); }
    SizeX = Csbi.dwSize.X; SizeY = Csbi.dwSize.Y;
    if(!SavedAttributes) SavedAttributes = Csbi.wAttributes;
    return Changed;
}

static TCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1] = _T("localhost");
static TCHAR CPUName[256];

static ULONGLONG TotalMemory;
static ULONGLONG UsedMemory;
static double UsedMemoryPerc;

static ULONGLONG TotalPageMemory;
static ULONGLONG UsedPageMemory;
static double UsedPageMemoryPerc;

#define TO_MB(X) ((X)/1048576)

static ULONGLONG UpTime;

static BOOL SystemMemoryKnown;
static metric_state SystemMemoryState = WTOP_METRIC_WARMING_UP;
static metric_state SystemCommitState = WTOP_METRIC_WARMING_UP;
static ULONGLONG PhysicalTotalBytes, PhysicalAvailableBytes, CommitBytes, CommitLimitBytes;
static ULONGLONG SystemMemoryObservedTick;

static void PollInitialSystemInfo(void)
{
    SYSTEM_INFO SystemInfo;
    GetNativeSystemInfo(&SystemInfo);
    CPUCoreCount = SystemInfo.dwNumberOfProcessors;
    GetAllActiveProcessors = (active_cpu_fn)GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "GetActiveProcessorCount");
    LARGE_INTEGER Frequency;
    if(QueryPerformanceFrequency(&Frequency) && Frequency.QuadPart > 0)
        PerformanceFrequency = (double)Frequency.QuadPart;
    _tcscpy_s(CPUName, _countof(CPUName), _T("Unknown CPU"));
    HKEY Key;
    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"), 0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS) {
        DWORD Count = sizeof(CPUName), Type = 0;
        if(RegQueryValueEx(Key, _T("ProcessorNameString"), NULL, &Type, (LPBYTE)CPUName, &Count) != ERROR_SUCCESS || Type != REG_SZ)
            _tcscpy_s(CPUName, _countof(CPUName), _T("Unknown CPU"));
        CPUName[_countof(CPUName) - 1] = 0;
        RegCloseKey(Key);
    }
    DWORD ComputerNameSize = _countof(ComputerName);
    GetComputerName(ComputerName, &ComputerNameSize);
}

static void PollSystemInfo(void)
{
    MEMORYSTATUSEX MemoryInfo;
    memset(&MemoryInfo, 0, sizeof(MemoryInfo));
    MemoryInfo.dwLength = sizeof(MemoryInfo);
    SystemMemoryKnown = GlobalMemoryStatusEx(&MemoryInfo);
    SystemMemoryState = SystemMemoryKnown ? METRIC_OK : LastMetricError();
    if(SystemMemoryKnown && MemoryInfo.ullAvailPhys > MemoryInfo.ullTotalPhys) {
        SystemMemoryKnown = FALSE; SystemMemoryState = WTOP_METRIC_RESET;
    }
    if(SystemMemoryKnown) {
        PhysicalTotalBytes = MemoryInfo.ullTotalPhys;
        PhysicalAvailableBytes = MemoryInfo.ullAvailPhys;
        TotalMemory = TO_MB(PhysicalTotalBytes);
        UsedMemory = TO_MB(PhysicalTotalBytes - PhysicalAvailableBytes);
        UsedMemoryPerc = PhysicalTotalBytes ? (double)(PhysicalTotalBytes - PhysicalAvailableBytes) / (double)PhysicalTotalBytes : 0;
    }
    PERFORMANCE_INFORMATION Performance;
    memset(&Performance, 0, sizeof(Performance));
    Performance.cb = sizeof(Performance);
    if(GetPerformanceInfo(&Performance, sizeof(Performance))) {
        wtop_counter Bytes, Limit;
        wtop_counter PageSize = CounterValue((ULONGLONG)Performance.PageSize);
        if(PageSize.high) SystemCommitState = WTOP_METRIC_CAPACITY;
        else SystemCommitState = wtop_commit_bytes(CounterValue((ULONGLONG)Performance.CommitTotal),
            CounterValue((ULONGLONG)Performance.CommitLimit), (unsigned long)Performance.PageSize,
            &Bytes, &Limit, &UsedPageMemoryPerc);
        if(SystemCommitState == METRIC_OK) {
            CommitBytes = NativeCounter(Bytes); CommitLimitBytes = NativeCounter(Limit);
            UsedPageMemory = TO_MB(CommitBytes); TotalPageMemory = TO_MB(CommitLimitBytes);
        }
    } else SystemCommitState = LastMetricError();
    SystemMemoryObservedTick = GetTickCount64();
    UpTime = GetTickCount64();
}

DWORD WINAPI PollProcessListThreadProc(LPVOID lpParam)
{
	UNREFERENCED_PARAMETER(lpParam);

	while(!ShouldStop()) {
        if(InterlockedCompareExchange(&CollectionPaused, 0, 0)) { WaitForSingleObject(ShutdownEvent, 50); continue; }
        DWORD Interval = (DWORD)InterlockedCompareExchange(&CollectorInterval, 0, 0);
        if(!PollProcessList(Interval)) {
            RequestShutdown();
            break;
        }
    }
	return 0;
}

#define TIME_STR_SIZE 32

/* Process age and system uptime are elapsed wall time, not consumed CPU time. */
static void FormatTimeString(TCHAR *Buffer, DWORD BufferSize, ULONGLONG MS)
{
	int Seconds = (int)(MS / 1000 % 60);
	int Minutes = (int)(MS / 60000 % 60);
	int Hours = (int)(MS / 3600000 % 24);
	int Days = (int)(MS / 86400000);
	_stprintf_s(Buffer, BufferSize, _T("%02d:%02d:%02d:%02d"), Days, Hours, Minutes, Seconds);
}


static void WriteBlankLine(void)
{
	ConPrintf(_T("%*c"), Width, _T(' '));
}

/* System-owned callback thread. Events remain alive until process exit. */
static BOOL WINAPI CtrlHandler(DWORD Signal)
{
    if(Signal != CTRL_C_EVENT && Signal != CTRL_BREAK_EVENT && Signal != CTRL_CLOSE_EVENT &&
       Signal != CTRL_LOGOFF_EVENT && Signal != CTRL_SHUTDOWN_EVENT) return FALSE;
    RequestShutdown();
    if(CleanupDoneEvent && WaitForSingleObject(CleanupDoneEvent, SHUTDOWN_GRACE_MS + 500) != WAIT_OBJECT_0) {
        /* Covers a blocked initial/snapshot query on the main thread too.
           The callback owns no model memory and never attempts thread teardown. */
        OutputDebugString(_T("wtop: cancellation cleanup exceeded 3500 ms; forcing own process exit 124.\n"));
        TerminateProcess(GetCurrentProcess(), EXIT_FORCED_SHUTDOWN);
    }
    return TRUE;
}

static void DrawProcessListHeader(void)
{
    wtop_tab *Tab = ActiveTab();
    wtop_frame_fill(&Screen.cells, 0, ProcessHeaderRow, Width, ' ', (WORD)Settings.colors[WTOP_COLOR_HEADER]);
    RenderedColumnCount = wtop_ui_columns(Width, Tab->columns, Tab->column_count, RenderedColumns);
    for(int i = 0; i < RenderedColumnCount; ++i) {
        wtop_ui_column_span *Span = &RenderedColumns[i];
        SetConCursorPos((SHORT)Span->start, (SHORT)ProcessHeaderRow);
        SetColor((WORD)((int)ProcessSortType == Span->id ? Settings.colors[WTOP_COLOR_BG_HIGHLIGHT] : Settings.colors[WTOP_COLOR_HEADER]));
        TCHAR *Name = FromUtf8(wtop_column_name(Span->id));
        if(Name) { DrawPaddedText(Name, Span->width); free(Name); }
    }
}

#define BAR_WIDTH 25

static int DrawPercentageBar(TCHAR *Name, double Percentage, WORD Color)
{
	int CharsWritten = 0;

	SetColor((WORD)Settings.colors[WTOP_COLOR_METER_LABEL]);
	CharsWritten += ConPrintf(_T("  %s"), Name);
	SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
	ConPutc(_T('['));
	CharsWritten++;

	BOOL Known = Percentage >= 0;
    if(Known) Percentage = min(1.0, Percentage);
    int BarWidth = Settings.meter_style == WTOP_METER_TEXT ? 0 : min(Settings.meter_width, max(8, Width - 15));
    int Bars = Known ? (int)((double)BarWidth * Percentage) : 0;
	SetColor(Color);
	for(int i = 0; i < Bars; i++) {
		ConPutc(Settings.meter_style == WTOP_METER_BLOCKS ? (Settings.character_set == WTOP_CHARSET_UNICODE ? (TCHAR)0x2588 : _T('#')) : _T('|'));
	}
	CharsWritten+= Bars;
	SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
	for(int i = 0; i < BarWidth - Bars; i++) {
		ConPutc(_T(' '));
	}
	CharsWritten += BarWidth - Bars;
	SetColor((WORD)Settings.colors[WTOP_COLOR_FG_HIGHLIGHT]);
	CharsWritten += Known ? ConPrintf(_T("%04.1f%%"), 100.0 * Percentage) : ConPrintf(_T("    ?"));
	SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
	ConPutc(_T(']'));
	CharsWritten++;
	return CharsWritten;
}

static void RestoreConsole(void)
{
	SetConsoleActiveScreenBuffer(OldConsoleHandle);
}

static void WriteProcessInfo(const process *Process, BOOL Highlighted)
{
    ULONGLONG Now = GetTickCount64();
    metric_state Cpu = FreshMetricState(Process->CpuState, Process->CpuObservedTick, Now);
    metric_state Memory = FreshMetricState(Process->MemoryState, Process->ObservedTick, Now);
    metric_state Io = FreshMetricState(Process->IoState, Process->ObservedTick, Now);
    metric_state Age = FreshMetricState(Process->AgeState, Process->ObservedTick, Now);
    WORD Color = (WORD)Settings.colors[WTOP_COLOR_FG];
    if(IsProcessTagged(Process)) Color = (WORD)Settings.colors[WTOP_COLOR_TAG];
    if(Highlighted) Color = (WORD)Settings.colors[FollowProcess ? WTOP_COLOR_FOLLOW : WTOP_COLOR_BG_HIGHLIGHT];
    SetColor(Color);
    int Row = RenderRow;
    wtop_frame_fill(&Screen.cells, 0, Row, Width, ' ', Color);
    for(int i = 0; i < RenderedColumnCount; ++i) {
        const wtop_ui_column_span *Span = &RenderedColumns[i];
        TCHAR Text[512];
        char Metric[32];
        Text[0] = 0;
        switch(Span->id) {
        case WTOP_COLUMN_ID: _stprintf_s(Text, _countof(Text), _T("%lu"), Process->ID); break;
        case WTOP_COLUMN_USER:
            _stprintf_s(Text, _countof(Text), _T("%s%s"), Process->OwnerNameStale ? _T("~") : _T(""), Process->UserName);
            break;
        case WTOP_COLUMN_PRIORITY:
            if(Process->BasePriority <= 999) _stprintf_s(Text, _countof(Text), _T("%lu"), Process->BasePriority);
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_CPU:
            if(Cpu == METRIC_OK && Process->PercentProcessorTime < 999.95) _stprintf_s(Text, _countof(Text), _T("%.1f"), Process->PercentProcessorTime);
            else if(Cpu == METRIC_OK) _tcscpy_s(Text, _countof(Text), _T(">999"));
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_MEMORY:
            if(Memory == METRIC_OK) {
                wtop_ui_format_bytes((double)Process->UsedMemory, FALSE, Metric, sizeof(Metric));
                _stprintf_s(Text, _countof(Text), _T("%hs"), Metric);
            }
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_THREADS:
            if(Process->ThreadCount <= 9999) _stprintf_s(Text, _countof(Text), _T("%lu"), Process->ThreadCount);
            else _tcscpy_s(Text, _countof(Text), _T(">999"));
            break;
        case WTOP_COLUMN_IO:
            if(Io == METRIC_OK) {
                wtop_ui_format_bytes(Process->DiskUsage, TRUE, Metric, sizeof(Metric));
                _stprintf_s(Text, _countof(Text), _T("%hs"), Metric);
            }
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_AGE:
            if(Age == METRIC_OK) {
                wtop_ui_format_age((double)Process->UpTime, Metric, sizeof(Metric));
                _stprintf_s(Text, _countof(Text), _T("%hs"), Metric);
            }
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_PROCESS: _tcscpy_s(Text, _countof(Text), Process->ExeName); break;
        }
        SetConCursorPos((SHORT)Span->start, (SHORT)Row);
        int Space = Span->width;
        if(Span->id == WTOP_COLUMN_PROCESS && ProcessSortType == SORT_BY_TREE) {
            DWORD Depth = min(Process->TreeDepth, (DWORD)max(0, (Space - 1) / 3));
            for(DWORD d = 0; d < Depth; ++d) { ConPrintf(_T("|  ")); Space -= 3; }
        }
        DrawPaddedText(Text, Space);
    }
}

static BOOL RedrawAtCursor = FALSE;

typedef enum scroll_type { SCROLL_UP, SCROLL_DOWN, SCROLL_PAGE_UP, SCROLL_PAGE_DOWN } scroll_type;

static void DoScroll(scroll_type Type, BOOL *Redraw)
{
    if(!ViewCount) return;
    FollowProcess = FALSE;
    DWORD Page = max(VisibleProcessCount, 1);
    DWORD Index = SelectedViewIndex;
    if(Type == SCROLL_UP && Index) --Index;
    if(Type == SCROLL_DOWN && Index + 1 < ViewCount) ++Index;
    if(Type == SCROLL_PAGE_UP) Index = Index > Page ? Index - Page : 0;
    if(Type == SCROLL_PAGE_DOWN) Index += min(Page, ViewCount - 1 - Index);
    SelectView(Index);
    *Redraw = TRUE;
}

#include "command_help.inc"

static int PrintPaths(const wtop_options *Options)
{
    wtop_store_result Result;
    if(!ResolveConfigStore(Options, &Result)) {
        fprintf(stderr, "wtop: path resolution %s: %s (Win32 %lu)\n",
            wtop_store_outcome_name(Result.outcome), Result.message, Result.native_error);
        return 1;
    }
    char *Module = ToUtf8(ConfigStore.module_path), *Read = ToUtf8(ConfigStore.read_path), *Write = ToUtf8(ConfigStore.write_path);
    if(!Module || !Read || !Write) { free(Module); free(Read); free(Write); return 1; }
    if(Options->json) {
        printf("{\"schema\":1,\"kind\":\"settings-paths\",\"owner\":\"%s\",\"source\":\"%s\",\"saving_disabled\":%s,\"canonical_aliases\":%s,\"module\":",
            wtop_store_owner_name(ConfigStore.owner), wtop_store_source_name(ConfigStore.source),
            ConfigStore.disabled || Options->readonly ? "true" : "false", ConfigStore.canonical_aliases ? "true" : "false");
        wtop_json_string(stdout, Module); fputs(",\"read\":", stdout); wtop_json_string(stdout, Read);
        fputs(",\"write\":", stdout); wtop_json_string(stdout, Write); puts("}");
    } else printf("Owner: %s\nSource: %s\nModule: %s\nRead: %s\nSave: %s\nSaving disabled: %s\n",
        wtop_store_owner_name(ConfigStore.owner), wtop_store_source_name(ConfigStore.source), Module,
        *Read ? Read : "compiled defaults", *Write ? Write : "unavailable",
        ConfigStore.disabled || Options->readonly ? "yes" : "no");
    free(Module); free(Read); free(Write);
    return fflush(stdout) || ferror(stdout) ? 1 : 0;
}

static int PrintInformation(const wtop_options *Options)
{
    if(Options->action == WTOP_HELP) {
        if(Options->json) { fputs("{\"schema\":1,\"help\":", stdout); wtop_json_string(stdout, HelpText); puts("}"); }
        else fputs(HelpText, stdout);
    } else if(Options->action == WTOP_VERSION) {
        if(Options->json) printf("{\"schema\":1,\"name\":\"wtop\",\"version\":\"%s\",\"profile\":\"%s\"}\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
        else printf("Wtop %s (%s)\nDerived from NTop; Copyright (c) 2019 Gian Sass and Wtop contributors.\nGPL-3.0-only. Historical OS support is not verified.\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
    } else {
        if(Options->json) printf("{\"schema\":1,\"kind\":\"build-capabilities\",\"version\":\"%s\",\"profile\":\"%s\",\"runtime_probe\":false,\"snapshot_schema\":2,\"tui\":true,\"snapshot\":true,\"query\":true,\"inspect\":true,\"log\":true,\"settings_schema\":2,\"optional_providers\":[\"command_line\",\"services\",\"per_core\",\"account_name\"],\"gui\":false,\"install\":false,\"gpu\":false,\"native_gui\":false,\"encoding\":\"utf-8\",\"metrics\":[\"cpu_percent\",\"working_set_bytes\",\"io_read_write_bytes_per_second\",\"age_ms\",\"owner\"]}\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
        else printf("Wtop %s: build capabilities (%s; no runtime probe)\nTUI with htop/vi/Emacs keys, optional mouse, setup and explicit config save.\nText/JSON schema-2 snapshots, query/inspect, finite JSONL logging.\nOptional isolated command-line, service, account-name and per-core providers.\nVersion-2 settings, ownership/path reports, contextual Setup and action results.\nMetric access varies by process rights; unavailable fields carry status/null.\nNo self-install, GUI or GPU telemetry. Delivery candidates and historical probes\nare separate development tools; their target support is unqualified.\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}

int GetProcessSortTypeFromName(const TCHAR *Name, process_sort_type *Dest)
{
    char *Utf8 = ToUtf8(Name);
    int Column = WTOP_COLUMN_ID;
    BOOL Ok = Utf8 && wtop_parse_column(Utf8, &Column);
    free(Utf8);
    if(Ok) *Dest = (process_sort_type)Column;
    return Ok;
}

static void ApplyActiveTab(void)
{
    DWORD Id = ViewCount ? ProcessList[SelectedProcessIndex].ID : 0;
    ULONGLONG Created = ViewCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
    ProcessSortType = ActiveTab()->tree ? SORT_BY_TREE : (process_sort_type)ActiveTab()->sort_column;
    SortOrder = ActiveTab()->descending ? DESCENDING : ASCENDING;
    SortProcessList();
    ReadjustCursor();
    if(Id) for(DWORD i = 0; i < ProcessCount; ++i)
        if(ProcessList[i].ID == Id && ProcessList[i].CreationTime == Created) { SelectProcess(i); break; }
    RenderInvalidated = TRUE;
}

void ChangeProcessSortType(process_sort_type NewProcessSortType)
{
    ActiveTab()->tree = NewProcessSortType == SORT_BY_TREE;
    if(NewProcessSortType != SORT_BY_TREE) ActiveTab()->sort_column = (int)NewProcessSortType;
    MarkSettingsChanged();
    ApplyActiveTab();
}

static vi_message_type CurrentViMessageType = VI_NOTICE;
static TCHAR *ViMessage;
static ULONGLONG ViMessageTick;

void SetViMessage(vi_message_type MessageType, TCHAR *Fmt, ...)
{
	CurrentViMessageType = MessageType;
    ViMessageTick = GetTickCount64();

	va_list VaList;

	va_start(VaList, Fmt);
	_vsntprintf_s(ViMessage, DEFAULT_STR_SIZE, _TRUNCATE, Fmt, VaList);
	va_end(VaList);
}

static BOOL ViMessageActive(void)
{
    if(!ViMessage) return FALSE;
    if(CurrentViMessageType == VI_NOTICE && GetTickCount64() - ViMessageTick > 5000)
        ViMessage[0] = 0;
    return ViMessage[0] != 0;
}

static void WriteStatusAt(int Row, int Column)
{
    if(Row < 0 || Column >= Width) return;
    SetConCursorPos((SHORT)Column, (SHORT)Row);
    wtop_ui_status Status;
    memset(&Status, 0, sizeof(Status));
    char *Prompt = ViMessageActive() ? ToUtf8(ViMessage) : NULL;
    Status.prompt = Prompt;
    Status.view = ActiveTab()->name;
    Status.filter = ActiveTab()->filter;
    Status.sort = ActiveTab()->tree ? "tree" : wtop_column_name(ActiveTab()->sort_column);
    Status.descending = ActiveTab()->descending;
    Status.dirty = ConfigDirty;
    Status.sample_available = SampleSequence != 0;
    ULONGLONG Now = GetTickCount64();
    ULONGLONG Age = Now >= LastSampleObservedTick ? Now - LastSampleObservedTick : 0;
    Status.age_ms = Age > ULONG_MAX ? ULONG_MAX : (unsigned long)Age;
    Status.stale = SampleSequence && LastSampleInterval && Age > LastSampleInterval * 3UL;
    Status.partial = CollectorError != 0 || !SampleComplete || UnresolvedOwnerFilterCount != 0;
    char ProviderNotice[128];
    _snprintf_s(ProviderNotice, sizeof(ProviderNotice), _TRUNCATE, "%s%s%s",
        DisplayTextFlags & (WTOP_TEXT_LOSSY | WTOP_TEXT_INVALID) ? "glyph substitution; " : "",
        ReadOnlyMode ? "read-only; " : "",
        ProcessSortType == SORT_BY_TREE && TreeUnresolvedParentCount ? "unverified parents shown as roots" : "");
    Status.provider_notice = ProviderNotice[0] ? ProviderNotice : NULL;
    Status.observed_rows = ObservedProcessCount;
    Status.selected_rows = ViewCount;
    char Text[4096]; unsigned Flags;
    wtop_ui_status_format(&Status, Width - Column, Text, sizeof(Text), &Flags);
    SetColor((WORD)Settings.colors[(Prompt && CurrentViMessageType == VI_ERROR) || Status.stale ? WTOP_COLOR_ERROR : WTOP_COLOR_FG]);
    if(Paused) ConPrintf(_T("PAUSED "));
    ConPrintf(_T("%hs"), Text);
    free(Prompt);
}

static void WriteVi(void) { WriteStatusAt(StatusRow, 0); }

void ClearViMessage(void)
{
    if(ViMessage) ViMessage[0] = 0;
}

static wtop_action_result *LastActionResults;
static DWORD LastActionCount;
static void ShowActionResults(void);

/* Handle ownership is synchronous and local. No presentation calls occur in
   the executor; every target keeps its own outcome and captured Win32 error. */
static void ExecuteNativeAction(void *Context, const wtop_action_request *Request,
                                wtop_action_result *Result)
{
    static const DWORD Classes[] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS,
        NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS };
    BOOL *LocalShutdown = Context;
    DWORD Pid = Request->target.pid;
    if(ReadOnlyMode) { Result->state = WTOP_ACTION_DENIED; Result->native_error = ERROR_ACCESS_DENIED; return; }
    if(!Request->target.creation.high && !Request->target.creation.low) {
        Result->state = WTOP_ACTION_IDENTITY_UNAVAILABLE; return;
    }
    DWORD Access = PROCESS_QUERY_LIMITED_INFORMATION;
    if(Request->kind == WTOP_ACTION_TERMINATE) Access |= PROCESS_TERMINATE;
    else if(Request->kind == WTOP_ACTION_PRIORITY_UP || Request->kind == WTOP_ACTION_PRIORITY_DOWN)
        Access |= PROCESS_SET_INFORMATION;
    else { Result->state = WTOP_ACTION_UNSUPPORTED; return; }
    /* Synchronization permits a nonblocking confirmation. Its absence must not
       remove an action otherwise allowed by the target's access policy. */
    HANDLE Handle = OpenProcess(Access | SYNCHRONIZE, FALSE, Pid);
    BOOL CanWait = Handle != NULL;
    if(!Handle) Handle = OpenProcess(Access, FALSE, Pid);
    if(!Handle) {
        Result->native_error = GetLastError();
        Result->state = Result->native_error == ERROR_ACCESS_DENIED ? WTOP_ACTION_DENIED : WTOP_ACTION_FAILED;
        return;
    }
    FILETIME Created, Exited, Kernel, User;
    if(!GetProcessTimes(Handle, &Created, &Exited, &Kernel, &User)) {
        Result->native_error = GetLastError();
        Result->state = Result->native_error == ERROR_ACCESS_DENIED ? WTOP_ACTION_DENIED : WTOP_ACTION_IDENTITY_UNAVAILABLE;
    } else if(FileTimeValue(&Created) != NativeCounter(Request->target.creation)) {
        Result->state = WTOP_ACTION_IDENTITY_CHANGED;
    } else if(CanWait && WaitForSingleObject(Handle, 0) == WAIT_OBJECT_0) {
        Result->state = WTOP_ACTION_ALREADY_EXITED;
    } else if(Request->kind == WTOP_ACTION_TERMINATE) {
        if(Pid == GetCurrentProcessId()) {
            *LocalShutdown = TRUE;
            Result->local_shutdown = 1;
            Result->state = WTOP_ACTION_REQUESTED;
        } else if(TerminateProcess(Handle, 9)) {
            Result->state = WTOP_ACTION_REQUESTED;
            if(CanWait) {
                DWORD Wait = WaitForSingleObject(Handle, 0);
                if(Wait == WAIT_OBJECT_0) Result->state = WTOP_ACTION_CONFIRMED_EXITED;
                else if(Wait == WAIT_FAILED) Result->native_error = GetLastError();
            }
        } else {
            Result->native_error = GetLastError();
            Result->state = Result->native_error == ERROR_ACCESS_DENIED ? WTOP_ACTION_DENIED : WTOP_ACTION_FAILED;
        }
    } else {
        DWORD Current = GetPriorityClass(Handle);
        Result->previous_priority = Current;
        if(!Current) {
            Result->native_error = GetLastError();
            Result->state = Result->native_error == ERROR_ACCESS_DENIED ? WTOP_ACTION_DENIED : WTOP_ACTION_FAILED;
        } else {
            int Position = -1;
            for(int i = 0; i < (int)_countof(Classes); ++i) if(Classes[i] == Current) Position = i;
            int Next = Position + (Request->kind == WTOP_ACTION_PRIORITY_UP ? 1 : -1);
            if(Position < 0 || Next < 0 || Next >= (int)_countof(Classes)) Result->state = WTOP_ACTION_SAFE_LIMIT;
            else {
                Result->requested_priority = Classes[Next];
                if(SetPriorityClass(Handle, Classes[Next])) Result->state = WTOP_ACTION_APPLIED;
                else {
                    Result->native_error = GetLastError();
                    Result->state = Result->native_error == ERROR_ACCESS_DENIED ? WTOP_ACTION_DENIED : WTOP_ACTION_FAILED;
                }
            }
        }
    }
    CloseHandle(Handle);
}

static int StopActionBatch(void *Context)
{
    return ShouldStop() || *(BOOL *)Context;
}

static BOOL AllocationFits(size_t Count, size_t ItemSize)
{
    return ItemSize && Count <= (size_t)-1 / ItemSize;
}

static BOOL RunNativeActions(const process_identity *Identities, DWORD Count, wtop_action_kind Kind)
{
    if(!Count || Count > INT_MAX || !AllocationFits(Count, sizeof(wtop_action_request)) ||
       !AllocationFits(Count, sizeof(wtop_action_result))) {
        SetViMessage(VI_ERROR, _T("Action batch exceeds local capacity")); return FALSE;
    }
    wtop_action_request *Requests = calloc(Count, sizeof(*Requests));
    wtop_action_result *Results = calloc(Count, sizeof(*Results));
    if(!Requests || !Results) {
        free(Requests); free(Results);
        SetViMessage(VI_ERROR, _T("No actions attempted: cannot allocate result storage")); return FALSE;
    }
    for(DWORD i = 0; i < Count; ++i) {
        Requests[i].target.pid = Identities[i].ID;
        Requests[i].target.creation = CounterValue(Identities[i].CreationTime);
        Requests[i].kind = Kind;
    }
    BOOL LocalShutdown = FALSE;
    wtop_actions_run(Requests, Count, Results, Count, ExecuteNativeAction, StopActionBatch, &LocalShutdown);
    free(Requests);
    free(LastActionResults);
    LastActionResults = Results;
    LastActionCount = Count;
    DWORD Accepted = 0, Pending = 0;
    for(DWORD i = 0; i < Count; ++i) {
        Accepted += wtop_action_success(Results[i].state);
        Pending += Results[i].state == WTOP_ACTION_REQUESTED;
    }
    SetViMessage(Accepted == Count ? VI_NOTICE : VI_ERROR,
        _T("Actions: %lu accepted (%lu pending), %lu refused/failed; per-target results retained"),
        Accepted, Pending, Count - Accepted);
    ShowActionResults();
    if(LocalShutdown) RequestShutdown();
    return Accepted == Count;
}

int KillObservedProcesses(const DWORD *Pids, size_t Count)
{
    if(!Pids || !Count || Count > INT_MAX || Count > (size_t)-1 / sizeof(process_identity)) return 0;
    process_identity *Identities = calloc(Count, sizeof(*Identities));
    if(!Identities) { SetViMessage(VI_ERROR, _T("No actions attempted: out of memory")); return 0; }
    for(size_t i = 0; i < Count; ++i) {
        Identities[i].ID = Pids[i];
        for(DWORD j = 0; j < ProcessCount; ++j)
            if(ProcessList[j].ID == Pids[i]) { Identities[i].CreationTime = ProcessList[j].CreationTime; break; }
    }
    int Ok = RunNativeActions(Identities, (DWORD)Count, WTOP_ACTION_TERMINATE);
    free(Identities);
    return Ok;
}

int KillObservedProcess(DWORD Pid)
{
    return KillObservedProcesses(&Pid, 1);
}

static ULONGLONG CaretTicks;
static BOOLEAN CaretState;

static void ResetCaret(void)
{
	CaretTicks = GetTickCount64();
	CaretState = TRUE;
}



static BOOL SelectionMatchesDisplay(void)
{
    if(ViewCount && !ProcessList[SelectedProcessIndex].CreationTime) {
        SetViMessage(VI_ERROR, _T("Cannot act on this selection: creation identity is unavailable"));
        return FALSE;
    }
    if(ViewCount && ProcessList[SelectedProcessIndex].ID == DisplayedProcessID &&
       ProcessList[SelectedProcessIndex].CreationTime == DisplayedCreationTime) return TRUE;
    SetViMessage(VI_ERROR, _T("Process selection changed; review the refreshed row before acting"));
    return FALSE;
}

typedef enum overlay_type { OVERLAY_NONE, OVERLAY_HELP, OVERLAY_SETUP, OVERLAY_SORT, OVERLAY_KILL, OVERLAY_RESULTS, OVERLAY_TAB_DELETE } overlay_type;
static overlay_type Overlay;
static int SetupPage, SetupIndex, OverlayTop, SortChoice;
static int EmacsPrefix;
static char EditingOriginalFilter[WTOP_FILTER_BYTES];
static BOOL EditingFilter, EditingWasDirty;
static unsigned long EditingTabId, PendingDeleteTabId;
static int SetupCount(void);
static void SetupAdjust(int Direction, BOOL Activate);
static process_identity *PendingKills;
static DWORD PendingKillCount;
static DWORD LastMouseButtons;
static int MouseWheelRemainder;

static wtop_ui_context_options CurrentUiContext(void)
{
    wtop_ui_context_options Context;
    wtop_ui_context Kind = WTOP_CONTEXT_PROCESS;
    if(InInputMode) Kind = WTOP_CONTEXT_EDITOR;
    else if(Overlay == OVERLAY_HELP) Kind = WTOP_CONTEXT_HELP;
    else if(Overlay == OVERLAY_SETUP) Kind = WTOP_CONTEXT_SETUP;
    else if(Overlay == OVERLAY_SORT) Kind = WTOP_CONTEXT_SORT;
    else if(Overlay == OVERLAY_KILL || Overlay == OVERLAY_TAB_DELETE) Kind = WTOP_CONTEXT_CONFIRM;
    else if(Overlay == OVERLAY_RESULTS) Kind = WTOP_CONTEXT_RESULTS;
    wtop_ui_context_defaults(&Context, Kind);
    Context.readonly = ReadOnlyMode;
    Context.has_selection = TaggedProcessListCount || ViewCount;
    Context.identity_known = TaggedProcessListCount || (ViewCount && ProcessList[SelectedProcessIndex].CreationTime);
    Context.kill_supported = Context.priority_supported = 1;
    Context.save_available = !ConfigStore.disabled && ConfigStore.write_path[0];
    Context.setup_tabs = Overlay == OVERLAY_SETUP && SetupPage == 3;
    Context.can_add_tab = Settings.tab_count < WTOP_MAX_TABS && Settings.next_tab_id != 0;
    int VisibleViews = 0;
    for(int i = 0; i < Settings.tab_count; ++i) VisibleViews += Settings.tabs[i].visible != 0;
    Context.can_delete_tab = Settings.tab_count > 1 &&
        (SetupIndex >= 0 && SetupIndex < Settings.tab_count) && (!Settings.tabs[SetupIndex].visible || VisibleViews > 1);
    Context.can_move_left = SetupIndex > 0;
    Context.can_move_right = SetupIndex + 1 < Settings.tab_count;
    if(Overlay == OVERLAY_KILL) {
        Context.has_selection = PendingKillCount != 0;
        Context.identity_known = PendingKillCount != 0;
        for(DWORD i = 0; i < PendingKillCount; ++i)
            if(!PendingKills[i].CreationTime) Context.identity_known = 0;
    } else if(Overlay == OVERLAY_TAB_DELETE) {
        /* Editing an in-memory view is permitted in read-only mode; saving it
           is still forbidden by the common store/action policy. */
        Context.readonly = 0;
        Context.has_selection = Context.identity_known = PendingDeleteTabId != 0;
    }
    return Context;
}

static void ConfigureMouse(void)
{
    /* ReadConsoleInput needs raw key events. LINE_INPUT intercepts Ctrl+S as
       console output pause before our Emacs search binding can receive it.
       Keep processed Ctrl+C handling; cleanup restores the exact caller mode. */
    DWORD Mode = (OriginalInputMode | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) &
        ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    Mode &= ~ENABLE_MOUSE_INPUT;
    MouseEnabled = FALSE;
    if(Settings.mouse != WTOP_MOUSE_OFF && SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), Mode | ENABLE_MOUSE_INPUT)) MouseEnabled = TRUE;
    else if(!SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), Mode))
        SetViMessage(VI_ERROR, _T("Console input mode could not be configured; keyboard fallback retained"));
    else if(Settings.mouse == WTOP_MOUSE_ON)
        SetViMessage(VI_NOTICE, _T("Mouse unavailable in this console; keyboard controls remain active"));
}

int SavePresentationSettings(void)
{
    if(ReadOnlyMode) { SetViMessage(VI_ERROR, _T("Configuration writes disabled by --readonly")); return 0; }
    if(ConfigStore.disabled) { SetViMessage(VI_ERROR, _T("Saving disabled by --no-config; use --write-config PATH to export")); return 0; }
    if(!ConfigStore.write_path[0]) { SetViMessage(VI_ERROR, _T("Configuration destination unavailable; select --config PATH or --portable")); return 0; }
    unsigned long CapturedRevision = SettingsRevision;
    wtop_store_result Result;
    BOOL Saved = wtop_store_save(&ConfigStore, &Settings, &ConfigDocument, CapturedRevision, &Result);
    ReportSettingsRecovery(&ConfigStore, &Result);
    if(!Saved) {
        SetViMessage(VI_ERROR, _T("Settings %hs: %hs (error %lu); edits retained%hs"),
            wtop_store_outcome_name(Result.outcome), Result.message, Result.native_error,
            ConfigStore.recovery_temporary[0] || ConfigStore.recovery_backup[0] ? "; recovery paths in invoking stderr" : "");
        return 0;
    }
    if(SettingsRevision == Result.saved_revision) ConfigDirty = FALSE;
    if(Result.native_error)
        SetViMessage(VI_ERROR, _T("Saved configuration with warning: %hs; full recovery paths in invoking stderr"), Result.message);
    else SetViMessage(VI_NOTICE, _T("Saved configuration: %s%hs"), ConfigStore.write_path,
        Result.migrated ? " (original retained; path in invoking stderr)" : "");
    return 1;
}

static void CloseOverlay(void)
{
    free(PendingKills); PendingKills = NULL; PendingKillCount = 0;
    Overlay = OVERLAY_NONE; OverlayTop = 0; RenderInvalidated = TRUE;
}

static void ShowActionResults(void)
{
    CloseOverlay();
    Overlay = OVERLAY_RESULTS;
}

static void BeginKill(void)
{
    if(ReadOnlyMode) { SetViMessage(VI_ERROR, _T("Process actions disabled by --readonly")); return; }
    CloseOverlay();
    if(!TaggedProcessListCount && !SelectionMatchesDisplay()) return;
    DWORD Count = TaggedProcessListCount ? TaggedProcessListCount : 1;
    PendingKills = malloc((size_t)Count * sizeof(*PendingKills));
    if(!PendingKills) { SetViMessage(VI_ERROR, _T("Cannot prepare termination: out of memory")); return; }
    if(TaggedProcessListCount) memcpy(PendingKills, TaggedProcessList, Count * sizeof(*PendingKills));
    else { PendingKills[0].ID = DisplayedProcessID; PendingKills[0].CreationTime = DisplayedCreationTime; }
    PendingKillCount = Count;
    Overlay = OVERLAY_KILL;
}

static void ChangeTab(int Direction)
{
    int Index = Settings.active_tab;
    for(int Step = 0; Step < Settings.tab_count; ++Step) {
        Index = (Index + Settings.tab_count + Direction) % Settings.tab_count;
        if(Settings.tabs[Index].visible) { Settings.active_tab = Index; MarkSettingsChanged(); ApplyActiveTab(); return; }
    }
}

static void StartEditor(TCHAR Prefix)
{
    CloseOverlay();
    EditingFilter = Prefix == _T('\\');
    if(EditingFilter) {
        strcpy_s(EditingOriginalFilter, sizeof(EditingOriginalFilter), ActiveTab()->filter);
        EditingWasDirty = ConfigDirty;
    }
    ViEnableInput(Prefix);
    ResetCaret();
}

/* Every action path (keyboard and clickable function buttons) owns this
   transition. Other mouse controls are ignored while text is being edited. */
static void CancelEditor(void)
{
    if(!InInputMode) return;
    if(EditingFilter) {
        strcpy_s(ActiveTab()->filter, sizeof(ActiveTab()->filter), EditingOriginalFilter);
        ConfigDirty = EditingWasDirty;
        ReadjustCursor();
    }
    EditingFilter = FALSE;
    EditingTabId = 0;
    ViDisableInput();
    ClearViMessage();
}

static int FindTabById(unsigned long Id)
{
    for(int i = 0; i < Settings.tab_count; ++i) if(Settings.tabs[i].id == Id) return i;
    return -1;
}

static BOOL AcceptEditor(void)
{
    if(EditingTabId) {
        unsigned TextFlags = 0;
        char *Name = ToUtf8WithStatus(CurrentInputStr + 1, &TextFlags);
        const char *Error = NULL;
        int Index = FindTabById(EditingTabId);
        BOOL Ok = Name && !TextFlags && Index >= 0 && wtop_settings_tab_rename(&Settings, Index, Name, &Error);
        free(Name);
        if(!Ok) {
            SetViMessage(VI_ERROR, _T("View not renamed: %hs"), Error ? Error : "invalid text or changed identity");
            return FALSE;
        }
        MarkSettingsChanged(); EditingTabId = 0; ViDisableInput();
    } else {
        ViExecInput();
        EditingFilter = FALSE;
    }
    RenderInvalidated = TRUE;
    return TRUE;
}

static void EditSetupTab(wtop_ui_action Action)
{
    const char *Error = NULL;
    BOOL Ok = FALSE;
    int Index = SetupIndex;
    if(Index < 0 || Index >= Settings.tab_count) return;
    if(Action == WTOP_UI_TAB_RENAME) {
        TCHAR *Name = FromUtf8(Settings.tabs[Index].name);
        if(Name && ViSetInputText(_T('='), Name)) EditingTabId = Settings.tabs[Index].id;
        else SetViMessage(VI_ERROR, _T("Cannot open the view-name editor"));
        free(Name); return;
    }
    if(Action == WTOP_UI_TAB_DELETE) {
        PendingDeleteTabId = Settings.tabs[Index].id;
        Overlay = OVERLAY_TAB_DELETE; return;
    }
    if(Action == WTOP_UI_TAB_NEW || Action == WTOP_UI_TAB_DUPLICATE) {
        char Name[WTOP_TAB_NAME_BYTES];
        sprintf_s(Name, sizeof(Name), "%s %lu", Action == WTOP_UI_TAB_NEW ? "View" : "Copy", Settings.next_tab_id);
        int Destination = Index + 1;
        Ok = Action == WTOP_UI_TAB_NEW ?
            wtop_settings_tab_insert(&Settings, Destination, Name, &Error) :
            wtop_settings_tab_duplicate(&Settings, Index, Destination, Name, &Error);
        if(Ok) { SetupIndex = Settings.active_tab = Destination; }
    } else {
        int Destination = Index + (Action == WTOP_UI_TAB_MOVE_RIGHT ? 1 : -1);
        Ok = wtop_settings_tab_move(&Settings, Index, Destination, &Error);
        if(Ok) SetupIndex = Destination;
    }
    if(Ok) { MarkSettingsChanged(); ApplyActiveTab(); }
    else SetViMessage(VI_ERROR, _T("View unchanged: %hs"), Error ? Error : "operation unavailable");
}

static BOOL NavigateOverlay(wtop_ui_action Action)
{
    if(Overlay != OVERLAY_HELP && Overlay != OVERLAY_RESULTS &&
       Overlay != OVERLAY_SETUP && Overlay != OVERLAY_SORT) return FALSE;
    int *Position = Overlay == OVERLAY_SETUP ? &SetupIndex : Overlay == OVERLAY_SORT ? &SortChoice : &OverlayTop;
    int Maximum = Overlay == OVERLAY_SETUP ? SetupCount() - 1 :
        Overlay == OVERLAY_SORT ? WTOP_MAX_COLUMNS - 1 :
        Overlay == OVERLAY_RESULTS ? (int)LastActionCount - 1 : 18;
    Maximum = max(0, Maximum);
    switch(Action) {
    case WTOP_UI_UP: --*Position; break;
    case WTOP_UI_DOWN: ++*Position; break;
    case WTOP_UI_PAGE_UP: *Position -= max(1, Height - 5); break;
    case WTOP_UI_PAGE_DOWN: *Position += max(1, Height - 5); break;
    case WTOP_UI_TOP: *Position = 0; break;
    case WTOP_UI_BOTTOM: *Position = Maximum; break;
    default: return FALSE;
    }
    *Position = max(0, min(Maximum, *Position));
    return TRUE;
}

static void DispatchAction(wtop_ui_action Action, BOOL *Redraw)
{
    if(Action == WTOP_UI_NONE) return;
    wtop_ui_context_options Context = CurrentUiContext();
    wtop_ui_binding Binding;
    wtop_ui_resolve_action(&Context, Action, &Binding);
    if(!Binding.enabled) {
        SetViMessage(VI_NOTICE, _T("%hs: %hs"), Binding.label, wtop_ui_reason_name(Binding.reason));
        *Redraw = TRUE; return;
    }
    *Redraw = TRUE;
    BOOL WasEditing = InInputMode;
    if(InInputMode && (Action == WTOP_UI_ACCEPT || Action == WTOP_UI_SAVE)) {
        if(!AcceptEditor() || Action == WTOP_UI_ACCEPT) return;
    } else if(InInputMode && Action == WTOP_UI_CLEAR) {
        KEY_EVENT_RECORD Key = {0}; Key.bKeyDown = TRUE; Key.wVirtualKeyCode = 'U'; Key.dwControlKeyState = LEFT_CTRL_PRESSED;
        ViHandleInputKey(&Key); return;
    } else if(InInputMode) CancelEditor();
    if(NavigateOverlay(Action)) { RenderInvalidated = TRUE; return; }
    switch(Action) {
    case WTOP_UI_UP: DoScroll(SCROLL_UP, Redraw); break;
    case WTOP_UI_DOWN: DoScroll(SCROLL_DOWN, Redraw); break;
    case WTOP_UI_PAGE_UP: DoScroll(SCROLL_PAGE_UP, Redraw); break;
    case WTOP_UI_PAGE_DOWN: DoScroll(SCROLL_PAGE_DOWN, Redraw); break;
    case WTOP_UI_TOP: FollowProcess = FALSE; SelectView(0); break;
    case WTOP_UI_BOTTOM: FollowProcess = FALSE; SelectView(ViewCount ? ViewCount - 1 : 0); break;
    case WTOP_UI_HELP: CloseOverlay(); Overlay = OVERLAY_HELP; break;
    case WTOP_UI_SETUP: CloseOverlay(); Overlay = OVERLAY_SETUP; SetupPage = SetupIndex = 0; break;
    case WTOP_UI_SEARCH: StartEditor(_T('/')); break;
    case WTOP_UI_FILTER: StartEditor(_T('\\')); break;
    case WTOP_UI_COMMAND: StartEditor(_T(':')); break;
    case WTOP_UI_TREE: ActiveTab()->tree = !ActiveTab()->tree; MarkSettingsChanged(); ApplyActiveTab(); break;
    case WTOP_UI_SORT: CloseOverlay(); Overlay = OVERLAY_SORT; SortChoice = ActiveTab()->sort_column; break;
    case WTOP_UI_PRIORITY_UP:
    case WTOP_UI_PRIORITY_DOWN:
        if(ReadOnlyMode) { SetViMessage(VI_ERROR, _T("Process actions disabled by --readonly")); break; }
        if(TaggedProcessListCount) {
            RunNativeActions(TaggedProcessList, TaggedProcessListCount,
                Action == WTOP_UI_PRIORITY_UP ? WTOP_ACTION_PRIORITY_UP : WTOP_ACTION_PRIORITY_DOWN);
        } else if(SelectionMatchesDisplay()) {
            process_identity Identity = { DisplayedProcessID, DisplayedCreationTime };
            RunNativeActions(&Identity, 1,
                Action == WTOP_UI_PRIORITY_UP ? WTOP_ACTION_PRIORITY_UP : WTOP_ACTION_PRIORITY_DOWN);
        }
        break;
    case WTOP_UI_KILL: BeginKill(); break;
    case WTOP_UI_QUIT: RequestShutdown(); break;
    case WTOP_UI_TAG:
        if(SelectionMatchesDisplay()) { ToggleTaggedProcess(&ProcessList[SelectedProcessIndex]); DoScroll(SCROLL_DOWN, Redraw); }
        break;
    case WTOP_UI_UNTAG: TaggedProcessListCount = 0; break;
    case WTOP_UI_REVERSE: ActiveTab()->descending = !ActiveTab()->descending; MarkSettingsChanged(); ApplyActiveTab(); break;
    case WTOP_UI_FOLLOW:
        if(SelectionMatchesDisplay()) {
            FollowProcess = !FollowProcess; FollowProcessID = DisplayedProcessID; FollowCreationTime = DisplayedCreationTime;
        }
        break;
    case WTOP_UI_SEARCH_NEXT: SearchNext(); break;
    case WTOP_UI_SEARCH_PREVIOUS: SearchPrevious(); break;
    case WTOP_UI_NEXT_TAB: ChangeTab(1); break;
    case WTOP_UI_PREVIOUS_TAB: ChangeTab(-1); break;
    case WTOP_UI_PAUSE:
        Paused = !Paused; InterlockedExchange(&CollectionPaused, Paused); break;
    case WTOP_UI_BAR: Settings.show_function_bar = !Settings.show_function_bar; MarkSettingsChanged(); break;
    case WTOP_UI_CANCEL:
        if(Overlay == OVERLAY_TAB_DELETE) { CloseOverlay(); Overlay = OVERLAY_SETUP; SetupPage = 3; PendingDeleteTabId = 0; }
        else if(!WasEditing) CloseOverlay();
        ClearViMessage(); EmacsPrefix = FALSE; break;
    case WTOP_UI_SORT_CPU: ChangeProcessSortType(SORT_BY_PROCESSOR_TIME); break;
    case WTOP_UI_SORT_MEMORY: ChangeProcessSortType(SORT_BY_USED_MEMORY); break;
    case WTOP_UI_SORT_PID: ChangeProcessSortType(SORT_BY_ID); break;
    case WTOP_UI_SORT_LEFT: ChangeProcessSortType((process_sort_type)((ActiveTab()->sort_column + WTOP_MAX_COLUMNS - 1) % WTOP_MAX_COLUMNS)); break;
    case WTOP_UI_SORT_RIGHT: ChangeProcessSortType((process_sort_type)((ActiveTab()->sort_column + 1) % WTOP_MAX_COLUMNS)); break;
    case WTOP_UI_EMACS_PREFIX: EmacsPrefix = TRUE; SetViMessage(VI_NOTICE, _T("C-x: C-c quits, C-s saves; C-g cancels")); break;
    case WTOP_UI_SAVE: SavePresentationSettings(); break;
    case WTOP_UI_SETUP_PREVIOUS_PAGE:
    case WTOP_UI_SETUP_NEXT_PAGE:
        SetupPage = (SetupPage + (Action == WTOP_UI_SETUP_NEXT_PAGE ? 1 : 4)) % 5;
        SetupIndex = OverlayTop = 0; break;
    case WTOP_UI_ADJUST_DOWN: SetupAdjust(-1, FALSE); break;
    case WTOP_UI_ADJUST_UP: SetupAdjust(1, FALSE); break;
    case WTOP_UI_ACCEPT:
        if(Overlay == OVERLAY_SETUP) SetupAdjust(1, TRUE);
        else if(Overlay == OVERLAY_SORT) { ChangeProcessSortType((process_sort_type)SortChoice); CloseOverlay(); }
        break;
    case WTOP_UI_CONFIRM:
        if(Overlay == OVERLAY_KILL) {
            RunNativeActions(PendingKills, PendingKillCount, WTOP_ACTION_TERMINATE);
            TaggedProcessListCount = 0;
        } else if(Overlay == OVERLAY_TAB_DELETE) {
            const char *Error = NULL;
            int Index = -1;
            for(int i = 0; i < Settings.tab_count; ++i)
                if(Settings.tabs[i].id == PendingDeleteTabId) Index = i;
            if(Index >= 0 && wtop_settings_tab_delete(&Settings, Index, &Error)) {
                MarkSettingsChanged(); ApplyActiveTab(); SetupIndex = min(Index, Settings.tab_count - 1);
            } else SetViMessage(VI_ERROR, _T("View not deleted: %hs"), Error ? Error : "view identity changed");
            CloseOverlay(); Overlay = OVERLAY_SETUP; SetupPage = 3; PendingDeleteTabId = 0;
        }
        break;
    case WTOP_UI_TAB_NEW:
    case WTOP_UI_TAB_DUPLICATE:
    case WTOP_UI_TAB_RENAME:
    case WTOP_UI_TAB_DELETE:
    case WTOP_UI_TAB_MOVE_LEFT:
    case WTOP_UI_TAB_MOVE_RIGHT:
        EditSetupTab(Action); break;
    default: break;
    }
    if(*Redraw) RenderInvalidated = TRUE;
}

static int OrderedItems(const int *Active, int Count, int Maximum, int *Items)
{
    int Used = 0;
    for(int i = 0; i < Count; ++i) Items[Used++] = Active[i];
    for(int i = 0; i < Maximum; ++i) {
        BOOL Found = FALSE;
        for(int j = 0; j < Count; ++j) if(Active[j] == i) Found = TRUE;
        if(!Found) Items[Used++] = i;
    }
    return Used;
}

static void ToggleOrderedItem(int *Items, int *Count, int Selected, int Maximum, BOOL AllowEmpty)
{
    int All[WTOP_MAX_COLUMNS];
    OrderedItems(Items, *Count, Maximum, All);
    if(Selected < *Count) {
        if(*Count == 1 && !AllowEmpty) { SetViMessage(VI_ERROR, _T("At least one process column must remain visible")); return; }
        memmove(Items + Selected, Items + Selected + 1, (size_t)(*Count - Selected - 1) * sizeof(*Items));
        --*Count;
    } else Items[(*Count)++] = All[Selected];
    MarkSettingsChanged();
}

static int SetupCount(void)
{
    switch(SetupPage) {
    case 0: return 12;
    case 1: return WTOP_HEADER_ROWS;
    case 2: return WTOP_MAX_COLUMNS;
    case 3: return Settings.tab_count;
    default: return WTOP_COLOR_COUNT;
    }
}

static void SetupAdjust(int Direction, BOOL Activate)
{
    if(SetupPage == 0) {
        switch(SetupIndex) {
        case 0: Settings.keymap = (wtop_keymap)((Settings.keymap + 3 + Direction) % 3); break;
        case 1: wtop_settings_set_theme(&Settings, (wtop_theme)((Settings.theme + 3 + Direction) % 3)); break;
        case 2: Settings.mouse = (wtop_mouse)((Settings.mouse + 3 + Direction) % 3); ConfigureMouse(); break;
        case 3: Settings.show_function_bar = !Settings.show_function_bar; break;
        case 4: Settings.show_tabs = !Settings.show_tabs; break;
        case 5:
            if(Direction > 0) Settings.interval_ms = min(60000, Settings.interval_ms + 100);
            else Settings.interval_ms = Settings.interval_ms > 150 ? Settings.interval_ms - 100 : 50;
            break;
        case 6: Settings.footer_style = (wtop_footer_style)((Settings.footer_style + 3 + Direction) % 3); break;
        case 7: Settings.status_position = (wtop_status_position)((Settings.status_position + 3 + Direction) % 3); break;
        case 8: Settings.frame_interval_ms = (unsigned long)max(16, min(1000, (int)Settings.frame_interval_ms + 10 * Direction)); break;
        case 9: Settings.meter_width = max(8, min(80, Settings.meter_width + Direction)); break;
        case 10: Settings.meter_style = (wtop_meter_style)((Settings.meter_style + 3 + Direction) % 3); break;
        case 11: Settings.character_set = (wtop_character_set)(1 - Settings.character_set); break;
        }
    } else if(SetupPage == 1 || SetupPage == 2) {
        int *Items = SetupPage == 1 ? Settings.header_rows : ActiveTab()->columns;
        int *Count = SetupPage == 1 ? &Settings.header_count : &ActiveTab()->column_count;
        int Maximum = SetupPage == 1 ? WTOP_HEADER_ROWS : WTOP_MAX_COLUMNS;
        if(Activate) ToggleOrderedItem(Items, Count, SetupIndex, Maximum, SetupPage == 1);
        else if(SetupIndex < *Count) {
            int Next = SetupIndex + Direction;
            if(Next >= 0 && Next < *Count) {
                int Item = Items[SetupIndex]; Items[SetupIndex] = Items[Next]; Items[Next] = Item; SetupIndex = Next;
            }
        }
    } else if(SetupPage == 3) {
        const char *Error = NULL;
        BOOL Ok;
        if(Activate) Ok = wtop_settings_tab_set_visible(&Settings, SetupIndex, !Settings.tabs[SetupIndex].visible, &Error);
        else {
            int Next = SetupIndex + Direction;
            Ok = wtop_settings_tab_move(&Settings, SetupIndex, Next, &Error);
            if(Ok) SetupIndex = Next;
        }
        if(!Ok) { SetViMessage(VI_ERROR, _T("View unchanged: %hs"), Error); return; }
        ApplyActiveTab();
    } else Settings.colors[SetupIndex] = (Settings.colors[SetupIndex] + 256 + Direction) % 256;
    MarkSettingsChanged();
    RenderInvalidated = TRUE;
}

static void HandleOverlayKey(int Key, unsigned Modifiers, BOOL *Redraw)
{
    wtop_ui_context_options Context = CurrentUiContext();
    DispatchAction(wtop_ui_context_key_action(&Context, Settings.keymap, Key, Modifiers), Redraw);
}

static void DispatchFunction(int Index, BOOL *Redraw)
{
    wtop_ui_context_options Context = CurrentUiContext();
    wtop_ui_binding Binding;
    wtop_ui_resolve_function(&Context, Index, &Binding);
    if(Binding.enabled) DispatchAction(Binding.action, Redraw);
    else if(Binding.action != WTOP_UI_NONE) {
        SetViMessage(VI_NOTICE, _T("%hs: %hs"), Binding.label, wtop_ui_reason_name(Binding.reason));
        *Redraw = TRUE;
    }
}

static int NativeKey(const KEY_EVENT_RECORD *Key)
{
    switch(Key->wVirtualKeyCode) {
    case VK_UP: return WTOP_KEY_UP;
    case VK_DOWN: return WTOP_KEY_DOWN;
    case VK_LEFT: return WTOP_KEY_LEFT;
    case VK_RIGHT: return WTOP_KEY_RIGHT;
    case VK_PRIOR: return WTOP_KEY_PAGE_UP;
    case VK_NEXT: return WTOP_KEY_PAGE_DOWN;
    case VK_HOME: return WTOP_KEY_HOME;
    case VK_END: return WTOP_KEY_END;
    case VK_ESCAPE: return 27;
    case VK_RETURN: return '\r';
    case VK_TAB: return '\t';
    default:
        if(Key->wVirtualKeyCode >= VK_F1 && Key->wVirtualKeyCode <= VK_F10)
            return WTOP_KEY_F1 + Key->wVirtualKeyCode - VK_F1;
        return Key->uChar.UnicodeChar;
    }
}

static void HandleMouse(const MOUSE_EVENT_RECORD *Mouse, BOOL *Redraw)
{
    DWORD Buttons = Mouse->dwButtonState & 0xffff;
    BOOL Click = !Mouse->dwEventFlags && (Buttons & FROM_LEFT_1ST_BUTTON_PRESSED) && !(LastMouseButtons & FROM_LEFT_1ST_BUTTON_PRESSED);
    LastMouseButtons = Buttons;
    if(!MouseEnabled || RenderInvalidated) return;
    int X = Mouse->dwMousePosition.X - ConsoleOriginX, Y = Mouse->dwMousePosition.Y - ConsoleOriginY;
    if(InInputMode && !(Click && Y == FunctionBarRow)) return;
    if(Mouse->dwEventFlags == MOUSE_WHEELED) {
        MouseWheelRemainder += (SHORT)HIWORD(Mouse->dwButtonState);
        int Steps = MouseWheelRemainder / WHEEL_DELTA;
        MouseWheelRemainder %= WHEEL_DELTA;
        Steps = max(-32, min(32, Steps));
        for(int i = 0; i < abs(Steps) * 3; ++i) {
            if(Overlay == OVERLAY_SETUP) SetupIndex = max(0, min(SetupCount() - 1, SetupIndex + (Steps > 0 ? -1 : 1)));
            else if(Overlay == OVERLAY_SORT) SortChoice = max(0, min(WTOP_MAX_COLUMNS - 1, SortChoice + (Steps > 0 ? -1 : 1)));
            else if(Overlay == OVERLAY_NONE) DoScroll(Steps > 0 ? SCROLL_UP : SCROLL_DOWN, Redraw);
        }
        *Redraw = TRUE; return;
    }
    if(!Click || X < 0 || X >= Width || Y < 0 || Y >= Height) return;
    if(Y == FunctionBarRow) {
        for(int i = 0; i < 10; ++i) if(FooterWidths[i] && X >= FooterStarts[i] && X - FooterStarts[i] < FooterWidths[i]) {
            DispatchFunction(i, Redraw); break;
        }
        return;
    }
    if(Overlay == OVERLAY_SETUP && Y >= 3) {
        int Index = OverlayTop + Y - 3;
        if(Index < SetupCount() && Y < Height - 2) { SetupIndex = Index; SetupAdjust(1, TRUE); *Redraw = TRUE; }
        return;
    }
    if(Overlay == OVERLAY_SORT && Y >= 3 && Y < Height - 2 && OverlayTop + Y - 3 < WTOP_MAX_COLUMNS) {
        ChangeProcessSortType((process_sort_type)(OverlayTop + Y - 3)); CloseOverlay(); *Redraw = TRUE; return;
    }
    if(Overlay != OVERLAY_NONE) return;
    if(Y == TabsRow) {
        for(int i = 0; i < Settings.tab_count; ++i)
            if(RenderedTabWidth[i] && X >= RenderedTabStart[i] && X - RenderedTabStart[i] < RenderedTabWidth[i]) {
                Settings.active_tab = i; MarkSettingsChanged(); ApplyActiveTab(); *Redraw = TRUE; return;
            }
    }
    if(Y == ProcessHeaderRow) {
        for(int i = 0; i < RenderedColumnCount; ++i) {
            const wtop_ui_column_span *Span = &RenderedColumns[i];
            if(X >= Span->start && X - Span->start < Span->width) {
                if(ActiveTab()->sort_column == Span->id) ActiveTab()->descending = !ActiveTab()->descending;
                ChangeProcessSortType((process_sort_type)Span->id); *Redraw = TRUE; return;
            }
        }
    }
    if(Y >= ProcessWindowPosY && (DWORD)(Y - ProcessWindowPosY) < RenderedRowCount) {
        const process_identity *Identity = &RenderedRows[Y - ProcessWindowPosY];
        for(DWORD i = 0; i < ProcessCount; ++i) {
            if(ProcessList[i].ID == Identity->ID && ProcessList[i].CreationTime == Identity->CreationTime) {
                FollowProcess = FALSE; SelectProcess(i); *Redraw = TRUE; return;
            }
        }
        SetViMessage(VI_NOTICE, _T("Clicked process has exited; refresh before acting")); *Redraw = TRUE;
    }
}

static void ProcessInput(BOOL *Redraw)
{
    DWORD Available = 0, Count = 0;
    HANDLE Input = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD Records[128];
    if(!GetNumberOfConsoleInputEvents(Input, &Available)) { RequestShutdown(); return; }
    if(!Available) return;
    if(!ReadConsoleInput(Input, Records, min(Available, (DWORD)_countof(Records)), &Count)) { RequestShutdown(); return; }
    for(DWORD i = 0; i < Count && !ShouldStop(); ++i) {
        if(Records[i].EventType == WINDOW_BUFFER_SIZE_EVENT) { RenderInvalidated = TRUE; *Redraw = TRUE; continue; }
        if(Records[i].EventType == MOUSE_EVENT) { if(PollConsoleInfo()) *Redraw = TRUE; HandleMouse(&Records[i].Event.MouseEvent, Redraw); continue; }
        if(Records[i].EventType != KEY_EVENT || !Records[i].Event.KeyEvent.bKeyDown) continue;
        KEY_EVENT_RECORD *Event = &Records[i].Event.KeyEvent;
        unsigned Modifiers = 0;
        if(Event->dwControlKeyState & SHIFT_PRESSED) Modifiers |= WTOP_MOD_SHIFT;
        if(Event->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) Modifiers |= WTOP_MOD_CTRL;
        if(Event->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) Modifiers |= WTOP_MOD_ALT;
        int Key = NativeKey(Event);
        if(Key >= WTOP_KEY_F1 && Key <= WTOP_KEY_F10) {
            DispatchFunction(Key - WTOP_KEY_F1, Redraw); continue;
        }
        if(InInputMode) {
            if(Key == '\r') { AcceptEditor(); *Redraw = TRUE; continue; }
            BOOL Cancel = Key == 27 || ((Modifiers & WTOP_MOD_CTRL) && (Key == 7 || Key == 'g' || Key == 'G'));
            ClearViMessage();
            if(ViHandleInputKey(Event)) {
                if(EditingFilter && Cancel) {
                    strcpy_s(ActiveTab()->filter, sizeof(ActiveTab()->filter), EditingOriginalFilter);
                    ConfigDirty = EditingWasDirty; ReadjustCursor();
                } else if(InInputMode && CurrentInputStr[0] == _T('\\')) SetPresentationFilter(CurrentInputStr + 1);
                if(!InInputMode) { EditingFilter = FALSE; EditingTabId = 0; }
                *Redraw = TRUE; RenderInvalidated = TRUE;
            }
            continue;
        }
        if(Overlay != OVERLAY_NONE) { HandleOverlayKey(Key, Modifiers, Redraw); continue; }
        if(EmacsPrefix) {
            EmacsPrefix = FALSE;
            if((Modifiers & WTOP_MOD_CTRL) && (Key == 3 || Key == 'c' || Key == 'C')) DispatchAction(WTOP_UI_QUIT, Redraw);
            else if((Modifiers & WTOP_MOD_CTRL) && (Key == 19 || Key == 's' || Key == 'S')) DispatchAction(WTOP_UI_SAVE, Redraw);
            else { ClearViMessage(); *Redraw = TRUE; }
            continue;
        }
        wtop_ui_context_options Context = CurrentUiContext();
        wtop_ui_action Action = wtop_ui_context_key_action(&Context, Settings.keymap, Key, Modifiers);
        unsigned Repeats = (Action == WTOP_UI_UP || Action == WTOP_UI_DOWN || Action == WTOP_UI_PAGE_UP || Action == WTOP_UI_PAGE_DOWN) ? min(Event->wRepeatCount, 32) : 1;
        for(unsigned Repeat = 0; Repeat < Repeats; ++Repeat) DispatchAction(Action, Redraw);
    }
}

static BOOL ApplyOptions(const wtop_options *Options)
{
    if(Options->mode == WTOP_INSPECT) {
        PidFilterList[0] = (DWORD)Options->inspect_pid;
        PidFilterCount = 1; FilterByPID = TRUE;
    }
    if(Options->user_filter) {
        TCHAR *Value = FromUtf8(Options->user_filter);
        if(!Value || _tcslen(Value) >= _countof(FilterUserName)) { free(Value); return FALSE; }
        _tcscpy_s(FilterUserName, _countof(FilterUserName), Value);
        free(Value); FilterByUserName = TRUE;
    }
    if(Options->pid_filter) {
        const char *Cursor = Options->pid_filter;
        while(*Cursor) {
            const char *Comma = strchr(Cursor, ',');
            size_t Length = Comma ? (size_t)(Comma - Cursor) : strlen(Cursor);
            char Token[32];
            unsigned long Pid;
            if(!Length || Length >= sizeof(Token) || PidFilterCount == _countof(PidFilterList)) return FALSE;
            memcpy(Token, Cursor, Length); Token[Length] = 0;
            if(!wtop_parse_unsigned(Token, 1, 4294967295UL, 10, &Pid)) return FALSE;
            PidFilterList[PidFilterCount++] = (DWORD)Pid;
            if(!Comma) break;
            Cursor = Comma + 1;
            if(!*Cursor) return FALSE;
        }
        FilterByPID = TRUE;
    }
    if(Options->name_filter) {
        TCHAR *Value = FromUtf8(Options->name_filter);
        if(!Value) return FALSE;
        TCHAR *Cursor = Value;
        while(*Cursor) {
            TCHAR *Comma = _tcschr(Cursor, _T(','));
            size_t Length = Comma ? (size_t)(Comma - Cursor) : _tcslen(Cursor);
            if(!Length || Length > MAX_NAMEPARTSIZE || NameFilterCount == MAX_NAMEPARTS) { free(Value); return FALSE; }
            _tcsncpy_s(NameFilterList[NameFilterCount++], MAX_NAMEPARTSIZE + 1, Cursor, Length);
            if(!Comma) break;
            Cursor = Comma + 1;
            if(!*Cursor) { free(Value); return FALSE; }
        }
        free(Value); FilterByName = TRUE;
    }
    return TRUE;
}

static unsigned SnapshotTextFlags;
static BOOL JsonTString(FILE *Output, const TCHAR *Text)
{
    unsigned Flags = 0;
    char *Utf8 = ToUtf8WithStatus(Text, &Flags);
    SnapshotTextFlags |= Flags;
    if(!Utf8) return FALSE;
    BOOL Ok = wtop_json_string(Output, Utf8);
    free(Utf8);
    return Ok;
}

/* A freshness budget is an observation policy, not a native-call timeout.
   It never implies that a blocked synchronous provider can be cancelled. */
static DWORD SampleFreshnessBudget(void)
{
    return LastSampleInterval ? LastSampleInterval * 3UL : 0;
}

static metric_state FreshMetricState(metric_state State, ULONGLONG Observed, ULONGLONG Now)
{
    double Age = 0;
    if(State != METRIC_OK) return State;
    return wtop_sample_freshness(CounterValue(Now), CounterValue(Observed), SampleFreshnessBudget(), &Age);
}

static void JsonMetricDouble(FILE *Output, const char *Name, metric_state State, double Value, const char *Unit)
{
    fprintf(Output, ",\"%s\":{\"status\":\"%s\",\"unit\":\"%s\",\"value\":", Name, MetricStateName(State), Unit);
    if(State == METRIC_OK) fprintf(Output, "%.6f", Value); else fputs("null", Output);
    fputc('}', Output);
}

static void JsonMetricInteger(FILE *Output, const char *Name, metric_state State, ULONGLONG Value, const char *Unit)
{
    fprintf(Output, ",\"%s\":{\"status\":\"%s\",\"unit\":\"%s\",\"value\":", Name, MetricStateName(State), Unit);
    if(State == METRIC_OK) fprintf(Output, "\"%llu\"", (unsigned long long)Value); else fputs("null", Output);
    fputc('}', Output);
}

static wtop_provider_session InspectionProvider;
static wtop_provider_result *CommandObservation, *ServicesObservation;

static int CollectInspection(const wtop_options *Options)
{
    if(ShouldStop()) return 130;
    if(Options->mode == WTOP_INSPECT && !ProcessCount) return 3;
    int ExitCode = Options->mode == WTOP_QUERY && UnresolvedOwnerFilterCount ? 3 : 0;
    wtop_provider_request Request;
    memset(&Request, 0, sizeof(Request));
    if(Options->mode == WTOP_INSPECT) {
        Request.pid = (DWORD)Options->inspect_pid;
        Request.creation.dwLowDateTime = (DWORD)ProcessList[0].CreationTime;
        Request.creation.dwHighDateTime = (DWORD)(ProcessList[0].CreationTime >> 32);
    }
    if(Options->command_line) {
        CommandObservation = calloc(1, sizeof(*CommandObservation));
        if(!CommandObservation) return 1;
        Request.kind = WTOP_PROVIDER_COMMAND_LINE;
        wtop_provider_collect(&InspectionProvider, &Request, 2500, ShutdownEvent, CommandObservation);
        if(ShouldStop()) return 130;
        if(CommandObservation->state != WTOP_PROVIDER_VALID) ExitCode = 3;
    }
    if(Options->services || (Options->mode == WTOP_QUERY && Options->query_target == WTOP_QUERY_SERVICES)) {
        ServicesObservation = calloc(1, sizeof(*ServicesObservation));
        if(!ServicesObservation) return 1;
        Request.kind = WTOP_PROVIDER_SERVICES;
        wtop_provider_collect(&InspectionProvider, &Request, 2500, ShutdownEvent, ServicesObservation);
        if(ShouldStop()) return 130;
        if(ServicesObservation->state != WTOP_PROVIDER_VALID) ExitCode = 3;
    }
    if(Options->per_core && (!CoreObservation || CoreObservation->state != WTOP_PROVIDER_VALID)) ExitCode = 3;
    return ExitCode;
}

static BOOL WriteProvider(FILE *Output, const char *Name, const wtop_provider_result *Result, BOOL *First)
{
    if(!Result) return TRUE;
    if(!*First) fputc(',', Output);
    *First = FALSE;
    fprintf(Output, "\"%s\":{\"status\":\"%s\",\"error_domain\":%d,\"native_error\":%lu,"
        "\"started_ms\":\"%llu\",\"observed_ms\":\"%llu\",\"elapsed_ms\":%.6f,\"acquisition_ms\":%.6f,"
        "\"retirement_confirmed\":%s,\"response_received\":%s,\"pid\":%lu,\"creation_filetime\":",
        Name, wtop_provider_state_name(Result->state), (int)Result->error_domain, Result->native_error,
        (unsigned long long)Result->started_ms, (unsigned long long)Result->observed_ms, Result->elapsed_ms, Result->acquisition_ms,
        Result->retirement_confirmed ? "true" : "false", Result->response_received ? "true" : "false", Result->pid);
    if(FileTimeValue(&Result->creation)) fprintf(Output, "\"%llu\"", (unsigned long long)FileTimeValue(&Result->creation));
    else fputs("null", Output);
    fprintf(Output, ",\"count\":%lu,\"total_count\":%lu", Result->count, Result->total_count);
    if(Result->kind == WTOP_PROVIDER_PER_CORE)
        fprintf(Output, ",\"scope\":\"%s\"", Result->legacy_processor_scope ? "legacy-processor" : "processor-information");
    fputs(",\"value\":", Output);
    if(Result->kind == WTOP_PROVIDER_COMMAND_LINE) {
        if(Result->state == WTOP_PROVIDER_VALID) {
            if(!JsonTString(Output, Result->data.command_line)) return FALSE;
        } else fputs("null", Output);
    } else if(Result->state == WTOP_PROVIDER_VALID || Result->state == WTOP_PROVIDER_CAPACITY) {
        fputc('[', Output);
        DWORD Limit = Result->kind == WTOP_PROVIDER_SERVICES ? WTOP_PROVIDER_MAX_SERVICES : WTOP_PROVIDER_MAX_CORES;
        for(DWORD i = 0; i < Result->count && i < Limit; ++i) {
            if(i) fputc(',', Output);
            if(Result->kind == WTOP_PROVIDER_SERVICES) {
                const wtop_provider_service *Service = &Result->data.services[i];
                fputs("{\"name\":", Output); if(!JsonTString(Output, Service->name)) return FALSE;
                fputs(",\"display_name\":", Output); if(!JsonTString(Output, Service->display_name)) return FALSE;
                fprintf(Output, ",\"pid\":%lu,\"service_state\":%lu,\"identity_status\":\"%s\",\"creation_filetime\":",
                    Service->pid, Service->service_state, wtop_provider_state_name(Service->identity_state));
                if(Service->identity_state == WTOP_PROVIDER_VALID)
                    fprintf(Output, "\"%llu\"", (unsigned long long)FileTimeValue(&Service->creation));
                else fputs("null", Output);
            } else {
                const wtop_provider_core *Core = &Result->data.cores[i];
                fputs("{\"instance\":", Output); if(!JsonTString(Output, Core->instance)) return FALSE;
                fputs(",\"group\":", Output); if(Core->group == MAXDWORD) fputs("null", Output); else fprintf(Output, "%lu", Core->group);
                fputs(",\"index\":", Output); if(Core->index == MAXDWORD) fputs("null", Output); else fprintf(Output, "%lu", Core->index);
                fprintf(Output, ",\"status\":\"%s\",\"busy_percent\":", wtop_provider_state_name(Core->state));
                if(Core->state == WTOP_PROVIDER_VALID) fprintf(Output, "%.6f", Core->busy_percent); else fputs("null", Output);
            }
            fputc('}', Output);
        }
        fputc(']', Output);
    } else fputs("null", Output);
    fputc('}', Output);
    return !ferror(Output);
}

static int WriteSnapshot(FILE *Output, const wtop_options *Options)
{
    BOOL Json = Options->json;
    DWORD OutputCount = Options->mode == WTOP_QUERY && Options->query_target != WTOP_QUERY_PROCESSES ? 0 : ProcessCount;
    SnapshotTextFlags = 0;
    ULONGLONG Now = GetTickCount64();
    double Age = 0;
    metric_state Freshness = wtop_sample_freshness(CounterValue(Now), CounterValue(LastSampleObservedTick), SampleFreshnessBudget(), &Age);
    const char *Normalization = CpuMachine ? "active-machine-capacity" : "one-core";
    const char *Command = Options->mode == WTOP_INSPECT ? "inspect" : Options->mode == WTOP_QUERY ? "query" : Options->mode == WTOP_LOG ? "log" : "snapshot";
    if(Json) {
        fprintf(Output, "{\"schema\":2,\"version\":\"%s\",\"profile\":\"%s\",\"command\":\"%s\",\"inspect_pid\":%lu,\"sample_elapsed_ms\":%llu,"
               "\"cpu_normalization\":\"%s\",\"cpu_active_capacity\":",
               WTOP_VERSION_STRING, WTOP_BUILD_PROFILE, Command, Options->inspect_pid, (unsigned long long)LastSampleElapsed, Normalization);
        if(ActiveCpuCapacity) fprintf(Output, "%lu", ActiveCpuCapacity); else fputs("null", Output);
        fprintf(Output, ",\"sample\":{\"sequence\":\"%llu\",\"clock\":\"GetTickCount64-ms\","
               "\"started_ms\":\"%llu\",\"observed_ms\":\"%llu\",\"published_ms\":\"%llu\","
               "\"read_ms\":\"%llu\",\"age_ms\":%.0f,\"status\":\"%s\",\"stale_after_ms\":%lu,"
               "\"requested_interval_ms\":%lu,\"complete\":%s,\"observed_process_count\":%lu,\"selected_process_count\":%lu,\"unresolved_owner_filter_count\":%lu},"
               "\"io_scope\":\"process read+write I/O; not physical-disk attribution\","
               "\"system\":{\"cpu_scope\":\"GetSystemTimes; caller primary processor group on systems with more than 64 logical processors\","
               "\"cpu_observed_ms\":\"%llu\",\"memory_observed_ms\":\"%llu\"",
               (unsigned long long)SampleSequence, (unsigned long long)LastSampleStartedTick,
               (unsigned long long)LastSampleObservedTick, (unsigned long long)LastSamplePublishedTick,
               (unsigned long long)Now, Age, MetricStateName(Freshness), SampleFreshnessBudget(), LastSampleInterval, SampleComplete ? "true" : "false", ObservedProcessCount, ProcessCount, UnresolvedOwnerFilterCount,
               (unsigned long long)CpuSystemObservedTick, (unsigned long long)SystemMemoryObservedTick);
        JsonMetricDouble(Output, "cpu_busy_percent", FreshMetricState(CPUUsageState, CpuSystemObservedTick, Now), CPUUsage * 100, "percent");
        JsonMetricInteger(Output, "active_logical_processors", ActiveCpuCapacity ? METRIC_OK : WTOP_METRIC_UNSUPPORTED, ActiveCpuCapacity, "count");
        JsonMetricInteger(Output, "physical_total_bytes", FreshMetricState(SystemMemoryState, SystemMemoryObservedTick, Now), PhysicalTotalBytes, "bytes");
        JsonMetricInteger(Output, "physical_available_bytes", FreshMetricState(SystemMemoryState, SystemMemoryObservedTick, Now), PhysicalAvailableBytes, "bytes");
        JsonMetricInteger(Output, "commit_bytes", FreshMetricState(SystemCommitState, SystemMemoryObservedTick, Now), CommitBytes, "bytes");
        JsonMetricInteger(Output, "commit_limit_bytes", FreshMetricState(SystemCommitState, SystemMemoryObservedTick, Now), CommitLimitBytes, "bytes");
        JsonMetricInteger(Output, "uptime_ms", METRIC_OK, UpTime, "milliseconds");
        fprintf(Output, "},\"tree\":{\"enabled\":%s,\"unresolved_parent_count\":%lu,\"index_unavailable\":%s},\"processes\":[",
            ProcessSortType == SORT_BY_TREE ? "true" : "false",
            ProcessSortType == SORT_BY_TREE ? TreeUnresolvedParentCount : 0,
            ProcessSortType == SORT_BY_TREE && TreeIndexUnavailable ? "true" : "false");
        for(DWORD i = 0; i < OutputCount; ++i) {
            const process *Process = &ProcessList[i];
            if(i) fputc(',', Output);
            fprintf(Output, "{\"pid\":%lu,\"parent_pid\":%lu,\"threads\":%lu,\"base_priority\":%lu,\"name\":",
                   Process->ID, Process->ParentPID, Process->ThreadCount, Process->BasePriority);
            if(!JsonTString(Output, Process->ExeName)) return 1;
            fputs(",\"creation_filetime\":", Output);
            if(Process->CreationTime) fprintf(Output, "\"%llu\"", (unsigned long long)Process->CreationTime); else fputs("null", Output);
            fprintf(Output, ",\"observed_ms\":\"%llu\",\"cpu_observed_ms\":\"%llu\",\"cpu_elapsed_ms\":%.6f,"
                   "\"cpu_acquisition_ms\":%.6f,\"owner\":{\"status\":\"%s\",\"value\":",
                   (unsigned long long)Process->ObservedTick, (unsigned long long)Process->CpuObservedTick,
                   Process->CpuElapsedMs, Process->CpuAcquisitionMs, MetricStateName(Process->OwnerState));
            if(Process->OwnerState == METRIC_OK) { if(!JsonTString(Output, Process->OwnerSid)) return 1; }
            else fputs("null", Output);
            fputs(",\"sid\":", Output);
            if(Process->OwnerSid[0]) JsonTString(Output, Process->OwnerSid); else fputs("null", Output);
            fprintf(Output, ",\"account_name_status\":\"%s\",\"account_name_observed_ms\":\"%llu\",\"account_name\":",
                Process->OwnerNameStale ? "stale" : wtop_provider_state_name(Process->OwnerNameState),
                (unsigned long long)Process->OwnerNameObservedTick);
            if(Process->OwnerNameState == WTOP_PROVIDER_VALID) JsonTString(Output, Process->UserName);
            else fputs("null", Output);
            fputc('}', Output);
            JsonMetricDouble(Output, "cpu_percent", FreshMetricState(Process->CpuState, Process->CpuObservedTick, Now), Process->PercentProcessorTime, "percent");
            JsonMetricInteger(Output, "working_set_bytes", FreshMetricState(Process->MemoryState, Process->ObservedTick, Now), Process->UsedMemory, "bytes");
            JsonMetricDouble(Output, "io_read_write_bytes_per_second", FreshMetricState(Process->IoState, Process->ObservedTick, Now), Process->DiskUsage, "bytes/second");
            JsonMetricInteger(Output, "age_ms", FreshMetricState(Process->AgeState, Process->ObservedTick, Now), Process->UpTime, "milliseconds");
            fputc('}', Output);
        }
        fputs("],\"providers\":{", Output);
        BOOL First = TRUE;
        if(!WriteProvider(Output, "command_line", CommandObservation, &First) ||
           !WriteProvider(Output, "services", ServicesObservation, &First) ||
           !WriteProvider(Output, "per_core", CoreObservation, &First)) return 1;
        fprintf(Output, "},\"text_encoding_loss\":%s}\n", SnapshotTextFlags ? "true" : "false");
    } else {
        fprintf(Output, "Wtop %s - one snapshot (%llu ms measured; CPU normalization %s)\n", WTOP_VERSION_STRING, (unsigned long long)LastSampleElapsed, Normalization);
        fputs("I/O is read+write process I/O; AGE is wall time at observation; uint64 values are exact." "\n", Output);
        fprintf(Output, "SYSTEM\tPHYSICAL_TOTAL_BYTES\t%s\t", MetricStateName(SystemMemoryState));
        if(SystemMemoryState == METRIC_OK) fprintf(Output, "%llu", (unsigned long long)PhysicalTotalBytes);
        fprintf(Output, "\nSYSTEM\tCOMMIT_BYTES\t%s\t", MetricStateName(SystemCommitState));
        if(SystemCommitState == METRIC_OK) fprintf(Output, "%llu\tLIMIT_BYTES\t%llu", (unsigned long long)CommitBytes, (unsigned long long)CommitLimitBytes);
        fputs("\nPID\tOWNER\tCPU%\tWORKING_SET_BYTES\tIO_BYTES/S\tAGE_MS\tNAME" "\n", Output);
        for(DWORD i = 0; i < OutputCount; ++i) {
            const process *Process = &ProcessList[i];
            metric_state Cpu = FreshMetricState(Process->CpuState, Process->CpuObservedTick, Now);
            metric_state Memory = FreshMetricState(Process->MemoryState, Process->ObservedTick, Now);
            metric_state Io = FreshMetricState(Process->IoState, Process->ObservedTick, Now);
            metric_state ProcessAge = FreshMetricState(Process->AgeState, Process->ObservedTick, Now);
            char Name[1024], Owner[1024];
            NativeAscii(Process->ExeName, Name, sizeof(Name));
            NativeAscii(Process->UserName, Owner, sizeof(Owner));
            fprintf(Output, "%lu\t%s\t", Process->ID, Process->OwnerState == METRIC_OK ? Owner : MetricStateName(Process->OwnerState));
            if(Cpu == METRIC_OK) fprintf(Output, "%.2f", Process->PercentProcessorTime); else fputs(MetricStateName(Cpu), Output);
            fputc('\t', Output);
            if(Memory == METRIC_OK) fprintf(Output, "%llu", (unsigned long long)Process->UsedMemory); else fputs(MetricStateName(Memory), Output);
            fputc('\t', Output);
            if(Io == METRIC_OK) fprintf(Output, "%.2f", Process->DiskUsage); else fputs(MetricStateName(Io), Output);
            fputc('\t', Output);
            if(ProcessAge == METRIC_OK) fprintf(Output, "%llu", (unsigned long long)Process->UpTime); else fputs(MetricStateName(ProcessAge), Output);
            fprintf(Output, "\t%s\n", Name);
        }
        if(CommandObservation || ServicesObservation || CoreObservation) {
            /* The same typed provider representation is also available after the
               human process table; strings are JSON-escaped, never terminal controls. */
            fputs("PROVIDERS\t{", Output); BOOL First = TRUE;
            if(!WriteProvider(Output, "command_line", CommandObservation, &First) ||
               !WriteProvider(Output, "services", ServicesObservation, &First) ||
               !WriteProvider(Output, "per_core", CoreObservation, &First)) return 1;
            fputs("}\n", Output);
        }
    }
    return fflush(Output) == 0 && !ferror(Output) ? 0 : 1;
}

static int RunLog(const wtop_options *Options)
{
    TCHAR *Path = FromUtf8(Options->output_path);
    wtop_log_sink Sink;
    if(!Path) return 2;
    if(!wtop_log_open(&Sink, Path, Options->max_bytes)) {
        fprintf(stderr, "wtop: cannot create log destination/staging file (Win32 %lu); existing destinations are preserved\n", Sink.native_error);
        free(Path); return 1;
    }
    free(Path);
    ULONGLONG Started = LogStartedTick;
    unsigned long Count = 0;
    int Result = 0, Partial = 0;
    while(!ShouldStop()) {
        if(Count) {
            if(Options->duration_ms && GetTickCount64() - Started >= Options->duration_ms) break;
            if(CollectPerCore) {
                free(CoreObservation); CoreObservation = CollectCoreObservation((DWORD)Settings.interval_ms);
                if(!CoreObservation) { Result = 1; break; }
                CoreObservedTick = CoreObservation->observed_ms;
            }
            if(!PollProcessList((DWORD)Settings.interval_ms)) { Result = ShouldStop() ? 130 : 1; break; }
            AdoptPendingSample(); PollSystemInfo();
        }
        if(CollectPerCore && (!CoreObservation || CoreObservation->state != WTOP_PROVIDER_VALID)) Partial = 1;
        FILE *Record = wtop_log_begin(&Sink);
        if(!Record || WriteSnapshot(Record, Options)) { Result = 1; break; }
        wtop_log_result Committed = wtop_log_commit(&Sink);
        if(Committed != WTOP_LOG_OK) {
            Result = Committed == WTOP_LOG_CAPACITY ? 3 : 1;
            fprintf(stderr, "wtop: log stopped after %lu complete records (%llu bytes): %s\n", Count,
                (unsigned long long)Sink.committed_bytes, Committed == WTOP_LOG_CAPACITY ? "byte limit reached" : "output failure");
            break;
        }
        ++Count;
        if(Options->sample_limit && Count >= Options->sample_limit) break;
        if(Options->duration_ms && GetTickCount64() - Started >= Options->duration_ms) break;
    }
    if(ShouldStop()) Result = 130;
    if(!wtop_log_close(&Sink)) Result = 1;
    if(!Result && Partial) Result = 3;
    return Result;
}

static void ClearRow(int Row)
{
    wtop_frame_fill(&Screen.cells, 0, Row, Width, ' ', (WORD)Settings.colors[WTOP_COLOR_BG]);
    SetConCursorPos(0, (SHORT)Row);
    SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
}

static void DrawFunctionBar(void)
{
    memset(FooterWidths, 0, sizeof(FooterWidths));
    if(FunctionBarRow < 0) return;
    BOOL SharedEditor = InInputMode && FunctionBarRow == StatusRow;
    if(!SharedEditor) ClearRow(FunctionBarRow);
    int CompactStart = 0;
    wtop_ui_context_options Context = CurrentUiContext();
    for(int i = 0; i < 10; ++i) {
        wtop_ui_binding Binding;
        wtop_ui_resolve_function(&Context, i, &Binding);
        if(Binding.action == WTOP_UI_NONE) continue;
        int Start, Length;
        wtop_ui_bar_span(Width, i, &Start, &Length);
        if(Settings.footer_style == WTOP_FOOTER_COMPACT) {
            Start = CompactStart;
            Length = min((i == 9 ? 3 : 2) + (int)strlen(Binding.label) + 1, Width - Start);
            CompactStart += max(0, Length);
        }
        if(SharedEditor) {
            static const int Indices[] = { 0, 1, 3, 8, 9 };
            int Slot = -1;
            for(int j = 0; j < 5; ++j) if(Indices[j] == i) Slot = j;
            if(Slot < 0) continue;
            int Available = Width / 2;
            Start = Width - Available + (Available * Slot) / 5;
            Length = Width - Available + (Available * (Slot + 1)) / 5 - Start;
        }
        FooterStarts[i] = Start; FooterWidths[i] = Length;
        if(!Length) continue;
        SetConCursorPos((SHORT)Start, (SHORT)FunctionBarRow);
        SetColor((WORD)(Binding.enabled ? Settings.colors[WTOP_COLOR_FUNCTION_KEY] : FOREGROUND_INTENSITY));
        TCHAR Key[8];
        _stprintf_s(Key, _countof(Key), _T("F%d"), i + 1);
        int KeyLength = min(Length, (int)_tcslen(Key));
        ConPrintf(_T("%.*s"), KeyLength, Key);
        SetColor((WORD)(Binding.enabled ? Settings.colors[WTOP_COLOR_MENU_BAR] : FOREGROUND_INTENSITY));
        int Remaining = Length - KeyLength;
        if(Remaining) {
            TCHAR *Label = FromUtf8(Length < 7 ? Binding.compact_label : Binding.label);
            if(Label) { DrawPaddedText(Label, Remaining); free(Label); }
        }
    }
}

static const TCHAR *HelpLines[] = {
    _T("F1 Help   F2 Setup   F3 Search   F4 Filter   F5 Tree"),
    _T("F6 Sort   F7 Prio+   F8 Prio-   F9 Kill    F10 Quit"),
    _T("Arrows/PgUp/PgDn/Home/End navigate. Tab/Shift+Tab changes views."),
    _T("Space tags a lifetime-identified process; U clears tags; F follows."),
    _T("Z pauses/resumes; I reverses sort; P sorts CPU; M sorts memory; B hides/shows the bar."),
    _T("htop: k opens Kill; N sorts PID; h/? helps; S/C opens Setup."),
    _T("vi: j/k move, g/G top/bottom, n/N search next/previous, K Kill."),
    _T("Emacs: C-n/C-p move, C-v/M-v page, M-</M-> top/bottom."),
    _T("Emacs: C-s search, C-r previous match, C-g cancel, M-x commands."),
    _T("Emacs: C-x C-s saves. F10/q/Ctrl+C quit in every keymap."),
    _T("/: search, \\: live name filter, : commands. Enter accepts, Esc closes."),
    _T("Editor: Left/Right/Home/End; C-a/C-e, C-u clear, C-w word erase."),
    _T("Mouse: click a row/header/tab/button; wheel scrolls. Keyboard always works."),
    _T("F2: Left/Right pages, Up/Down row, Space toggle, +/- reorder/color."),
    _T("F2 s or :save explicitly saves. Quit never silently writes settings."),
    _T("Priority changes Windows classes, not Unix nice; REALTIME is excluded."),
    _T("F9 captures process identities; y confirms, n/Esc cancels."),
    _T("? metrics are unavailable. I/O is process transfers; Cmt is commit."),
    _T("Config file: custom tab names/count/filter, columns, meters and all colors.")
};

static void SetupLine(int Index, TCHAR *Text, size_t Capacity)
{
    if(SetupPage == 0) {
        switch(Index) {
        case 0: _stprintf_s(Text, Capacity, _T("Keymap: %hs"), wtop_keymap_name(Settings.keymap)); break;
        case 1: _stprintf_s(Text, Capacity, _T("Theme: %hs (choosing resets color overrides)"), wtop_theme_name(Settings.theme)); break;
        case 2: _stprintf_s(Text, Capacity, _T("Mouse: %hs; native input %s"), wtop_mouse_name(Settings.mouse), MouseEnabled ? _T("available") : _T("off/unavailable")); break;
        case 3: _stprintf_s(Text, Capacity, _T("[%c] Last-row function bar"), Settings.show_function_bar ? 'x' : ' '); break;
        case 4: _stprintf_s(Text, Capacity, _T("[%c] Tab strip"), Settings.show_tabs ? 'x' : ' '); break;
        case 5: _stprintf_s(Text, Capacity, _T("Sample interval: %lu ms (+/- 100 ms)"), Settings.interval_ms); break;
        case 6: _stprintf_s(Text, Capacity, _T("Footer style: %hs"), wtop_footer_style_name(Settings.footer_style)); break;
        case 7: _stprintf_s(Text, Capacity, _T("Live status: %hs (bottom explicitly adds a row)"), wtop_status_position_name(Settings.status_position)); break;
        case 8: _stprintf_s(Text, Capacity, _T("Paint interval: %lu ms; input remains immediate"), Settings.frame_interval_ms); break;
        case 9: _stprintf_s(Text, Capacity, _T("Meter width: %d cells"), Settings.meter_width); break;
        case 10: _stprintf_s(Text, Capacity, _T("Meter style: %hs"), wtop_meter_style_name(Settings.meter_style)); break;
        case 11: _stprintf_s(Text, Capacity, _T("Character set: %hs (complex glyphs retain safe fallback)"), wtop_character_set_name(Settings.character_set)); break;
        }
    } else if(SetupPage == 1 || SetupPage == 2) {
        int Items[WTOP_MAX_COLUMNS];
        int Count = SetupPage == 1 ? Settings.header_count : ActiveTab()->column_count;
        OrderedItems(SetupPage == 1 ? Settings.header_rows : ActiveTab()->columns, Count,
            SetupPage == 1 ? WTOP_HEADER_ROWS : WTOP_MAX_COLUMNS, Items);
        _stprintf_s(Text, Capacity, _T("[%c] %hs"), Index < Count ? 'x' : ' ',
            SetupPage == 1 ? wtop_header_row_name(Items[Index]) : wtop_column_name(Items[Index]));
    } else if(SetupPage == 3) {
        TCHAR *Name = FromUtf8(Settings.tabs[Index].name);
        _stprintf_s(Text, Capacity, _T("[%c] %s%s"), Settings.tabs[Index].visible ? 'x' : ' ', Name ? Name : _T("?"), Index == Settings.active_tab ? _T(" (active)") : _T(""));
        free(Name);
    } else _stprintf_s(Text, Capacity, _T("%-24hs 0x%02X (+/- changes Windows color attribute)"), wtop_color_name(Index), Settings.colors[Index]);
}

static void DrawEditorStatus(void)
{
    int EditorWidth = FunctionBarRow == StatusRow ? Width - Width / 2 : Width;
    int Prefix = 0;
    if(ViMessageActive() && CurrentViMessageType == VI_ERROR) {
        SetColor((WORD)Settings.colors[WTOP_COLOR_ERROR]);
        Prefix = ConPrintf(_T("%.*s | "), max(0, min(22, EditorWidth / 2)), ViMessage);
    }
    EditorWidth = max(1, EditorWidth - Prefix);
    unsigned long Cursor = ViInputCursor();
    unsigned long CursorCells = 0;
    for(unsigned long i = 0; i < Cursor && CurrentInputStr[i]; ++i) {
#ifdef UNICODE
        if(CurrentInputStr[i] >= 0xd800 && CurrentInputStr[i] <= 0xdbff && i + 1 < Cursor &&
           CurrentInputStr[i + 1] >= 0xdc00 && CurrentInputStr[i + 1] <= 0xdfff) ++i;
#endif
        ++CursorCells;
    }
    char Text[DEFAULT_STR_SIZE];
    size_t Cells = NativeAscii(CurrentInputStr, Text, sizeof(Text));
    if(CursorCells > Cells) CursorCells = (unsigned long)Cells;
    unsigned long Start = CursorCells >= (unsigned long)EditorWidth ? CursorCells - (unsigned long)EditorWidth + 1 : 0;
    SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
    ConPrintf(_T("%.*hs"), EditorWidth, Text + Start);
    wtop_frame_attribute(&Screen.cells, Prefix + (int)(CursorCells - Start), StatusRow,
        (WORD)Settings.colors[WTOP_COLOR_BG_HIGHLIGHT]);
}

static void DrawOverlay(void)
{
    SetConCursorPos(0, 0);
    SetColor((WORD)Settings.colors[WTOP_COLOR_HEADER]);
    const TCHAR *Title = Overlay == OVERLAY_HELP ? _T("Help") : Overlay == OVERLAY_SETUP ? _T("Setup") : Overlay == OVERLAY_SORT ? _T("Sort") : Overlay == OVERLAY_RESULTS ? _T("Actions") : Overlay == OVERLAY_TAB_DELETE ? _T("Delete view") : _T("Terminate");
    ConPrintf(_T("%s - Wtop [%hs]%s"), Title, wtop_keymap_name(Settings.keymap), ConfigDirty ? _T(" * unsaved") : _T(""));
    SetConCursorPos(0, 1); SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
    if(Overlay == OVERLAY_RESULTS) {
        ConPrintf(_T("Up/Down/PgUp/PgDn inspect; Enter/Esc returns. Requested means not yet confirmed."));
        for(DWORD i = (DWORD)OverlayTop; i < LastActionCount && i - (DWORD)OverlayTop < (DWORD)max(0, Height - 5); ++i) {
            const wtop_action_result *Result = &LastActionResults[i];
            SetConCursorPos(0, (SHORT)(3 + i - (DWORD)OverlayTop));
            ConPrintf(_T("PID %lu %hs error=%lu priority=%lu->%lu"), Result->request.target.pid,
                wtop_action_state_name(Result->state), Result->native_error,
                Result->previous_priority, Result->requested_priority);
        }
    } else if(Overlay == OVERLAY_TAB_DELETE) {
        int Index = FindTabById(PendingDeleteTabId);
        TCHAR *Name = Index >= 0 ? FromUtf8(Settings.tabs[Index].name) : NULL;
        ConPrintf(_T("Delete view %lu '%s'? y confirms; n/Esc cancels."), PendingDeleteTabId, Name ? Name : _T("unavailable"));
        free(Name);
    } else if(Overlay == OVERLAY_KILL) {
        ConPrintf(_T("Terminate %lu captured process(es)? y confirms; n/Esc cancels."), PendingKillCount);
        for(DWORD i = 0; i < PendingKillCount && i < (DWORD)max(0, Height - 5); ++i) {
            SetConCursorPos(0, (SHORT)(i + 3));
            ConPrintf(_T("PID %lu  creation %llu"), PendingKills[i].ID, PendingKills[i].CreationTime);
        }
    } else if(Overlay == OVERLAY_HELP) {
        ConPrintf(_T("Up/Down scroll; Esc returns. Control hints reflect the selected keymap."));
        for(int i = OverlayTop; i < (int)_countof(HelpLines) && i - OverlayTop < Height - 5; ++i) {
            SetConCursorPos(0, (SHORT)(3 + i - OverlayTop)); ConPrintf(_T("%s"), HelpLines[i]);
        }
    } else if(Overlay == OVERLAY_SORT) {
        ConPrintf(_T("Up/Down selects a metric; Enter applies; Esc cancels."));
        int Visible = max(1, Height - 5);
        if(SortChoice < OverlayTop) OverlayTop = SortChoice;
        if(SortChoice >= OverlayTop + Visible) OverlayTop = SortChoice - Visible + 1;
        for(int i = OverlayTop; i < WTOP_MAX_COLUMNS && i - OverlayTop < Visible; ++i) {
            SetConCursorPos(0, (SHORT)(i - OverlayTop + 3));
            SetColor((WORD)Settings.colors[i == SortChoice ? WTOP_COLOR_BG_HIGHLIGHT : WTOP_COLOR_FG]);
            ConPrintf(_T("%hs"), wtop_column_name(i));
        }
    } else {
        static const TCHAR *Pages[] = { _T("General"), _T("Meters"), _T("Columns"), _T("Tabs"), _T("Colors") };
        ConPrintf(_T("%s | Left/Right page, Space toggle, +/- move/change, s save, Esc back"), Pages[SetupPage]);
        int Visible = max(1, Height - 5);
        if(SetupIndex < OverlayTop) OverlayTop = SetupIndex;
        if(SetupIndex >= OverlayTop + Visible) OverlayTop = SetupIndex - Visible + 1;
        for(int i = OverlayTop; i < SetupCount() && i - OverlayTop < Visible; ++i) {
            TCHAR Text[512]; SetupLine(i, Text, _countof(Text));
            SetConCursorPos(0, (SHORT)(i - OverlayTop + 3));
            SetColor((WORD)Settings.colors[i == SetupIndex ? WTOP_COLOR_BG_HIGHLIGHT : WTOP_COLOR_FG]);
            DrawPaddedText(Text, Width);
        }
    }
    StatusRow = Chrome.prompt >= 0 ? Chrome.prompt : Chrome.status;
    if(StatusRow < 0 && (DisplayTextFlags & (WTOP_TEXT_LOSSY | WTOP_TEXT_INVALID))) { SetConCursorPos(0, 2); ConPrintf(_T("glyph substitution; raw text preserved")); }
    if(StatusRow >= 0) { ClearRow(StatusRow); if(InInputMode) DrawEditorStatus(); else WriteVi(); }
    DrawFunctionBar();
    RenderedRowCount = 0;
    RenderInvalidated = FALSE;
}

static void DrawHeaderRow(int Row, int Kind)
{
    ClearRow(Row);
    switch(Kind) {
    case WTOP_ROW_TITLE:
        SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
        ConPrintf(_T("Wtop %hs on %s [%hs]%s"), WTOP_VERSION_STRING, ComputerName, wtop_keymap_name(Settings.keymap), ConfigDirty ? _T(" *") : _T(""));
        break;
    case WTOP_ROW_CPU:
        DrawPercentageBar(_T("CPU"), CPUUsageKnown ? CPUUsage : -1, (WORD)Settings.colors[WTOP_COLOR_CPU]);
        ConPrintf(_T("  Tasks: %lu/%lu  Active: %lu"), ViewCount, ProcessCount, RunningProcessCount);
        break;
    case WTOP_ROW_MEMORY:
        DrawPercentageBar(_T("Mem"), SystemMemoryKnown ? UsedMemoryPerc : -1, (WORD)Settings.colors[WTOP_COLOR_MEMORY]);
        if(SystemMemoryKnown) ConPrintf(_T("  %llu/%llu MiB"), UsedMemory, TotalMemory);
        break;
    case WTOP_ROW_COMMIT: {
        DrawPercentageBar(_T("Cmt"), SystemCommitState == METRIC_OK ? UsedPageMemoryPerc : -1, (WORD)Settings.colors[WTOP_COLOR_COMMIT]);
        TCHAR Age[32]; FormatTimeString(Age, _countof(Age), UpTime); ConPrintf(_T("  Up: %s"), Age);
        break;
    }
    case WTOP_ROW_SUMMARY:
        ConPrintf(_T("%s (%lu CPUs in native view)"), CPUName, CPUCoreCount);
        break;
    }
}

static int DrawCoreRows(int Row)
{
    if(!CollectPerCore || Row >= Height - 5) return Row;
    if(!CoreObservation || (CoreObservation->state != WTOP_PROVIDER_VALID && CoreObservation->state != WTOP_PROVIDER_CAPACITY)) {
        ClearRow(Row++);
        ConPrintf(_T("Per-core: %hs"), CoreObservation ? wtop_provider_state_name(CoreObservation->state) : "unavailable");
        return Row;
    }
    BOOL Fresh = CoreObservedTick && GetTickCount64() - CoreObservedTick <= max(3000UL, SampleFreshnessBudget());
    int Columns = max(1, Width / 20);
    DWORD Shown = 0;
    for(; Shown < CoreObservation->count && Row < Height - 5; ++Row) {
        ClearRow(Row);
        for(int Column = 0; Column < Columns && Shown < CoreObservation->count; ++Column, ++Shown) {
            const wtop_provider_core *Core = &CoreObservation->data.cores[Shown];
            SetConCursorPos((SHORT)(Column * 20), (SHORT)Row);
            SetColor((WORD)Settings.colors[WTOP_COLOR_CPU]);
            TCHAR Label[32];
            if(Core->group != MAXDWORD) _stprintf_s(Label, _countof(Label), _T("%lu:%lu"), Core->group, Core->index);
            else _stprintf_s(Label, _countof(Label), _T("%lu"), Shown);
            if(Fresh && Core->state == WTOP_PROVIDER_VALID) {
                TCHAR Bar[9]; int Filled = (int)max(0.0, min(8.0, Core->busy_percent * 0.08));
                for(int i = 0; i < 8; ++i) Bar[i] = i < Filled ? _T('|') : _T(' ');
                Bar[8] = 0;
                ConPrintf(_T("%4.4s[%s]%5.1f"), Label, Bar, Core->busy_percent);
            } else ConPrintf(_T("%4.4s[   ?    ] %hs"), Label, Fresh ? "?" : "old");
        }
    }
    if(Shown < CoreObservation->count && Row > 0) {
        ClearRow(Row - 1);
        ConPrintf(_T("Per-core: %lu total; more in query system --per-core"), CoreObservation->count);
    }
    return Row;
}

static void ComposeFrame(void)
{
    DisplayTextFlags = 0;
    wtop_ui_chrome_layout(Height, Settings.show_function_bar, Settings.footer_style,
        Settings.status_position, InInputMode || ViMessageActive(), &Chrome);
    FunctionBarRow = Chrome.footer;
    StatusRow = Chrome.prompt >= 0 ? Chrome.prompt : Chrome.status;
    if(InInputMode && Settings.show_function_bar && Settings.footer_style != WTOP_FOOTER_STATUS)
        FunctionBarRow = StatusRow;
    if(Width < 40 || Height < 10) {
        ClearRow(0); ConPrintf(_T("Resize to at least 40x10; F10 quits."));
        VisibleProcessCount = RenderedRowCount = 0;
        DisplayedProcessID = 0; DisplayedCreationTime = 0;
        RenderInvalidated = TRUE; return;
    }
    if(Overlay != OVERLAY_NONE) { DrawOverlay(); return; }
    ReadjustCursor();
    int Row = 0;
    int HeaderLimit = min(Settings.header_count, max(0, Chrome.content_end - 3));
    for(int i = 0; i < HeaderLimit; ++i) DrawHeaderRow(Row++, Settings.header_rows[i]);
    int BannerRow = -1;
    for(int i = 0; i < HeaderLimit; ++i) if(Settings.header_rows[i] == WTOP_ROW_TITLE) BannerRow = i;
    if(Chrome.banner_status && BannerRow < 0 && Row < Chrome.content_end - 2) BannerRow = Row++;
    Row = DrawCoreRows(Row);
    TabsRow = -1;
    memset(RenderedTabWidth, 0, sizeof(RenderedTabWidth));
    if(Settings.show_tabs) {
        TabsRow = Row++;
        ClearRow(TabsRow);
        for(int i = 0; i < Settings.tab_count; ++i) {
            if(!Settings.tabs[i].visible) continue;
            RenderedTabStart[i] = RenderColumn;
            SetColor((WORD)Settings.colors[i == Settings.active_tab ? WTOP_COLOR_BG_HIGHLIGHT : WTOP_COLOR_FG]);
            TCHAR *Name = FromUtf8(Settings.tabs[i].name);
            if(Name) { ConPrintf(_T(" %s "), Name); free(Name); }
            RenderedTabWidth[i] = RenderColumn - RenderedTabStart[i];
        }
    }
    ProcessHeaderRow = Row++;
    ClearRow(ProcessHeaderRow); DrawProcessListHeader();
    ProcessWindowPosY = Row;
    StatusRow = Chrome.prompt >= 0 ? Chrome.prompt : Chrome.status;
    int EndRow = Chrome.content_end;
    VisibleProcessCount = (DWORD)max(0, EndRow - ProcessWindowPosY);
    ProcessWindowHeight = VisibleProcessCount;
    ReadjustCursor();
    DisplayedProcessID = ViewCount ? ProcessList[SelectedProcessIndex].ID : 0;
    DisplayedCreationTime = ViewCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
    if(VisibleProcessCount > RenderedRowCapacity) {
        process_identity *Larger = realloc(RenderedRows, (size_t)VisibleProcessCount * sizeof(*Larger));
        if(!Larger) { CollectorError = ERROR_NOT_ENOUGH_MEMORY; RequestShutdown(); return; }
        RenderedRows = Larger; RenderedRowCapacity = VisibleProcessCount;
    }
    RenderedRowCount = 0;
    for(DWORD i = 0; i < VisibleProcessCount; ++i) {
        DWORD Index = ProcessIndex + i;
        if(Index >= ViewCount) break;
        const process *Process = &ProcessList[ViewIndices[Index]];
        SetConCursorPos(0, (SHORT)(ProcessWindowPosY + (int)i));
        WriteProcessInfo(Process, Index == SelectedViewIndex);
        RenderedRows[RenderedRowCount].ID = Process->ID;
        RenderedRows[RenderedRowCount++].CreationTime = Process->CreationTime;
    }
    if(Chrome.banner_status && BannerRow >= 0) {
        ClearRow(BannerRow);
        SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
        ConPrintf(_T("Wtop %hs%hs | "), WTOP_VERSION_STRING, Paused ? " PAUSED" : "");
        WriteStatusAt(BannerRow, RenderColumn);
    }
    if(StatusRow >= 0) {
        ClearRow(StatusRow);
        if(InInputMode) DrawEditorStatus();
        else WriteVi();
    }
    DrawFunctionBar();
    RenderInvalidated = FALSE;
}

static void DrawFrame(void)
{
    if(!wtop_console_frame_begin(&Screen, ConsoleHandle, Width, Height,
        ConsoleOriginX, ConsoleOriginY, (WORD)Settings.colors[WTOP_COLOR_BG])) {
        CollectorError = Screen.error; RequestShutdown(); return;
    }
    RenderRow = RenderColumn = 0;
    CurrentColor = (WORD)Settings.colors[WTOP_COLOR_FG];
    ComposeFrame();
    int Result = wtop_console_frame_present(&Screen);
    if(Result < 0) {
        RenderInvalidated = TRUE;
        if(Screen.error != ERROR_RETRY) { CollectorError = Screen.error; RequestShutdown(); }
    } else if(Width >= 40 && Height >= 10) RenderInvalidated = FALSE;
}

int _tmain(int argc, TCHAR *argv[])
{
    int Result = 0;
    if(wtop_provider_worker_dispatch(argc, argv, &Result)) return Result;
    BOOL Initialized = FALSE, HandlerRegistered = FALSE, ScreenCreated = FALSE;
    BOOL InputModeChanged = FALSE;
    wtop_settings_defaults(&Settings);
    char **Utf8Argv = calloc((size_t)argc, sizeof(*Utf8Argv));
    if(!Utf8Argv) return 1;
    for(int i = 0; i < argc; ++i) {
        unsigned TextFlags = 0;
        Utf8Argv[i] = ToUtf8WithStatus(argv[i], &TextFlags);
        if(!Utf8Argv[i]) { Result = 1; goto Finish; }
        if(TextFlags) { fputs("wtop: command line contains invalid Unicode\n", stderr); Result = 2; goto Finish; }
    }
    wtop_options Options;
    const char *Error;
    if(!wtop_parse_options(argc, (const char *const *)Utf8Argv, &Options, &Error)) {
        fprintf(stderr, "wtop: %s; use --help\n", Error);
        Result = 2; goto Finish;
    }
    if(Options.action == WTOP_PATHS) { Result = PrintPaths(&Options); goto Finish; }
    if(Options.action != WTOP_RUN && Options.action != WTOP_WRITE_CONFIG) { Result = PrintInformation(&Options); goto Finish; }
    if(Options.mode == WTOP_GUI) {
        fputs("wtop: --gui is reserved; native GUI is not implemented. Use --tui or --snapshot.\n", stderr);
        Result = 2; goto Finish;
    }
    CpuMachine = Options.cpu_machine;
    CollectPerCore = Options.per_core;
    SkipProcessRows = Options.mode == WTOP_QUERY && Options.query_target != WTOP_QUERY_PROCESSES;
    ReadOnlyMode = Options.readonly;
    if(!ApplyOptions(&Options)) { fputs("wtop: invalid or excessive filter value\n", stderr); Result = 2; goto Finish; }
    if(!ReadConfigFile(&Options)) {
        fputs("wtop: cannot read configuration or configuration is invalid\n", stderr);
        Result = 2; goto Finish;
    }
    if(!wtop_settings_apply_options(&Settings, &Options, &Error)) {
        fprintf(stderr, "wtop: %s\n", Error); Result = 2; goto Finish;
    }
    ProcessSortType = ActiveTab()->tree ? SORT_BY_TREE : (process_sort_type)ActiveTab()->sort_column;
    SortOrder = ActiveTab()->descending ? DESCENDING : ASCENDING;
    if(Options.action == WTOP_WRITE_CONFIG) {
        wtop_store_result Saved;
        if(!ExportConfigFile(Options.write_config_path, &Saved)) {
            fprintf(stderr, "wtop: settings %s: %s (Win32 %lu)\n",
                wtop_store_outcome_name(Saved.outcome), Saved.message, Saved.native_error);
            Result = 1; goto Finish;
        }
        printf("Saved configuration: %s\n", Options.write_config_path);
        Result = fflush(stdout) || ferror(stdout) ? 1 : 0;
        goto Finish;
    }
    InteractiveMode = Options.mode == WTOP_TUI;
    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if(InteractiveMode) {
        DWORD OutputMode;
        if(!GetConsoleMode(ConsoleHandle, &OutputMode) ||
           !GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &OriginalInputMode)) {
            fputs("wtop: TUI requires console input and output; use --snapshot --format text|json when redirected.\n", stderr);
            Result = 2; goto Finish;
        }
    }
    InitializeCriticalSection(&SyncLock);
    Initialized = TRUE;
    ShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CleanupDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if(!ShutdownEvent || !CleanupDoneEvent) { Result = 1; goto Cleanup; }
    if(!SetConsoleCtrlHandler(CtrlHandler, TRUE)) { Result = 1; goto Cleanup; }
    HandlerRegistered = TRUE;
    ProcessList = calloc(ProcessListSize, sizeof(*ProcessList));
    TaggedProcessList = calloc(TaggedProcessListSize, sizeof(*TaggedProcessList));
    ViMessage = calloc(DEFAULT_STR_SIZE, sizeof(*ViMessage));
    if(!ProcessList || !TaggedProcessList || !ViMessage || !ViInit()) { Result = 1; goto Cleanup; }
    wtop_provider_session_init(&OwnerProvider);
    wtop_provider_session_init(&CoreProvider);
    wtop_provider_session_init(&InspectionProvider);
    PollInitialSystemInfo();
    PollSystemInfo();
    InterlockedExchange(&CollectorInterval, (LONG)Settings.interval_ms);
    LogStartedTick = GetTickCount64();
    if(Options.mode == WTOP_LOG && Options.duration_ms) CollectionDeadline = LogStartedTick + Options.duration_ms;
    if(CollectPerCore) {
        if(InteractiveMode) {
            PerCoreThread = CreateThread(NULL, 0, PollCoreThreadProc, NULL, 0, NULL);
            if(!PerCoreThread) { Result = 1; goto Cleanup; }
        } else {
            CoreObservation = CollectCoreObservation((DWORD)Settings.interval_ms);
            if(!CoreObservation) { Result = 1; goto Cleanup; }
            CoreObservedTick = CoreObservation->observed_ms;
        }
    }
    if(!PollProcessList(InteractiveMode ? 50 : (DWORD)Settings.interval_ms)) {
        Result = CollectorError ? 1 : 130;
        goto Cleanup;
    }
    AdoptPendingSample();
    if(!InteractiveMode) {
        PollSystemInfo();
        if(Options.mode == WTOP_LOG) Result = RunLog(&Options);
        else {
            int Inspection = CollectInspection(&Options);
            if(Inspection == 130 || ShouldStop()) { Result = 130; goto Cleanup; }
            Result = WriteSnapshot(stdout, &Options);
            if(!Result) Result = Inspection;
            if(ShouldStop()) Result = 130;
        }
        goto Cleanup;
    }
    if(InteractiveMode) {
        OldConsoleHandle = ConsoleHandle;
        ConsoleHandle = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
        if(ConsoleHandle == INVALID_HANDLE_VALUE) { Result = 1; goto Cleanup; }
        ScreenCreated = TRUE;
        if(!SetConsoleActiveScreenBuffer(ConsoleHandle)) { Result = 1; goto Cleanup; }
        SetConsoleMode(ConsoleHandle, ENABLE_PROCESSED_OUTPUT);
        InputModeChanged = TRUE; /* Restore the exact caller mode even after a partial setup failure. */
        ConfigureMouse();
        PollConsoleInfo();
    }
    InterlockedExchange(&CollectorInterval, (LONG)Settings.interval_ms);
    ProcessListThread = CreateThread(NULL, 0, PollProcessListThreadProc, NULL, 0, NULL);
    if(!ProcessListThread) { Result = 1; goto Cleanup; }
    ULONGLONG LastDraw = 0, LastSystemPoll = 0, LastAgeDraw = 0;
    BOOL Redraw = TRUE;
    while(!ShouldStop()) {
        /* Consume input against the last displayed model. Adopting a sample
           first invalidates that frame and can silently discard an otherwise
           valid function-bar click. Geometry changes still invalidate hits;
           native actions independently revalidate their captured lifetimes.
           Publish the next front only after input, then paint it this turn. */
        if(PollConsoleInfo()) Redraw = TRUE;
        BOOL InputRedraw = FALSE;
        ProcessInput(&InputRedraw);
        if(InputRedraw) Redraw = TRUE;
        InterlockedExchange(&CollectorInterval, (LONG)Settings.interval_ms);
        if(ShouldStop()) break;
        if(!Paused && AdoptPendingSample()) Redraw = TRUE;
        if(!Paused && AdoptPendingCore()) Redraw = TRUE;
        ULONGLONG Now = GetTickCount64();
        if(!Paused && Now - LastSystemPoll >= Settings.interval_ms) {
            PollSystemInfo(); LastSystemPoll = Now; Redraw = TRUE;
        }
        if(Now - LastAgeDraw >= 1000) { Redraw = TRUE; LastAgeDraw = Now; }
        if((Redraw || RedrawAtCursor || RenderInvalidated) &&
           (InputRedraw || Now - LastDraw >= Settings.frame_interval_ms)) {
            DrawFrame(); LastDraw = Now; Redraw = RedrawAtCursor = FALSE;
        }
        WaitForSingleObject(ShutdownEvent, INPUT_LOOP_DELAY);
    }
Cleanup:
    RequestShutdown();
    /* Restore caller-owned console state even if a native query cannot finish. */
    if(ScreenCreated) { RestoreConsole(); CloseHandle(ConsoleHandle); ConsoleHandle = OldConsoleHandle; }
    if(InputModeChanged) SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), OriginalInputMode);
    if(ProcessListThread) {
        if(WaitForSingleObject(ProcessListThread, SHUTDOWN_GRACE_MS) != WAIT_OBJECT_0) {
            /* Only this read-only monitor is terminated. Never free shared state
               while its worker is still running, and never TerminateThread. */
            const TCHAR *Message = _T("wtop: collector did not stop within 3000 ms; forcing monitor shutdown (exit 124).\n");
            OutputDebugString(Message);
            DWORD ErrorMode;
            if(GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &ErrorMode))
                WriteTText(GetStdHandle(STD_ERROR_HANDLE), Message, _tcslen(Message));
            TerminateProcess(GetCurrentProcess(), EXIT_FORCED_SHUTDOWN);
            /* GetCurrentProcess supplies a full-access pseudo-handle. */
            ExitProcess(EXIT_FORCED_SHUTDOWN);
        }
        CloseHandle(ProcessListThread);
        ProcessListThread = NULL;
    }
    if(PerCoreThread) {
        if(WaitForSingleObject(PerCoreThread, SHUTDOWN_GRACE_MS) != WAIT_OBJECT_0) {
            OutputDebugString(_T("wtop: per-core worker did not retire; own monitor exit 124.\n"));
            TerminateProcess(GetCurrentProcess(), EXIT_FORCED_SHUTDOWN);
            ExitProcess(EXIT_FORCED_SHUTDOWN);
        }
        CloseHandle(PerCoreThread); PerCoreThread = NULL;
    }
    free(PendingCoreObservation); PendingCoreObservation = NULL;
    if(!wtop_provider_session_close(&OwnerProvider, 1000)) {
        fputs("wtop: owner-provider retirement not confirmed; exiting with owned job cleanup\n", stderr);
        Result = EXIT_FORCED_SHUTDOWN;
    }
    if(!wtop_provider_session_close(&CoreProvider, 1000)) Result = EXIT_FORCED_SHUTDOWN;
    free(CoreObservation); CoreObservation = NULL;
    if(!wtop_provider_session_close(&InspectionProvider, 1000)) Result = EXIT_FORCED_SHUTDOWN;
    free(CommandObservation); CommandObservation = NULL;
    free(ServicesObservation); ServicesObservation = NULL;
    wtop_console_frame_dispose(&Screen);
    ViDestroy();
    free(ViMessage); ViMessage = NULL;
    FreePendingSample(PendingSample); PendingSample = NULL;
    free(ProcessList); ProcessList = NULL;
    free(TaggedProcessList); TaggedProcessList = NULL;
    free(ViewIndices); ViewIndices = NULL;
    free(RenderedRows); RenderedRows = NULL;
    free(PendingKills); PendingKills = NULL;
    free(LastActionResults); LastActionResults = NULL; LastActionCount = 0;
    if(Initialized) DeleteCriticalSection(&SyncLock);
    if(CleanupDoneEvent) SetEvent(CleanupDoneEvent);
    if(HandlerRegistered) SetConsoleCtrlHandler(CtrlHandler, FALSE);
    /* The two callback rendezvous handles have process lifetime: a control
       callback may still be returning after unregistration. Exit closes them. */
    if(CollectorError) { fprintf(stderr, "wtop: collector failed (Win32 error %lu)\n", CollectorError); Result = 1; }
Finish:
    wtop_store_dispose(&ConfigStore);
    wtop_config_document_free(&ConfigDocument);
    for(int i = 0; i < argc; ++i) free(Utf8Argv[i]);
    free(Utf8Argv);
    return Result;
}
