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
#include "core.h"
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
static int RenderColumn;
static int SizeX;
static int SizeY;
static const int ProcessWindowPosY = 6;
static DWORD ProcessWindowHeight;
static DWORD VisibleProcessCount;
static WORD SavedAttributes;
HANDLE ConsoleHandle;
static HANDLE OldConsoleHandle;
static BOOL InteractiveMode = TRUE;
static CRITICAL_SECTION SyncLock;
static HANDLE ShutdownEvent;
static HANDLE CleanupDoneEvent;
static HANDLE ProcessListThread;
static volatile LONG ShutdownRequested;
static DWORD CollectorError;
static ULONGLONG LastSampleElapsed;

void RequestShutdown(void)
{
    InterlockedExchange(&ShutdownRequested, 1);
    if(ShutdownEvent) SetEvent(ShutdownEvent);
}

static BOOL ShouldStop(void)
{
    return InterlockedCompareExchange(&ShutdownRequested, 0, 0) != 0;
}

static int ConPrintf(const TCHAR *Fmt, ...)
{
    TCHAR Buffer[4096];
    va_list VaList;
    va_start(VaList, Fmt);
    int CharsWritten = _vsntprintf_s(Buffer, _countof(Buffer), _TRUNCATE, Fmt, VaList);
    va_end(VaList);
    if(CharsWritten < 0) CharsWritten = (int)_tcslen(Buffer);
    if(InteractiveMode && Width > 0) {
        CharsWritten = min(CharsWritten, max(0, Width - RenderColumn));
#ifdef UNICODE
        if(CharsWritten && Buffer[CharsWritten - 1] >= 0xd800 && Buffer[CharsWritten - 1] <= 0xdbff) --CharsWritten;
#endif
        RenderColumn += CharsWritten;
    }
    if(!WriteTText(ConsoleHandle, Buffer, (size_t)CharsWritten)) RequestShutdown();
    return CharsWritten;
}

static void ConPutc(TCHAR c)
{
    if(InteractiveMode && Width > 0 && RenderColumn >= Width) return;
    ++RenderColumn;
    if(!WriteTText(ConsoleHandle, &c, 1)) RequestShutdown();
}

#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_CYAN (FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_CYAN (BACKGROUND_INTENSITY | BACKGROUND_GREEN | BACKGROUND_BLUE)

typedef struct config {
	WORD FGColor;
	WORD BGColor;
	WORD FGHighlightColor;
	WORD BGHighlightColor;
	WORD MenuBarColor;
	WORD ProcessListHeaderColor;
	WORD CPUBarColor;
	WORD MemoryBarColor;
	WORD PageMemoryBarColor;
	WORD ErrorColor;
	ULONGLONG RedrawInterval;
} config;

static config Config = {
	/* Default values */
	FOREGROUND_WHITE,
	FOREGROUND_INTENSITY,
	FOREGROUND_CYAN,
	BACKGROUND_CYAN,
	BACKGROUND_BLUE,
	BACKGROUND_GREEN,
	FOREGROUND_RED,
	FOREGROUND_GREEN,
	FOREGROUND_GREEN,
	BACKGROUND_RED | FOREGROUND_WHITE,
	1000
};

static config MonochromeConfig = {
	FOREGROUND_WHITE,
	FOREGROUND_INTENSITY,
	FOREGROUND_INTENSITY | FOREGROUND_WHITE,
	BACKGROUND_WHITE,
	0,
	BACKGROUND_WHITE,
	FOREGROUND_WHITE,
	FOREGROUND_WHITE,
	FOREGROUND_WHITE,
	BACKGROUND_WHITE,
	1000
};

static BOOL ParseConfigLine(char *Line)
{
    const char *Delimiters = " \t\r\n";
    char *Context;
    char *Key = strtok_s(Line, Delimiters, &Context);
    if(!Key || Key[0] == '#') return TRUE;
    char *Value = strtok_s(NULL, Delimiters, &Context);
    char *Extra = strtok_s(NULL, Delimiters, &Context);
    if(!Value || (Extra && Extra[0] != '#')) return FALSE;
    unsigned long Num;
    if(!_stricmp(Key, "RedrawInterval")) {
        if(!wtop_parse_unsigned(Value, 50, 60000, 10, &Num)) return FALSE;
        Config.RedrawInterval = Num;
        return TRUE;
    }
    if(!wtop_parse_unsigned(Value, 0, 255, 0, &Num)) return FALSE;
    if(!_stricmp(Key, "FGColor")) Config.FGColor = (WORD)Num;
    else if(!_stricmp(Key, "BGColor")) Config.BGColor = (WORD)Num;
    else if(!_stricmp(Key, "FGHighlightColor")) Config.FGHighlightColor = (WORD)Num;
    else if(!_stricmp(Key, "BGHighlightColor")) Config.BGHighlightColor = (WORD)Num;
    else if(!_stricmp(Key, "MenuBarColor")) Config.MenuBarColor = (WORD)Num;
    else if(!_stricmp(Key, "ProcessListHeaderColor")) Config.ProcessListHeaderColor = (WORD)Num;
    else if(!_stricmp(Key, "CPUBarColor")) Config.CPUBarColor = (WORD)Num;
    else if(!_stricmp(Key, "MemoryBarColor")) Config.MemoryBarColor = (WORD)Num;
    else if(!_stricmp(Key, "PageMemoryBarColor")) Config.PageMemoryBarColor = (WORD)Num;
    else if(!_stricmp(Key, "ErrorColor")) Config.ErrorColor = (WORD)Num;
    else return FALSE;
    return TRUE;
}

/* Read-only compatibility import. Persistence/install policy is not implemented. */
static BOOL ReadConfigFile(const char *ExplicitPath)
{
    FILE *File = NULL;
    TCHAR Path[32768];
    if(ExplicitPath) {
        TCHAR *Converted = FromUtf8(ExplicitPath);
        if(!Converted || _tcslen(Converted) >= _countof(Path)) { free(Converted); return FALSE; }
        _tcscpy_s(Path, _countof(Path), Converted);
        free(Converted);
    } else {
        DWORD Length = GetModuleFileName(NULL, Path, _countof(Path));
        if(!Length || Length >= _countof(Path)) return FALSE;
        TCHAR *Slash = _tcsrchr(Path, _T('\\'));
        if(!Slash) return FALSE;
        Slash[1] = 0;
        if(_tcslen(Path) + 11 >= _countof(Path)) return FALSE;
        _tcscat_s(Path, _countof(Path), _T("wtop.conf"));
        if(_tfopen_s(&File, Path, _T("r"))) {
            Slash[1] = 0;
            _tcscat_s(Path, _countof(Path), _T("ntop.conf"));
        }
    }
    if(!File && _tfopen_s(&File, Path, _T("r"))) {
        return ExplicitPath ? FALSE : TRUE;
    }
    char Line[4096];
    unsigned long LineNumber = 0;
    BOOL Ok = TRUE;
    while(fgets(Line, sizeof(Line), File)) {
        ++LineNumber;
        if((!strchr(Line, '\n') && !feof(File)) || !ParseConfigLine(Line)) {
            fprintf(stderr, "wtop: invalid configuration at line %lu\n", LineNumber);
            Ok = FALSE;
            break;
        }
    }
    if(ferror(File)) Ok = FALSE;
    fclose(File);
    return Ok;
}

static WORD CurrentColor;
static WORD ColorOverride;

static void SetColor(WORD Color)
{
	Color |= ColorOverride;
	CurrentColor = Color;
	SetConsoleTextAttribute(ConsoleHandle, Color);
}

static void SetConCursorPos(SHORT X, SHORT Y)
{
	COORD Coord;
	Coord.X = X;
	Coord.Y = Y;
	SetConsoleCursorPosition(ConsoleHandle, Coord);
    RenderColumn = X;
}

typedef enum metric_state {
    METRIC_OK, METRIC_DENIED, METRIC_UNAVAILABLE, METRIC_WARMING, METRIC_DISCONTINUITY
} metric_state;

static const char *MetricStateName(metric_state State)
{
    static const char *Names[] = { "ok", "denied", "unavailable", "warming", "discontinuity" };
    return Names[(int)State];
}

static metric_state LastMetricError(void)
{
    return GetLastError() == ERROR_ACCESS_DENIED ? METRIC_DENIED : METRIC_UNAVAILABLE;
}

typedef struct process {
	HANDLE Handle;
	DWORD ID;
	TCHAR UserName[UNLEN + 1];
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
    ULONGLONG PreviousCpu;
    ULONGLONG PreviousRead;
    ULONGLONG PreviousWrite;
    ULONGLONG IoReadTotal;
    ULONGLONG IoWriteTotal;
    ULONGLONG SampleTick;
    BOOL PreviousTimesValid;
    BOOL PreviousIoValid;
    metric_state OwnerState, CpuState, MemoryState, IoState, AgeState;
	DWORD TreeDepth;

	struct process *Next;
	struct process *Parent;
	struct process *FirstChild;
} process;

#define PROCLIST_BUF_INCREASE 64

static process *ProcessList;
static DWORD ProcessListSize = PROCLIST_BUF_INCREASE;

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
    if(TaggedProcessListCount == TaggedProcessListSize) {
        DWORD Capacity = TaggedProcessListSize + PROCLIST_BUF_INCREASE;
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

#define SORT_PROCESS_BY_INTEGER(Attribute) \
static int SortProcessBy##Attribute(const void *A, const void *B) \
{ \
    const process *Left = A, *Right = B; \
    int Compare = (Left->Attribute > Right->Attribute) - (Left->Attribute < Right->Attribute); \
    return SortOrder == ASCENDING ? Compare : -Compare; \
}
#define SORT_PROCESS_BY_UINT64(Attribute) SORT_PROCESS_BY_INTEGER(Attribute)

#define SORT_PROCESS_BY_DOUBLE(Attribute)                                               \
static int SortProcessBy##Attribute(const void *A, const void *B)                       \
{												                                        \
	double Diff = (((const process *)A)->Attribute - ((const process *)B)->Attribute);  \
	if (Diff > 0.0) return (SortOrder == ASCENDING) ? 1 : -1;                      		\
	if (Diff < 0.0) return (SortOrder == ASCENDING) ? -1 : 1;                           \
	return 0;                                                                           \
}												                                        \

#define SORT_PROCESS_BY_STRING(Attribute, MaxLength)								\
static int SortProcessBy##Attribute(const void *A, const void *B)						\
{														\
	int Compare = _tcsncicmp(((const process *)A)->Attribute, ((const process *)B)->Attribute, MaxLength);	\
	return (SortOrder == ASCENDING) ? Compare : -Compare;							\
}														\

SORT_PROCESS_BY_INTEGER(ID);
SORT_PROCESS_BY_DOUBLE(PercentProcessorTime);
SORT_PROCESS_BY_UINT64(UsedMemory);
SORT_PROCESS_BY_INTEGER(UpTime);
SORT_PROCESS_BY_INTEGER(BasePriority);
SORT_PROCESS_BY_INTEGER(ThreadCount);
SORT_PROCESS_BY_INTEGER(ParentPID);
SORT_PROCESS_BY_DOUBLE(DiskUsage);
SORT_PROCESS_BY_STRING(ExeName, MAX_PATH);
SORT_PROCESS_BY_STRING(UserName, UNLEN);

static process_sort_type ProcessSortType = SORT_BY_ID;

static DWORD CPUCoreCount;
static double CPUUsage;
static BOOL CPUUsageKnown;

static ULONGLONG SubtractTimes(const FILETIME *A, const FILETIME *B)
{
	LARGE_INTEGER lA, lB;

	lA.LowPart = A->dwLowDateTime;
	lA.HighPart = A->dwHighDateTime;

	lB.LowPart = B->dwLowDateTime;
	lB.HighPart = B->dwHighDateTime;

	return lA.QuadPart - lB.QuadPart;
}

void AddChildProcess(process *ParentProcess, process *Process)
{
	if(ParentProcess == Process) return;

	if (ParentProcess->FirstChild == NULL) {
		ParentProcess->FirstChild = Process;
	} else {
		process *ChildProcess = ParentProcess->FirstChild;
		while (ChildProcess->Next != NULL) {
			ChildProcess = ChildProcess->Next;
			if(ChildProcess == Process) return;
		}
		ChildProcess->Next = Process;
	}

	Process->Parent = ParentProcess;
}

/* The "root" process which does not actually exist, acts as process tree root */
static process RootProcess;

/* Finds and assigns parent and child processes */
void FindParentChildProcesses(void)
{
    memset(&RootProcess, 0, sizeof(RootProcess));
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].TreeDepth = 0;
        ProcessList[i].Next = ProcessList[i].Parent = ProcessList[i].FirstChild = NULL;
    }
    for(DWORD i = 0; i < ProcessCount; ++i) {
        process *Child = &ProcessList[i];
        process *Parent = &RootProcess;
        for(DWORD j = 0; j < ProcessCount; ++j) {
            process *Candidate = &ProcessList[j];
            if(i == j || !Child->ParentPID || Candidate->ID != Child->ParentPID) continue;
            if(Child->CreationTime && Candidate->CreationTime && Candidate->CreationTime > Child->CreationTime) continue;
            process *Ancestor = Candidate;
            DWORD Steps = 0;
            while(Ancestor && Ancestor != Child && ++Steps <= ProcessCount) Ancestor = Ancestor->Parent;
            if(Ancestor == Child || Steps > ProcessCount) continue;
            Parent = Candidate;
            break;
        }
        AddChildProcess(Parent, Child);
    }
    for(DWORD i = 0; i < ProcessCount; ++i) {
        process *Parent = ProcessList[i].Parent;
        while(Parent && Parent != &RootProcess && ProcessList[i].TreeDepth < ProcessCount) {
            ++ProcessList[i].TreeDepth;
            Parent = Parent->Parent;
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

		FindParentChildProcesses();
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

static void SelectProcess(DWORD Index)
{
    if(!ProcessCount) { ProcessIndex = SelectedProcessIndex = 0; return; }
    DWORD Visible = max(VisibleProcessCount, 1);
    SelectedProcessIndex = min(Index, ProcessCount - 1);
    if(SelectedProcessIndex < ProcessIndex) ProcessIndex = SelectedProcessIndex;
    if(SelectedProcessIndex - ProcessIndex >= Visible)
        ProcessIndex = SelectedProcessIndex - Visible + 1;
}

static void ReadjustCursor(void)
{
    if(!ProcessCount) { ProcessIndex = SelectedProcessIndex = 0; FollowProcess = FALSE; return; }
    DWORD Visible = max(VisibleProcessCount, 1);
    DWORD MaxTop = ProcessCount > Visible ? ProcessCount - Visible : 0;
    ProcessIndex = min(ProcessIndex, MaxTop);
    SelectedProcessIndex = min(SelectedProcessIndex, ProcessCount - 1);
    if(FollowProcess) {
        BOOL Found = FALSE;
        for(DWORD i = 0; i < ProcessCount; ++i) {
            if(ProcessList[i].ID == FollowProcessID && ProcessList[i].CreationTime == FollowCreationTime) { SelectProcess(i); Found = TRUE; break; }
        }
        if(!Found) FollowProcess = FALSE;
    }
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
	if(!SearchActive || !ProcessCount) return;

	for(DWORD i = SelectedProcessIndex + 1; i < ProcessCount; ++i) {
		const process *Process = &ProcessList[i];
		if(SearchMatchesProcess(Process)) {
			SelectProcess(i);
			return;
		}
	}

	SetViMessage(VI_NOTICE, _T("search hit BOTTOM, continuing at TOP"));

	for(DWORD i = 0; i <= SelectedProcessIndex; ++i) {
		const process *Process = &ProcessList[i];
		if(SearchMatchesProcess(Process)) {
			SelectProcess(i);
			return;
		}
	}

	SetViMessage(VI_ERROR, _T("Pattern not found: %s"), SearchPattern);
}

static void SearchPrevious(void)
{
	if(!SearchActive || !ProcessCount) return;

	for(DWORD i = SelectedProcessIndex; i > 0; --i) {
		const process *Process = &ProcessList[i - 1];
		if(SearchMatchesProcess(Process)) {
			SelectProcess(i - 1);
			return;
		}
	}

	SetViMessage(VI_NOTICE, _T("search hit TOP, continuing at BOTTOM"));

	for(DWORD i = ProcessCount; i > SelectedProcessIndex; --i) {
		const process *Process = &ProcessList[i - 1];
		if(SearchMatchesProcess(Process)) {
			SelectProcess(i - 1);
			return;
		}
	}

	SetViMessage(VI_ERROR, _T("Pattern not found: %s"), SearchPattern);
}

static ULONGLONG FileTimeValue(const FILETIME *Value)
{
    return ((ULONGLONG)Value->dwHighDateTime << 32) | Value->dwLowDateTime;
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

/* Private sample handles and buffers never escape the collector. */
static BOOL PollProcessList(DWORD UpdateTime)
{
    FILETIME SnapshotStarted;
    GetSystemTimeAsFileTime(&SnapshotStarted);
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(Snapshot == INVALID_HANDLE_VALUE) { CollectorError = GetLastError(); return FALSE; }
    DWORD Capacity = PROCLIST_BUF_INCREASE, Count = 0;
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
        Process.ThreadCount = Entry.cntThreads;
        Process.BasePriority = Entry.pcPriClassBase;
        Process.ParentPID = Entry.th32ParentProcessID;
        _tcsncpy_s(Process.ExeName, _countof(Process.ExeName), Entry.szExeFile, _TRUNCATE);
        _tcscpy_s(Process.UserName, _countof(Process.UserName), _T("?"));
        Process.OwnerState = Process.CpuState = Process.MemoryState = Process.IoState = Process.AgeState = METRIC_UNAVAILABLE;
        if(!MatchesStaticFilter(&Process)) continue;
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
                if(GetLastError() == ERROR_INSUFFICIENT_BUFFER && Size) {
                    TOKEN_USER *User = malloc(Size);
                    if(User && GetTokenInformation(Token, TokenUser, User, Size, &Size)) {
                        TCHAR Domain[256];
                        DWORD NameSize = _countof(Process.UserName), DomainSize = _countof(Domain);
                        SID_NAME_USE Use;
                        if(LookupAccountSid(NULL, User->User.Sid, Process.UserName, &NameSize, Domain, &DomainSize, &Use))
                            Process.OwnerState = METRIC_OK;
                        else Process.OwnerState = LastMetricError();
                    } else Process.OwnerState = User ? LastMetricError() : METRIC_UNAVAILABLE;
                    free(User);
                } else Process.OwnerState = LastMetricError();
                CloseHandle(Token);
            } else Process.OwnerState = LastMetricError();
            if(Process.OwnerState != METRIC_OK) _tcscpy_s(Process.UserName, _countof(Process.UserName), _T("?"));
        } else {
            Process.OwnerState = Process.CpuState = Process.MemoryState = Process.IoState = Process.AgeState = OpenState;
        }
        if(FilterByUserName && (Process.OwnerState != METRIC_OK || _tcsicmp(Process.UserName, FilterUserName))) {
            if(Process.Handle) CloseHandle(Process.Handle);
            continue;
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
    BOOL HaveSystemBefore = GetSystemTimes(&PrevIdle, &PrevKernel, &PrevUser);
    ULONGLONG Started = GetTickCount64();
    for(DWORD i = 0; i < Count && !ShouldStop(); ++i) {
        process *Process = &Sample[i];
        if(!Process->Handle) continue;
        FILETIME Created, Exited, Kernel, User;
        if(GetProcessTimes(Process->Handle, &Created, &Exited, &Kernel, &User)) {
            Process->CreationTime = FileTimeValue(&Created);
            Process->PreviousCpu = FileTimeValue(&Kernel) + FileTimeValue(&User);
            Process->PreviousTimesValid = TRUE;
            Process->CpuState = METRIC_WARMING;
        } else Process->CpuState = Process->AgeState = LastMetricError();
        IO_COUNTERS Io;
        if(GetProcessIoCounters(Process->Handle, &Io)) {
            Process->PreviousRead = Io.ReadTransferCount;
            Process->PreviousWrite = Io.WriteTransferCount;
            Process->PreviousIoValid = TRUE;
            Process->IoState = METRIC_WARMING;
        } else Process->IoState = LastMetricError();
        Process->SampleTick = GetTickCount64();
    }
    if(WaitForSingleObject(ShutdownEvent, UpdateTime) != WAIT_TIMEOUT) {
        CloseSampleHandles(Sample, Count); free(Sample); return FALSE;
    }
    FILETIME Idle, Kernel, User;
    BOOL HaveSystemAfter = GetSystemTimes(&Idle, &Kernel, &User);
    ULONGLONG SystemDelta = 0, IdleDelta = 0;
    if(HaveSystemBefore && HaveSystemAfter && FileTimeValue(&Kernel) >= FileTimeValue(&PrevKernel) &&
       FileTimeValue(&User) >= FileTimeValue(&PrevUser) && FileTimeValue(&Idle) >= FileTimeValue(&PrevIdle)) {
        SystemDelta = FileTimeValue(&Kernel) - FileTimeValue(&PrevKernel) + FileTimeValue(&User) - FileTimeValue(&PrevUser);
        IdleDelta = FileTimeValue(&Idle) - FileTimeValue(&PrevIdle);
    }
    DWORD Running = 0;
    for(DWORD i = 0; i < Count && !ShouldStop(); ++i) {
        process *Process = &Sample[i];
        if(!Process->Handle) continue;
        FILETIME Created, Exited, ProcessKernel, ProcessUser, Now;
        if(GetProcessTimes(Process->Handle, &Created, &Exited, &ProcessKernel, &ProcessUser)) {
            ULONGLONG Total = FileTimeValue(&ProcessKernel) + FileTimeValue(&ProcessUser);
            GetSystemTimeAsFileTime(&Now);
            Process->CreationTime = FileTimeValue(&Created);
            if(FileTimeValue(&Now) >= Process->CreationTime) {
                ULONGLONG EndTime = FileTimeValue(&Exited) ? FileTimeValue(&Exited) : FileTimeValue(&Now);
                Process->UpTime = EndTime >= Process->CreationTime ? (EndTime - Process->CreationTime) / 10000 : 0;
                Process->AgeState = METRIC_OK;
            } else Process->AgeState = METRIC_DISCONTINUITY;
            if(Process->PreviousTimesValid && SystemDelta && Total >= Process->PreviousCpu) {
                Process->PercentProcessorTime = 100.0 * (double)(Total - Process->PreviousCpu) / (double)SystemDelta;
                Process->CpuState = METRIC_OK;
                if(Process->PercentProcessorTime >= 0.01) ++Running;
            } else Process->CpuState = Process->PreviousTimesValid ? METRIC_DISCONTINUITY : METRIC_WARMING;
        } else Process->CpuState = Process->AgeState = LastMetricError();
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
        CloseHandle(Process->Handle);
        Process->Handle = NULL;
    }
    CloseSampleHandles(Sample, Count);
    if(ShouldStop()) { free(Sample); return FALSE; }
    ULONGLONG SampleElapsed = GetTickCount64() - Started;
    EnterCriticalSection(&SyncLock);
    DWORD SelectedId = ProcessCount && SelectedProcessIndex < ProcessCount ? ProcessList[SelectedProcessIndex].ID : 0;
    ULONGLONG SelectedCreated = ProcessCount && SelectedProcessIndex < ProcessCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
    if(Count > ProcessListSize) {
        process *Larger = realloc(ProcessList, (size_t)Count * sizeof(*ProcessList));
        if(!Larger) {
            LeaveCriticalSection(&SyncLock); free(Sample); CollectorError = ERROR_NOT_ENOUGH_MEMORY; return FALSE;
        }
        ProcessList = Larger; ProcessListSize = Count;
    }
    memcpy(ProcessList, Sample, (size_t)Count * sizeof(*ProcessList));
    ProcessCount = Count;
    RunningProcessCount = Running;
    CPUUsageKnown = SystemDelta && IdleDelta <= SystemDelta;
    if(CPUUsageKnown) CPUUsage = (double)(SystemDelta - IdleDelta) / (double)SystemDelta;
    LastSampleElapsed = SampleElapsed;
    SortProcessList();
    ReadjustCursor();
    if(!FollowProcess && SelectedId) {
        for(DWORD i = 0; i < ProcessCount; ++i) {
            if(ProcessList[i].ID == SelectedId && ProcessList[i].CreationTime == SelectedCreated) {
                SelectProcess(i);
                break;
            }
        }
    }
    LeaveCriticalSection(&SyncLock);
    free(Sample);
    return !ShouldStop();
}

static void DisableCursor(void)
{
	CONSOLE_CURSOR_INFO CursorInfo;
	GetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
	CursorInfo.bVisible = FALSE; /* Note: this has to be set every time console buffer resizes! */
	SetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
}

static WORD GetConsoleColor(void)
{
	CONSOLE_SCREEN_BUFFER_INFO Csbi;
	GetConsoleScreenBufferInfo(ConsoleHandle, &Csbi);
	return Csbi.wAttributes;
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

	if(Width != OldWidth || Height != OldHeight) {
		DisableCursor();
		COORD Size;
		Size.X = (USHORT)Width;
		Size.Y = (USHORT)Height;
		SetConsoleScreenBufferSize(ConsoleHandle, Size);

		OldWidth = Width;
		OldHeight = Height;

		return TRUE;
	}

	SizeX = (int)Csbi.dwSize.X;
	SizeY = (int)Csbi.dwSize.Y;

	if(SavedAttributes == 0)
		SavedAttributes = Csbi.wAttributes;

	return FALSE;
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

static void PollInitialSystemInfo(void)
{
    SYSTEM_INFO SystemInfo;
    GetNativeSystemInfo(&SystemInfo);
    CPUCoreCount = SystemInfo.dwNumberOfProcessors;
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
    if(SystemMemoryKnown) {
        TotalMemory = TO_MB(MemoryInfo.ullTotalPhys);
        UsedMemory = TO_MB(MemoryInfo.ullTotalPhys - MemoryInfo.ullAvailPhys);
        UsedMemoryPerc = MemoryInfo.ullTotalPhys ? (double)(MemoryInfo.ullTotalPhys - MemoryInfo.ullAvailPhys) / (double)MemoryInfo.ullTotalPhys : 0;
        TotalPageMemory = TO_MB(MemoryInfo.ullTotalPageFile);
        UsedPageMemory = TO_MB(MemoryInfo.ullTotalPageFile - MemoryInfo.ullAvailPageFile);
        UsedPageMemoryPerc = MemoryInfo.ullTotalPageFile ? (double)(MemoryInfo.ullTotalPageFile - MemoryInfo.ullAvailPageFile) / (double)MemoryInfo.ullTotalPageFile : 0;
    }
    UpTime = GetTickCount64();
}

DWORD WINAPI PollProcessListThreadProc(LPVOID lpParam)
{
	UNREFERENCED_PARAMETER(lpParam);

	while(!ShouldStop()) {
        if(!PollProcessList((DWORD)Config.RedrawInterval)) {
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

static void FormatMemoryString(TCHAR *Buffer, DWORD BufferSize, unsigned __int64 Memory)
{
	double Value;
	TCHAR Unit[255];

	if (Memory < 1000ULL*1000) {
		Value = Memory / 1000.0;
		_tcscpy_s(Unit, _countof(Unit), _T("KB"));
	} else if (Memory < 1000ULL*1000*1000) {
		Value = Memory / (1000*1000.0);
		_tcscpy_s(Unit, _countof(Unit), _T("MB"));
	} else if (Memory < 1000ULL*1000*1000*1000) {
		Value = Memory / (1000*1000*1000.0);
		_tcscpy_s(Unit, _countof(Unit), _T("GB"));
	} else {
		Value = Memory / (1000*1000*1000*1000.0);
		_tcscpy_s(Unit, _countof(Unit), _T("TB"));
	}

	_stprintf_s(Buffer, BufferSize, _T("% 8.1f %s"), Value, Unit);
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

typedef struct process_list_column {
	TCHAR *Name;
	int Width;
} process_list_column;

static void DrawProcessListHeader(const process_list_column *Columns, int Count)
{
	int CharsWritten = 0;
	for(int i = 0; i < Count; i++) {
		if(i == (int)ProcessSortType) {
			SetColor(Config.BGHighlightColor);
		} else {
			SetColor(Config.ProcessListHeaderColor);
		}
		CharsWritten += ConPrintf(_T("%*s  "), Columns[i].Width, Columns[i].Name);
	}

	for(; CharsWritten < Width; CharsWritten++) {
		ConPutc(_T(' '));
	}
}

typedef struct options_column {
	TCHAR *Key;
	TCHAR *Name;
} options_column;

static void DrawOptions(const options_column *Columns, int Count)
{
	int CharsWritten = 0;
	for(int i = 0; i < Count; i++) {
		SetColor(Config.FGColor);
		CharsWritten += ConPrintf(_T("%s"), Columns[i].Key);
		SetColor(Config.BGHighlightColor);
		CharsWritten += ConPrintf(_T("%-4s"), Columns[i].Name);
	}

	for(; CharsWritten < Width; CharsWritten++) {
		ConPutc(_T(' '));
	}
}

#define BAR_WIDTH 25

static int DrawPercentageBar(TCHAR *Name, double Percentage, WORD Color)
{
	int CharsWritten = 0;

	SetColor(Config.FGHighlightColor);
	CharsWritten += ConPrintf(_T("  %s"), Name);
	SetColor(Config.FGColor);
	ConPutc(_T('['));
	CharsWritten++;

	BOOL Known = Percentage >= 0;
    if(Known) Percentage = min(1.0, Percentage);
    int Bars = Known ? (int)((double)BAR_WIDTH * Percentage) : 0;
	SetColor(Color);
	for(int i = 0; i < Bars; i++) {
		ConPutc(_T('|'));
	}
	CharsWritten+= Bars;
	SetColor(Config.FGColor);
	for(int i = 0; i < BAR_WIDTH - Bars; i++) {
		ConPutc(_T(' '));
	}
	CharsWritten += BAR_WIDTH - Bars;
	SetColor(Config.BGColor);
	CharsWritten += Known ? ConPrintf(_T("%04.1f%%"), 100.0 * Percentage) : ConPrintf(_T("    ?"));
	SetColor(Config.FGColor);
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
    WORD Color = Config.FGColor;
    BOOL Selected = IsProcessTagged(Process);
    if(Highlighted) Color = Selected ? Config.BGColor | Config.BGHighlightColor : Config.BGHighlightColor;
    else if(Selected) Color = Config.BGColor;
    SetColor(Color);
    TCHAR Age[32], Memory[64], Cpu[32], Io[64];
    if(Process->AgeState == METRIC_OK) FormatTimeString(Age, _countof(Age), Process->UpTime);
    else _tcscpy_s(Age, _countof(Age), _T("?"));
    if(Process->MemoryState == METRIC_OK) FormatMemoryString(Memory, _countof(Memory), Process->UsedMemory);
    else _tcscpy_s(Memory, _countof(Memory), _T("?"));
    if(Process->CpuState == METRIC_OK) _stprintf_s(Cpu, _countof(Cpu), _T("%5.1f"), Process->PercentProcessorTime);
    else _tcscpy_s(Cpu, _countof(Cpu), _T("?"));
    if(Process->IoState == METRIC_OK) _stprintf_s(Io, _countof(Io), _T("%.1f MB/s"), Process->DiskUsage / 1000000.0);
    else _tcscpy_s(Io, _countof(Io), _T("?"));
    int CharsWritten = ConPrintf(_T("%7u  %9.9s  %3u  %5s  %11s  %4u  %9s  %14s  "),
        Process->ID, Process->UserName, Process->BasePriority, Cpu, Memory, Process->ThreadCount, Io, Age);
    if(ProcessSortType == SORT_BY_TREE) {
        DWORD Depth = min(Process->TreeDepth, (DWORD)max(0, Width / 3));
        for(DWORD i = 0; i < Depth; ++i) CharsWritten += ConPrintf(_T("|  "));
    }
    CharsWritten += ConPrintf(_T("%s"), Process->ExeName);
    if(InteractiveMode && CharsWritten < Width) ConPrintf(_T("%*c"), Width - CharsWritten, _T(' '));
}

static ULONGLONG KeyPressStart = 0;
static ULONGLONG LastKeyPress = 0;
static BOOL KeyPress = FALSE;
static DWORD OldSelectedProcessIndex = 0;
static BOOL RedrawAtCursor = FALSE;

typedef enum scroll_type {
	SCROLL_UP,
	SCROLL_DOWN,
	SCROLL_PAGE_UP,
	SCROLL_PAGE_DOWN,
} scroll_type;

static void DoScroll(scroll_type ScrollType, BOOL *Redraw)
{
    if(!ProcessCount) return;
	ULONGLONG Now = GetTickCount64();
	FollowProcess = FALSE;

	if(!KeyPress) {
		KeyPress = TRUE;
		KeyPressStart = Now;
	}

	/*
	 * In order to enforce a one-time delay when scrolling, scroll only
	 * if this is the instant we pressed the key or at any time 500ms thereafter.
	 */
	if(Now == KeyPressStart || Now - KeyPressStart > 500) {
		BOOL Scrolled = FALSE;

		OldSelectedProcessIndex = SelectedProcessIndex;
		switch(ScrollType) {
		case SCROLL_UP:
			if(SelectedProcessIndex != 0) {
				Scrolled = TRUE;
				OldSelectedProcessIndex = SelectedProcessIndex;
				SelectedProcessIndex--;
				if(SelectedProcessIndex <= ProcessIndex - 1 && ProcessIndex != 0) {
					ProcessIndex--;
					*Redraw = TRUE;
				}
			}
			break;
		case SCROLL_DOWN:
			if(SelectedProcessIndex != ProcessCount-1) {
				Scrolled = TRUE;
				OldSelectedProcessIndex = SelectedProcessIndex;
				SelectedProcessIndex++;
				if(SelectedProcessIndex - ProcessIndex >= VisibleProcessCount) {
					if(ProcessIndex <= ProcessCount - ProcessWindowHeight + 1) {
						ProcessIndex++;
						*Redraw = TRUE;
					}
				}
			}
			break;
		case SCROLL_PAGE_UP:
			if(SelectedProcessIndex != 0) {
				Scrolled = TRUE;
				OldSelectedProcessIndex = SelectedProcessIndex;
				if(SelectedProcessIndex > VisibleProcessCount)
					SelectedProcessIndex -= VisibleProcessCount;
				else
					SelectedProcessIndex = 0;
				if(SelectedProcessIndex <= ProcessIndex - 1 && ProcessIndex != 0) {
					ProcessIndex = max(0, SelectedProcessIndex);
					*Redraw = TRUE;
				}
			}
			break;
		case SCROLL_PAGE_DOWN:
			if(SelectedProcessIndex != ProcessCount-1) {
				Scrolled = TRUE;
				OldSelectedProcessIndex = SelectedProcessIndex;
				SelectedProcessIndex += VisibleProcessCount;
				if(SelectedProcessIndex > ProcessCount - 1)
					SelectedProcessIndex = ProcessCount - 1;
				if(SelectedProcessIndex - ProcessIndex >= VisibleProcessCount) {
					ProcessIndex = min(ProcessCount > VisibleProcessCount ? ProcessCount - VisibleProcessCount : 0, SelectedProcessIndex);
					*Redraw = TRUE;
				}
			}
			break;
		}

		if(Scrolled) {
			RedrawAtCursor = TRUE;
			LastKeyPress = GetTickCount64();
		}
	}
}

static const char HelpText[] =
    "Wtop " WTOP_VERSION_STRING " - NTop-derived Windows terminal process monitor\n"
    "Usage: wtop [--tui | --snapshot | --gui] [options]\n"
    "  --tui                 Interactive TUI (default; requires console input/output)\n"
    "  --snapshot, --dump, -d One read-only snapshot; exits after two observations\n"
    "  --format text|json    Snapshot or informational output; alone selects snapshot\n"
    "  --interval MS         Sample/redraw interval: 50..60000 ms (default 1000)\n"
    "  --help, -h            Help without initializing console or collector\n"
    "  --version, -v         Version and build profile\n"
    "  --capabilities        Build capabilities; field availability is runtime-specific\n"
    "  --gui                 Reserved; native GUI is not implemented\n"
    "  --config PATH         Read configuration from this explicit UTF-8 path\n"
    "  --no-config           Ignore configuration files\n"
    "  -C                    Monochrome TUI\n"
    "  -p PID,PID            Filter process IDs (maximum 1024)\n"
    "  -n PART,PART          Case-sensitive name substrings (maximum 10)\n"
    "  -u USER               Exact account-name filter, case-insensitive\n"
    "  -s COLUMN             ID USER PRI CPU% MEM THRD I/O AGE PROCESS\n"
    "                        DISK and TIME remain aliases for I/O and AGE\n"
    "TUI: arrows/j/k, PgUp/PgDn, g/G; Ctrl+Left/Right sort; M/P sort memory/CPU;\n"
    "     Space tag, U untag, K terminate tags, I reverse, F follow, / search, n/N;\n"
    "     q/F10/Ctrl+C quit. Commands: :q :quit :tree :sort :search :kill :exec.\n"
    "I/O is process read+write I/O, not physical-disk attribution. AGE is wall time.\n"
    "Cmt is commit use/limit, not page-file residency. ?/JSON null means unavailable.\n"
    "No service, persistent collector, updater or startup entry is installed.\n";

static int PrintInformation(const wtop_options *Options)
{
    if(Options->action == WTOP_HELP) {
        if(Options->json) { fputs("{\"schema\":1,\"help\":", stdout); wtop_json_string(stdout, HelpText); puts("}"); }
        else fputs(HelpText, stdout);
    } else if(Options->action == WTOP_VERSION) {
        if(Options->json) printf("{\"schema\":1,\"name\":\"wtop\",\"version\":\"%s\",\"profile\":\"%s\"}\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
        else printf("Wtop %s (%s)\nDerived from NTop; Copyright (c) 2019 Gian Sass and Wtop contributors.\nGPL-3.0-only. Historical OS support is not verified.\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
    } else {
        if(Options->json) printf("{\"schema\":1,\"kind\":\"build-capabilities\",\"version\":\"%s\",\"profile\":\"%s\",\"runtime_probe\":false,\"tui\":true,\"snapshot\":true,\"gui\":false,\"install\":false,\"gpu\":false,\"native_gui\":false,\"encoding\":\"utf-8\",\"metrics\":[\"cpu_share_percent\",\"working_set_bytes\",\"io_read_write_bytes_per_second\",\"age_ms\",\"owner\"]}\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
        else printf("Wtop %s: build capabilities (%s; no runtime probe)\nTUI, text/JSON snapshot, process tree/search/sort and NTop commands.\nMetric access varies by process rights; unavailable fields carry status/null.\nGUI, installers, GPU telemetry and historical OS ports are not implemented.\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}

int GetProcessSortTypeFromName(const TCHAR *Name, process_sort_type *Dest)
{
	if(!lstrcmpi(Name, _T("ID"))) {
		*Dest = SORT_BY_ID;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("USER"))) {
		*Dest = SORT_BY_USER_NAME;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("PRI"))) {
		*Dest = SORT_BY_PRIORITY;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("CPU%"))) {
		*Dest = SORT_BY_PROCESSOR_TIME;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("MEM"))) {
		*Dest = SORT_BY_USED_MEMORY;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("THRD"))) {
		*Dest = SORT_BY_THREAD_COUNT;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("TIME")) || !lstrcmpi(Name, _T("AGE"))) {
		*Dest = SORT_BY_UPTIME;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("PROCESS"))) {
		*Dest = SORT_BY_PROCESS;
		return TRUE;
	} else if(!lstrcmpi(Name, _T("DISK")) || !lstrcmpi(Name, _T("I/O"))) {
		*Dest = SORT_BY_DISK_USAGE;
		return TRUE;
	}

	return FALSE;
}

void ChangeProcessSortType(process_sort_type NewProcessSortType)
{
	EnterCriticalSection(&SyncLock);
	ProcessSortType = NewProcessSortType;
	SortProcessList();
	ReadjustCursor();
	LeaveCriticalSection(&SyncLock);
}

static vi_message_type CurrentViMessageType = VI_NOTICE;
static TCHAR *ViMessage;

void SetViMessage(vi_message_type MessageType, TCHAR *Fmt, ...)
{
	CurrentViMessageType = MessageType;

	va_list VaList;

	va_start(VaList, Fmt);
	_vsntprintf_s(ViMessage, DEFAULT_STR_SIZE, _TRUNCATE, Fmt, VaList);
	va_end(VaList);
}

static BOOL ViMessageActive(void)
{
	return _tcslen(ViMessage) > 0;
}

static void WriteVi(void)
{
	SetConCursorPos(0, (SHORT)Height-1);

	if(ViMessageActive()) {
		switch(CurrentViMessageType) {
		case VI_NOTICE:
			// Default?
			break;
		case VI_ERROR:
			SetColor(Config.ErrorColor);
			break;
		}

		int CharsWritten = ConPrintf(_T("%s"), ViMessage);

		for (; CharsWritten < Width + 1; CharsWritten++) {
			ConPutc(_T(' '));
		}
	} else {
		SetColor(FOREGROUND_WHITE);
		WriteBlankLine();
	}
}

static void HideViMessage(void)
{
	ViMessage[0] = _T('\0');
	WriteVi();
}

void ClearViMessage(void)
{
	if(ViMessageActive()) {
		SetConsoleMode(ConsoleHandle, ENABLE_PROCESSED_OUTPUT);
		HideViMessage();
		SetConsoleMode(ConsoleHandle, ENABLE_PROCESSED_OUTPUT);
	}
}

static int KillIdentity(DWORD Pid, ULONGLONG CreationTime)
{
    if(!CreationTime) { SetViMessage(VI_ERROR, _T("Cannot act on PID %lu: creation identity unavailable"), Pid); return 0; }
    HANDLE Handle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
    if(!Handle) { SetViMessage(VI_ERROR, _T("Cannot open PID %lu: error %lu"), Pid, GetLastError()); return 0; }
    FILETIME Created, Exited, Kernel, User;
    int Result = 0;
    if(!GetProcessTimes(Handle, &Created, &Exited, &Kernel, &User) || FileTimeValue(&Created) != CreationTime)
        SetViMessage(VI_ERROR, _T("PID %lu changed or its identity cannot be revalidated"), Pid);
    else if(!TerminateProcess(Handle, 9)) SetViMessage(VI_ERROR, _T("Cannot terminate PID %lu: error %lu"), Pid, GetLastError());
    else Result = 1;
    CloseHandle(Handle);
    return Result;
}

int KillObservedProcess(DWORD Pid)
{
    for(DWORD i = 0; i < ProcessCount; ++i)
        if(ProcessList[i].ID == Pid) return KillIdentity(Pid, ProcessList[i].CreationTime);
    SetViMessage(VI_ERROR, _T("PID %lu is not in the observed process set"), Pid);
    return 0;
}

static void KillTaggedProcesses(void)
{
    for(DWORD i = 0; i < TaggedProcessListCount; ++i)
        KillIdentity(TaggedProcessList[i].ID, TaggedProcessList[i].CreationTime);
    TaggedProcessListCount = 0;
}

static ULONGLONG CaretTicks;
static BOOLEAN CaretState;

static void ResetCaret(void)
{
	CaretTicks = GetTickCount64();
	CaretState = TRUE;
}

static BOOL CTRLState;

static BOOL SelectionMatchesDisplay(void)
{
    if(ProcessCount && !ProcessList[SelectedProcessIndex].CreationTime) {
        SetViMessage(VI_ERROR, _T("Cannot act on this selection: creation identity is unavailable"));
        return FALSE;
    }
    if(ProcessCount && ProcessList[SelectedProcessIndex].ID == DisplayedProcessID &&
       ProcessList[SelectedProcessIndex].CreationTime == DisplayedCreationTime) return TRUE;
    SetViMessage(VI_ERROR, _T("Process selection changed; review the refreshed row before acting"));
    return FALSE;
}

static void ProcessInput(BOOL *Redraw)
{
	DWORD NumEvents = 0, Num = 0;

	if(!GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &NumEvents)) { RequestShutdown(); return; }
    NumEvents = min(NumEvents, 128);

	if(NumEvents == 0) {
		return;
	}

	INPUT_RECORD *Records = malloc((size_t)NumEvents * sizeof(*Records));
    if(!Records) { CollectorError = ERROR_NOT_ENOUGH_MEMORY; RequestShutdown(); return; }
	if(!ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), Records, NumEvents, &Num)) { free(Records); RequestShutdown(); return; }

	for(DWORD i = 0; i < Num; i++) {
		INPUT_RECORD InputRecord = Records[i];
		if (InputRecord.EventType == KEY_EVENT) {
			if (InputRecord.Event.KeyEvent.bKeyDown) {
				if(!InInputMode) {
					switch(InputRecord.Event.KeyEvent.wVirtualKeyCode) {
					case VK_UP:
						DoScroll(SCROLL_UP, Redraw);
						break;
					case VK_DOWN:
						DoScroll(SCROLL_DOWN, Redraw);
						break;
					case VK_PRIOR:
						DoScroll(SCROLL_PAGE_UP, Redraw);
						break;
					case VK_NEXT:
						DoScroll(SCROLL_PAGE_DOWN, Redraw);
						break;
					case VK_SPACE:
						if(SelectionMatchesDisplay()) ToggleTaggedProcess(&ProcessList[SelectedProcessIndex]);
						RedrawAtCursor = TRUE;
						OldSelectedProcessIndex = SelectedProcessIndex;
						DoScroll(SCROLL_DOWN, Redraw);
						break;
					case VK_CONTROL:
						CTRLState = TRUE;
						break;
					case VK_LEFT:
						if(CTRLState) {
							if(ProcessSortType == 0) {
								ProcessSortType = SORT_TYPE_MAX - 1;
							} else {
								--ProcessSortType;
							}
							ChangeProcessSortType(ProcessSortType);
							*Redraw = TRUE;
						}
						break;
					case VK_RIGHT:
						if(CTRLState) {
							if(ProcessSortType == SORT_TYPE_MAX - 1) {
								ProcessSortType = 0;
							} else {
								++ProcessSortType;
							}
							ChangeProcessSortType(ProcessSortType);
							*Redraw = TRUE;
						}
						break;
					case VK_F10:
						RequestShutdown();
                        break;
					default:
						switch(InputRecord.Event.KeyEvent.uChar.UnicodeChar) {
						case 'K':
							KillTaggedProcesses();
							break;
						case 'g':
							ProcessIndex = SelectedProcessIndex = 0;
							ClearViMessage();
							*Redraw = TRUE;
							break;
						case 'G':
							SelectedProcessIndex = ProcessCount ? ProcessCount - 1 : 0;
                            ProcessIndex = ProcessCount > VisibleProcessCount ? ProcessCount - VisibleProcessCount : 0;
							ClearViMessage();
							*Redraw = TRUE;
							break;
						case 'I':
							EnterCriticalSection(&SyncLock);
							if(SortOrder == ASCENDING)
								SortOrder = DESCENDING;
							else
								SortOrder = ASCENDING;
							SortProcessList();
							ReadjustCursor();
							LeaveCriticalSection(&SyncLock);
							*Redraw = TRUE;
							break;
						case 'F':
                            if(!SelectionMatchesDisplay()) { *Redraw = TRUE; break; }
							FollowProcess = TRUE;
							FollowProcessID = ProcessCount ? ProcessList[SelectedProcessIndex].ID : 0;
                            FollowCreationTime = ProcessCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
							break;
						case 'U':
							TaggedProcessListCount = 0;
							*Redraw = TRUE;
							break;
						case '/':
							ViEnableInput(_T('/'));
							ResetCaret();
							*Redraw = TRUE;
							break;
						case ':':
							ViEnableInput(_T(':'));
							ResetCaret();
							*Redraw = TRUE;
							break;
						case 'n':
							SearchNext();
							*Redraw = TRUE;
							break;
						case 'N':
							SearchPrevious();
							*Redraw = TRUE;
							break;
						case 'j':
							DoScroll(SCROLL_DOWN, Redraw);
							break;
						case 'k':
							DoScroll(SCROLL_UP, Redraw);
							break;
						// Top compatibility keys:
						case 'M':
							ChangeProcessSortType(SORT_BY_USED_MEMORY);
							*Redraw = TRUE;
							break;
						case 'P':
							ChangeProcessSortType(SORT_BY_PROCESSOR_TIME);
							*Redraw = TRUE;
							break;
						case 'q':
							RequestShutdown();
						}
						break;
					}
				} else {
					if(ViHandleInputKey(&InputRecord.Event.KeyEvent)) {
						*Redraw = TRUE;
					}
				}
			} else {
				if(!InInputMode) {
					if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_CONTROL) {
						CTRLState = FALSE;
					}
				}
			}
		}
	}

	free(Records);
}

static BOOL ApplyOptions(const wtop_options *Options)
{
    if(Options->sort_column) {
        TCHAR *Value = FromUtf8(Options->sort_column);
        BOOL Ok = Value && GetProcessSortTypeFromName(Value, &ProcessSortType);
        free(Value);
        if(!Ok) { fputs("wtop: invalid sort column\n", stderr); return FALSE; }
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

static BOOL JsonTString(const TCHAR *Text)
{
    char *Utf8 = ToUtf8(Text);
    if(!Utf8) return FALSE;
    BOOL Ok = wtop_json_string(stdout, Utf8);
    free(Utf8);
    return Ok;
}

static void JsonMetricDouble(const char *Name, metric_state State, double Value)
{
    printf(",\"%s\":{\"status\":\"%s\",\"value\":", Name, MetricStateName(State));
    if(State == METRIC_OK) printf("%.6f", Value); else fputs("null", stdout);
    fputc('}', stdout);
}

static void JsonMetricInteger(const char *Name, metric_state State, ULONGLONG Value)
{
    printf(",\"%s\":{\"status\":\"%s\",\"value\":", Name, MetricStateName(State));
    if(State == METRIC_OK) printf("\"%llu\"", (unsigned long long)Value); else fputs("null", stdout);
    fputc('}', stdout);
}

static int WriteSnapshot(BOOL Json)
{
    if(Json) {
        printf("{\"schema\":1,\"version\":\"%s\",\"profile\":\"%s\",\"sample_elapsed_ms\":%llu,"
               "\"cpu_normalization\":\"share-of-GetSystemTimes-aggregate; processor-group limits apply\","
               "\"io_scope\":\"process read+write I/O; not physical-disk attribution\",\"processes\":[",
               WTOP_VERSION_STRING, WTOP_BUILD_PROFILE, (unsigned long long)LastSampleElapsed);
        for(DWORD i = 0; i < ProcessCount; ++i) {
            const process *Process = &ProcessList[i];
            if(i) fputc(',', stdout);
            printf("{\"pid\":%lu,\"parent_pid\":%lu,\"threads\":%lu,\"base_priority\":%lu,\"name\":",
                   Process->ID, Process->ParentPID, Process->ThreadCount, Process->BasePriority);
            if(!JsonTString(Process->ExeName)) return 1;
            fputs(",\"creation_filetime\":", stdout);
            if(Process->CreationTime) printf("\"%llu\"", (unsigned long long)Process->CreationTime); else fputs("null", stdout);
            printf(",\"owner\":{\"status\":\"%s\",\"value\":", MetricStateName(Process->OwnerState));
            if(Process->OwnerState == METRIC_OK) { if(!JsonTString(Process->UserName)) return 1; }
            else fputs("null", stdout);
            fputc('}', stdout);
            JsonMetricDouble("cpu_share_percent", Process->CpuState, Process->PercentProcessorTime);
            JsonMetricInteger("working_set_bytes", Process->MemoryState, Process->UsedMemory);
            JsonMetricDouble("io_read_write_bytes_per_second", Process->IoState, Process->DiskUsage);
            JsonMetricInteger("age_ms", Process->AgeState, Process->UpTime);
            fputc('}', stdout);
        }
        puts("]}");
    } else {
        printf("Wtop %s - one snapshot (%llu ms measured)\n", WTOP_VERSION_STRING, (unsigned long long)LastSampleElapsed);
        puts("CPU% is aggregate CPU share; I/O is read+write process I/O; AGE is elapsed wall time.");
        puts("PID\tOWNER\tCPU%\tWORKING_SET_BYTES\tIO_BYTES/S\tAGE_MS\tNAME");
        for(DWORD i = 0; i < ProcessCount; ++i) {
            const process *Process = &ProcessList[i];
            char *Name = ToUtf8(Process->ExeName), *Owner = ToUtf8(Process->UserName);
            if(!Name || !Owner) { free(Name); free(Owner); return 1; }
            printf("%lu\t%s\t", Process->ID, Process->OwnerState == METRIC_OK ? Owner : MetricStateName(Process->OwnerState));
            if(Process->CpuState == METRIC_OK) printf("%.2f", Process->PercentProcessorTime); else fputs(MetricStateName(Process->CpuState), stdout);
            fputc('\t', stdout);
            if(Process->MemoryState == METRIC_OK) printf("%llu", (unsigned long long)Process->UsedMemory); else fputs(MetricStateName(Process->MemoryState), stdout);
            fputc('\t', stdout);
            if(Process->IoState == METRIC_OK) printf("%.2f", Process->DiskUsage); else fputs(MetricStateName(Process->IoState), stdout);
            fputc('\t', stdout);
            if(Process->AgeState == METRIC_OK) printf("%llu", (unsigned long long)Process->UpTime); else fputs(MetricStateName(Process->AgeState), stdout);
            printf("\t%s\n", Name);
            free(Name); free(Owner);
        }
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}

static void ClearRow(int Row)
{
    COORD Position = { 0, (SHORT)Row };
    DWORD Written;
    FillConsoleOutputCharacter(ConsoleHandle, _T(' '), (DWORD)Width, Position, &Written);
    FillConsoleOutputAttribute(ConsoleHandle, Config.FGColor, (DWORD)Width, Position, &Written);
    SetConCursorPos(0, (SHORT)Row);
}

static void DrawFrame(void)
{
    if(Width < 40 || Height < 10) {
        ClearRow(0);
        ConPrintf(_T("Resize to at least 40x10; q quits."));
        VisibleProcessCount = 0;
        DisplayedProcessID = 0; DisplayedCreationTime = 0;
        return;
    }
    ClearRow(0);
    SetColor(Config.FGColor | Config.MenuBarColor);
    ConPrintf(_T("Wtop %hs on %s"), WTOP_VERSION_STRING, ComputerName);
    ClearRow(1);
    DrawPercentageBar(_T("CPU"), CPUUsageKnown ? CPUUsage : -1, Config.CPUBarColor);
    ConPrintf(_T("  Tasks: %lu  Active: %lu"), ProcessCount, RunningProcessCount);
    ClearRow(2);
    DrawPercentageBar(_T("Mem"), SystemMemoryKnown ? UsedMemoryPerc : -1, Config.MemoryBarColor);
    if(SystemMemoryKnown) ConPrintf(_T("  %llu/%llu MiB"), UsedMemory, TotalMemory);
    ClearRow(3);
    DrawPercentageBar(_T("Cmt"), SystemMemoryKnown ? UsedPageMemoryPerc : -1, Config.PageMemoryBarColor);
    TCHAR Age[32];
    FormatTimeString(Age, _countof(Age), UpTime);
    ConPrintf(_T("  Up: %s"), Age);
    ClearRow(4);
    ConPrintf(_T("%s (%lu processors in current native system view)"), CPUName, CPUCoreCount);
    ClearRow(5);
    const process_list_column Columns[] = {
        { _T("ID"), 7 }, { _T("USER"), 9 }, { _T("PRI"), 3 },
        { _T("CPU%"), 5 }, { _T("MEM"), 11 }, { _T("THRD"), 4 },
        { _T("I/O"), 9 }, { _T("AGE"), 14 }, { _T("PROCESS"), 7 }
    };
    DrawProcessListHeader(Columns, _countof(Columns));
    ProcessWindowHeight = (DWORD)(Height - ProcessWindowPosY);
    VisibleProcessCount = ProcessWindowHeight - 2;
    ReadjustCursor();
    DisplayedProcessID = ProcessCount ? ProcessList[SelectedProcessIndex].ID : 0;
    DisplayedCreationTime = ProcessCount ? ProcessList[SelectedProcessIndex].CreationTime : 0;
    for(DWORD i = 0; i < VisibleProcessCount; ++i) {
        ClearRow(ProcessWindowPosY + (int)i);
        DWORD Index = ProcessIndex + i;
        if(Index < ProcessCount) WriteProcessInfo(&ProcessList[Index], Index == SelectedProcessIndex);
    }
    ClearRow(Height - 2);
    ConPrintf(_T("q Quit  / Search  : Commands  Space Tag  K Kill  I Reverse"));
    ClearRow(Height - 1);
    if(InInputMode) ConPrintf(_T("%s_"), CurrentInputStr);
    else if(ViMessageActive()) WriteVi();
}

int _tmain(int argc, TCHAR *argv[])
{
    int Result = 0;
    BOOL Initialized = FALSE, HandlerRegistered = FALSE, ScreenCreated = FALSE;
    BOOL InputModeChanged = FALSE;
    DWORD OriginalInputMode = 0;
    char **Utf8Argv = calloc((size_t)argc, sizeof(*Utf8Argv));
    if(!Utf8Argv) return 1;
    for(int i = 0; i < argc; ++i) {
        Utf8Argv[i] = ToUtf8(argv[i]);
        if(!Utf8Argv[i]) { Result = 1; goto Finish; }
    }
    wtop_options Options;
    const char *Error;
    if(!wtop_parse_options(argc, (const char *const *)Utf8Argv, &Options, &Error)) {
        fprintf(stderr, "wtop: %s; use --help\n", Error);
        Result = 2; goto Finish;
    }
    if(Options.action != WTOP_RUN) { Result = PrintInformation(&Options); goto Finish; }
    if(Options.mode == WTOP_GUI) {
        fputs("wtop: --gui is reserved; native GUI is not implemented. Use --tui or --snapshot.\n", stderr);
        Result = 2; goto Finish;
    }
    if(!ApplyOptions(&Options)) { fputs("wtop: invalid or excessive filter value\n", stderr); Result = 2; goto Finish; }
    if(!Options.no_config && !ReadConfigFile(Options.config_path)) {
        fputs("wtop: cannot read configuration or configuration is invalid\n", stderr);
        Result = 2; goto Finish;
    }
    if(Options.monochrome) {
        ULONGLONG Interval = Config.RedrawInterval;
        Config = MonochromeConfig;
        Config.RedrawInterval = Interval;
    }
    if(Options.interval_set) Config.RedrawInterval = Options.interval_ms;
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
    PollInitialSystemInfo();
    PollSystemInfo();
    if(!PollProcessList(InteractiveMode ? 50 : (DWORD)Config.RedrawInterval)) {
        Result = CollectorError ? 1 : 130;
        goto Cleanup;
    }
    if(!InteractiveMode) { Result = WriteSnapshot(Options.json); goto Cleanup; }
    if(InteractiveMode) {
        OldConsoleHandle = ConsoleHandle;
        ConsoleHandle = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
        if(ConsoleHandle == INVALID_HANDLE_VALUE) { Result = 1; goto Cleanup; }
        ScreenCreated = TRUE;
        if(!SetConsoleActiveScreenBuffer(ConsoleHandle)) { Result = 1; goto Cleanup; }
        SetConsoleMode(ConsoleHandle, ENABLE_PROCESSED_OUTPUT);
        InputModeChanged = SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),
            (OriginalInputMode | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) & ~ENABLE_QUICK_EDIT_MODE);
        PollConsoleInfo();
    }
    ProcessListThread = CreateThread(NULL, 0, PollProcessListThreadProc, NULL, 0, NULL);
    if(!ProcessListThread) { Result = 1; goto Cleanup; }
    ULONGLONG LastDraw = 0;
    BOOL Redraw = TRUE;
    while(!ShouldStop()) {
        EnterCriticalSection(&SyncLock);
        ProcessInput(&Redraw);
        if(PollConsoleInfo()) Redraw = TRUE;
        ULONGLONG Now = GetTickCount64();
        if(Redraw || RedrawAtCursor || Now - LastDraw >= Config.RedrawInterval) {
            PollSystemInfo();
            DrawFrame();
            LastDraw = Now;
            Redraw = RedrawAtCursor = FALSE;
        }
        LeaveCriticalSection(&SyncLock);
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
    ViDestroy();
    free(ViMessage); ViMessage = NULL;
    free(ProcessList); ProcessList = NULL;
    free(TaggedProcessList); TaggedProcessList = NULL;
    if(Initialized) DeleteCriticalSection(&SyncLock);
    if(CleanupDoneEvent) SetEvent(CleanupDoneEvent);
    if(HandlerRegistered) SetConsoleCtrlHandler(CtrlHandler, FALSE);
    /* The two callback rendezvous handles have process lifetime: a control
       callback may still be returning after unregistration. Exit closes them. */
    if(CollectorError) { fprintf(stderr, "wtop: collector failed (Win32 error %lu)\n", CollectorError); Result = 1; }
Finish:
    for(int i = 0; i < argc; ++i) free(Utf8Argv[i]);
    free(Utf8Argv);
    return Result;
}
