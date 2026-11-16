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
#include <errno.h>
#include "core.h"
#include "settings.h"
#include "ui.h"
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
static int RenderColumn;
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

static wtop_settings Settings;
static wtop_config_document ConfigDocument;
static TCHAR ConfigPath[32768];
static BOOL ConfigWritesDisabled;
static DWORD OriginalInputMode;

static wtop_tab *ActiveTab(void) { return &Settings.tabs[Settings.active_tab]; }

/* Configuration paths are independent of CWD unless explicitly supplied. The
   roaming folder lookup is optional and never creates a directory at startup. */
static BOOL ConfigExpectedExists;
static TCHAR ConfigUserDirectory[32768];

static BOOL ResolveExplicitPath(const char *Text, TCHAR *Path, DWORD Capacity)
{
    TCHAR *Converted = FromUtf8(Text);
    DWORD Length = Converted ? GetFullPathName(Converted, Capacity, Path, NULL) : 0;
    free(Converted);
    return Length && Length < Capacity;
}

static BOOL ResolveUserConfigPath(void)
{
    typedef HRESULT (WINAPI *folder_fn)(HWND, int, HANDLE, DWORD, LPTSTR);
    TCHAR Library[MAX_PATH + 32], Folder[MAX_PATH];
    UINT Length = GetSystemDirectory(Library, _countof(Library));
    if(!Length || Length >= MAX_PATH) return FALSE;
    _tcscat_s(Library, _countof(Library), _T("\\shell32.dll"));
    HMODULE Module = LoadLibrary(Library);
    if(!Module) return FALSE;
#ifdef UNICODE
    folder_fn GetFolder = (folder_fn)GetProcAddress(Module, "SHGetFolderPathW");
#else
    folder_fn GetFolder = (folder_fn)GetProcAddress(Module, "SHGetFolderPathA");
#endif
    HRESULT Status = GetFolder ? GetFolder(NULL, 0x001a /* CSIDL_APPDATA, no CREATE */, NULL, 0, Folder) : E_NOTIMPL;
    FreeLibrary(Module);
    if(FAILED(Status) || !Folder[0]) return FALSE;
    _stprintf_s(ConfigUserDirectory, _countof(ConfigUserDirectory), _T("%s\\Wtop"), Folder);
    _stprintf_s(ConfigPath, _countof(ConfigPath), _T("%s\\wtop.conf"), ConfigUserDirectory);
    return TRUE;
}

static BOOL ReadConfigFile(const char *ExplicitPath, BOOL SkipRead)
{
    FILE *File = NULL;
    TCHAR ReadPath[32768];
    ConfigWritesDisabled = SkipRead;
    ConfigPath[0] = ConfigUserDirectory[0] = 0;
    ConfigExpectedExists = FALSE;
    if(SkipRead) return TRUE;
    if(ExplicitPath) {
        if(!ResolveExplicitPath(ExplicitPath, ConfigPath, _countof(ConfigPath))) return FALSE;
    } else {
        DWORD Length = GetModuleFileName(NULL, ConfigPath, _countof(ConfigPath));
        if(!Length || Length >= _countof(ConfigPath)) return FALSE;
        TCHAR *Slash = _tcsrchr(ConfigPath, _T('\\'));
        if(!Slash) return FALSE;
        Slash[1] = 0;
        if(_tcslen(ConfigPath) + 11 >= _countof(ConfigPath)) return FALSE;
        _tcscat_s(ConfigPath, _countof(ConfigPath), _T("wtop.conf"));
    }
    _tcscpy_s(ReadPath, _countof(ReadPath), ConfigPath);
    errno_t OpenError = _tfopen_s(&File, ReadPath, _T("rb"));
    if(!OpenError) ConfigExpectedExists = TRUE;
    if(OpenError && !ExplicitPath && (OpenError == ENOENT || OpenError == ENOTDIR)) {
        TCHAR *Slash = _tcsrchr(ReadPath, _T('\\'));
        Slash[1] = 0;
        _tcscat_s(ReadPath, _countof(ReadPath), _T("ntop.conf"));
        OpenError = _tfopen_s(&File, ReadPath, _T("rb"));
        /* Import legacy configuration without changing the legacy file. */
        if(OpenError && (OpenError == ENOENT || OpenError == ENOTDIR)) {
            ConfigPath[0] = 0;
            if(!ResolveUserConfigPath()) return TRUE; /* TUI still works; saving reports unavailable. */
            OpenError = _tfopen_s(&File, ConfigPath, _T("rb"));
            if(!OpenError) ConfigExpectedExists = TRUE;
        }
    }
    if(OpenError) return !ExplicitPath && (OpenError == ENOENT || OpenError == ENOTDIR);
    unsigned long Line;
    const char *Error = NULL;
    BOOL Ok = wtop_settings_read(File, &Settings, &ConfigDocument, &Line, &Error);
    if(!Ok) fprintf(stderr, "wtop: invalid configuration at line %lu: %s\n", Line, Error ? Error : "read failed");
    fclose(File);
    return Ok;
}

/* Check the last loaded/saved bytes, not merely timestamp/length. This catches
   edits since load; it is not a cross-process compare-and-swap transaction. */
static BOOL ConfigTargetUnchanged(void)
{
    HANDLE File = CreateFile(ConfigPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(File == INVALID_HANDLE_VALUE) {
        DWORD Error = GetLastError();
        return !ConfigExpectedExists && (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND);
    }
    BOOL Ok = ConfigExpectedExists;
    size_t Offset = 0;
    BYTE Buffer[1024];
    DWORD Count;
    while(Ok) {
        if(!ReadFile(File, Buffer, sizeof(Buffer), &Count, NULL)) { Ok = FALSE; break; }
        if(!Count) { Ok = Offset == ConfigDocument.length; break; }
        if(Offset > ConfigDocument.length || Count > ConfigDocument.length - Offset ||
           memcmp(Buffer, ConfigDocument.original + Offset, Count)) { Ok = FALSE; break; }
        Offset += Count;
    }
    CloseHandle(File);
    return Ok;
}

/* Explicit writes use a unique sibling file, flush it, then atomically replace
   on the current NT backend. No fallback truncates the destination in-place. */
static BOOL WriteConfigFile(const TCHAR *Path, BOOL CheckForChanges, const char **Error)
{
    TCHAR Temporary[32768];
    HANDLE Handle = INVALID_HANDLE_VALUE;
    wtop_config_document WrittenDocument = {0};
    DWORD Failure = ERROR_SUCCESS;
    if(!wtop_settings_validate(&Settings, Error)) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    if(CheckForChanges && !ConfigTargetUnchanged()) { *Error = "configuration changed since load; reload before saving"; SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
    *Error = "cannot create configuration temporary file";
    if(_tcslen(Path) + 64 >= _countof(Temporary)) { *Error = "configuration path too long"; SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    for(unsigned Attempt = 0; Attempt < 16; ++Attempt) {
        _stprintf_s(Temporary, _countof(Temporary), _T("%s.tmp-%lu-%llu-%u"), Path, GetCurrentProcessId(), GetTickCount64(), Attempt);
        Handle = CreateFile(Temporary, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if(Handle != INVALID_HANDLE_VALUE) break;
        if(GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    }
    if(Handle == INVALID_HANDLE_VALUE) return FALSE;
    int Descriptor = _open_osfhandle((INT_PTR)Handle, _O_RDWR | _O_BINARY);
    if(Descriptor < 0) { CloseHandle(Handle); DeleteFile(Temporary); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    FILE *File = _fdopen(Descriptor, "w+b");
    if(!File) { _close(Descriptor); DeleteFile(Temporary); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    BOOL Ok = wtop_settings_write(File, &Settings, &ConfigDocument, Error);
    if(!Ok) Failure = ERROR_WRITE_FAULT;
    if(Ok && fflush(File)) { *Error = "cannot flush configuration stream"; Failure = ERROR_WRITE_FAULT; Ok = FALSE; }
    if(Ok && !FlushFileBuffers((HANDLE)_get_osfhandle(Descriptor))) {
        *Error = "cannot flush configuration temporary file"; Failure = GetLastError(); Ok = FALSE;
    }
    if(Ok && CheckForChanges) {
        /* Capture our exact output while the temporary handle excludes sharing.
           Do not reread the destination after replacement: that could adopt a
           concurrent editor's bytes as our own baseline without applying them. */
        unsigned long Line;
        wtop_settings WrittenSettings = Settings;
        if(fseek(File, 0, SEEK_SET) || !wtop_settings_read(File, &WrittenSettings, &WrittenDocument, &Line, Error)) {
            *Error = "cannot verify serialized configuration"; Failure = ERROR_READ_FAULT; Ok = FALSE;
        }
    }
    if(fclose(File) && Ok) { *Error = "cannot close configuration temporary file"; Failure = ERROR_WRITE_FAULT; Ok = FALSE; }
    if(Ok && CheckForChanges && !ConfigTargetUnchanged()) {
        *Error = "configuration changed while saving; destination preserved"; Failure = ERROR_SHARING_VIOLATION; Ok = FALSE;
    }
    DWORD Flags = MOVEFILE_WRITE_THROUGH;
    /* A new implicit destination must not overwrite a file created after our
       check. Explicit exports deliberately allow replacement. */
    if(!CheckForChanges || ConfigExpectedExists) Flags |= MOVEFILE_REPLACE_EXISTING;
    if(Ok && !MoveFileEx(Temporary, Path, Flags)) {
        *Error = "cannot replace configuration destination"; Failure = GetLastError(); Ok = FALSE;
    }
    if(Ok && CheckForChanges) {
        wtop_config_document_free(&ConfigDocument);
        ConfigDocument = WrittenDocument;
        memset(&WrittenDocument, 0, sizeof(WrittenDocument));
        ConfigExpectedExists = TRUE;
    }
    wtop_config_document_free(&WrittenDocument);
    if(!Ok) { DeleteFile(Temporary); SetLastError(Failure); }
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
	Coord.X = (SHORT)(X + ConsoleOriginX);
	Coord.Y = (SHORT)(Y + ConsoleOriginY);
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
    ConfigDirty = TRUE;
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
    BOOL Changed = Width != OldWidth || Height != OldHeight ||
        ConsoleOriginX != Csbi.srWindow.Left || ConsoleOriginY != Csbi.srWindow.Top;
    ConsoleOriginX = Csbi.srWindow.Left;
    ConsoleOriginY = Csbi.srWindow.Top;
    if(Changed) { DisableCursor(); OldWidth = Width; OldHeight = Height; RenderInvalidated = TRUE; }
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
        EnterCriticalSection(&SyncLock);
        DWORD Interval = (DWORD)Settings.interval_ms;
        LeaveCriticalSection(&SyncLock);
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
    RenderedColumnCount = wtop_ui_columns(Width, Tab->columns, Tab->column_count, RenderedColumns);
    for(int i = 0; i < RenderedColumnCount; ++i) {
        wtop_ui_column_span *Span = &RenderedColumns[i];
        SetConCursorPos((SHORT)Span->start, (SHORT)ProcessHeaderRow);
        SetColor((int)ProcessSortType == Span->id ? Settings.colors[WTOP_COLOR_BG_HIGHLIGHT] : Settings.colors[WTOP_COLOR_HEADER]);
        TCHAR *Name = FromUtf8(wtop_column_name(Span->id));
        if(Name) { ConPrintf(_T("%-*.*s"), Span->width, Span->width, Name); free(Name); }
    }
}

#define BAR_WIDTH 25

static int DrawPercentageBar(TCHAR *Name, double Percentage, WORD Color)
{
	int CharsWritten = 0;

	SetColor(Settings.colors[WTOP_COLOR_METER_LABEL]);
	CharsWritten += ConPrintf(_T("  %s"), Name);
	SetColor(Settings.colors[WTOP_COLOR_FG]);
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
	SetColor(Settings.colors[WTOP_COLOR_FG]);
	for(int i = 0; i < BAR_WIDTH - Bars; i++) {
		ConPutc(_T(' '));
	}
	CharsWritten += BAR_WIDTH - Bars;
	SetColor(Settings.colors[WTOP_COLOR_FG_HIGHLIGHT]);
	CharsWritten += Known ? ConPrintf(_T("%04.1f%%"), 100.0 * Percentage) : ConPrintf(_T("    ?"));
	SetColor(Settings.colors[WTOP_COLOR_FG]);
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
    WORD Color = (WORD)Settings.colors[WTOP_COLOR_FG];
    if(IsProcessTagged(Process)) Color = (WORD)Settings.colors[WTOP_COLOR_TAG];
    if(Highlighted) Color = (WORD)Settings.colors[FollowProcess ? WTOP_COLOR_FOLLOW : WTOP_COLOR_BG_HIGHLIGHT];
    SetColor(Color);
    int Row;
    CONSOLE_SCREEN_BUFFER_INFO Info;
    if(!GetConsoleScreenBufferInfo(ConsoleHandle, &Info)) return;
    Row = Info.dwCursorPosition.Y - ConsoleOriginY;
    COORD RowStart = { (SHORT)ConsoleOriginX, Info.dwCursorPosition.Y };
    DWORD Painted;
    FillConsoleOutputAttribute(ConsoleHandle, Color, (DWORD)Width, RowStart, &Painted);
    for(int i = 0; i < RenderedColumnCount; ++i) {
        const wtop_ui_column_span *Span = &RenderedColumns[i];
        TCHAR Text[512];
        char Metric[32];
        Text[0] = 0;
        switch(Span->id) {
        case WTOP_COLUMN_ID: _stprintf_s(Text, _countof(Text), _T("%lu"), Process->ID); break;
        case WTOP_COLUMN_USER: _tcscpy_s(Text, _countof(Text), Process->UserName); break;
        case WTOP_COLUMN_PRIORITY:
            if(Process->BasePriority <= 999) _stprintf_s(Text, _countof(Text), _T("%lu"), Process->BasePriority);
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_CPU:
            if(Process->CpuState == METRIC_OK && Process->PercentProcessorTime < 999.95) _stprintf_s(Text, _countof(Text), _T("%.1f"), Process->PercentProcessorTime);
            else if(Process->CpuState == METRIC_OK) _tcscpy_s(Text, _countof(Text), _T(">999"));
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_MEMORY:
            if(Process->MemoryState == METRIC_OK) {
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
            if(Process->IoState == METRIC_OK) {
                wtop_ui_format_bytes(Process->DiskUsage, TRUE, Metric, sizeof(Metric));
                _stprintf_s(Text, _countof(Text), _T("%hs"), Metric);
            }
            else _tcscpy_s(Text, _countof(Text), _T("?"));
            break;
        case WTOP_COLUMN_AGE:
            if(Process->AgeState == METRIC_OK) {
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
        ConPrintf(_T("%-*.*s"), Space, Space, Text);
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
    "  --no-config           Ignore config files and disable implicit TUI saves\n"
    "  --write-config PATH   Explicitly export settings, then exit (no sampling)\n"
    "  --keymap NAME         htop (default), vi/vim, or emacs\n"
    "  --theme NAME          htop (default), ntop, or monochrome\n"
    "  --mouse MODE          auto (default), on, or off; --no-mouse disables\n"
    "  --[no-]status-bar     Show/hide last-row function bar (default shown)\n"
    "  -C                    Monochrome TUI\n"
    "  -p PID,PID            Filter process IDs (maximum 1024)\n"
    "  -n PART,PART          Case-sensitive name substrings (maximum 10)\n"
    "  -u USER               Exact account-name filter, case-insensitive\n"
    "  -s COLUMN             ID USER PRI CPU% MEM THRD I/O AGE PROCESS\n"
    "                        DISK and TIME remain aliases for I/O and AGE\n"
    "TUI: F1 Help, F2 Setup, F3 Search, F4 Filter, F5 Tree, F6 Sort,\n"
    "     F7/F8 Windows priority class, F9 confirmed termination, F10 Quit.\n"
    "     Arrows/PgUp/PgDn/Home/End navigate; Tab/Shift+Tab switches views.\n"
    "     htop k opens Kill; vi j/k navigate. B toggles the function bar.\n"
    "     F2 s or :save saves explicitly; quitting never writes settings.\n"
    "     Commands: :q :quit :tree :sort :search :kill :exec :save.\n"
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
        else printf("Wtop %s: build capabilities (%s; no runtime probe)\nTUI with htop/vi/Emacs keys, optional mouse, setup and explicit config save.\nText/JSON snapshot, process tree/search/sort and NTop commands.\nMetric access varies by process rights; unavailable fields carry status/null.\nGUI, installers, GPU telemetry and historical OS ports are not implemented.\n", WTOP_VERSION_STRING, WTOP_BUILD_PROFILE);
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}

int GetProcessSortTypeFromName(const TCHAR *Name, process_sort_type *Dest)
{
    char *Utf8 = ToUtf8(Name);
    int Column;
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
    EnterCriticalSection(&SyncLock);
    ActiveTab()->tree = NewProcessSortType == SORT_BY_TREE;
    if(NewProcessSortType != SORT_BY_TREE) ActiveTab()->sort_column = (int)NewProcessSortType;
    ConfigDirty = TRUE;
    ApplyActiveTab();
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
    if(StatusRow < 0) return;
	SetConCursorPos(0, (SHORT)StatusRow);

	if(ViMessageActive()) {
		switch(CurrentViMessageType) {
		case VI_NOTICE:
			// Default?
			break;
		case VI_ERROR:
			SetColor(Settings.colors[WTOP_COLOR_ERROR]);
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

void ClearViMessage(void)
{
    if(ViMessage) ViMessage[0] = 0;
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
    else if(Pid == GetCurrentProcessId()) { RequestShutdown(); Result = 1; }
    else if(!TerminateProcess(Handle, 9)) SetViMessage(VI_ERROR, _T("Cannot terminate PID %lu: error %lu"), Pid, GetLastError());
    else Result = 1;
    CloseHandle(Handle);
    return Result;
}

/* Windows priority classes are not POSIX nice values. Never enter REALTIME by
   a repeated shortcut; each handle is bound to the observed process lifetime. */
static void ChangePriority(const process_identity *Identity, int Direction)
{
    static const DWORD Classes[] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS,
        NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS };
    if(!Identity->CreationTime) { SetViMessage(VI_ERROR, _T("Priority unavailable: unknown process identity")); return; }
    HANDLE Handle = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Identity->ID);
    if(!Handle) { SetViMessage(VI_ERROR, _T("Cannot change PID %lu priority: error %lu"), Identity->ID, GetLastError()); return; }
    FILETIME Created, Exited, Kernel, User;
    if(!GetProcessTimes(Handle, &Created, &Exited, &Kernel, &User) || FileTimeValue(&Created) != Identity->CreationTime) {
        SetViMessage(VI_ERROR, _T("Priority refused: PID %lu identity changed"), Identity->ID);
        CloseHandle(Handle); return;
    }
    DWORD Current = GetPriorityClass(Handle);
    int Position = -1;
    for(int i = 0; i < (int)_countof(Classes); ++i) if(Classes[i] == Current) Position = i;
    int Next = Position + Direction;
    if(Position < 0 || Next < 0 || Next >= (int)_countof(Classes))
        SetViMessage(VI_ERROR, _T("PID %lu: no further safe priority class in this direction"), Identity->ID);
    else if(!SetPriorityClass(Handle, Classes[Next]))
        SetViMessage(VI_ERROR, _T("Cannot change PID %lu priority: error %lu"), Identity->ID, GetLastError());
    else SetViMessage(VI_NOTICE, _T("PID %lu Windows priority class changed"), Identity->ID);
    CloseHandle(Handle);
}

int KillObservedProcess(DWORD Pid)
{
    for(DWORD i = 0; i < ProcessCount; ++i)
        if(ProcessList[i].ID == Pid) return KillIdentity(Pid, ProcessList[i].CreationTime);
    SetViMessage(VI_ERROR, _T("PID %lu is not in the observed process set"), Pid);
    return 0;
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

typedef enum overlay_type { OVERLAY_NONE, OVERLAY_HELP, OVERLAY_SETUP, OVERLAY_SORT, OVERLAY_KILL } overlay_type;
static overlay_type Overlay;
static int SetupPage, SetupIndex, OverlayTop, SortChoice;
static int EmacsPrefix;
static char EditingOriginalFilter[WTOP_FILTER_BYTES];
static BOOL EditingFilter, EditingWasDirty;
static process_identity *PendingKills;
static DWORD PendingKillCount;
static DWORD LastMouseButtons;
static int MouseWheelRemainder;

static void ConfigureMouse(void)
{
    DWORD Mode = (OriginalInputMode | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT) & ~ENABLE_QUICK_EDIT_MODE;
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
    if(ConfigWritesDisabled) { SetViMessage(VI_ERROR, _T("Saving disabled by --no-config; use --write-config PATH to export defaults")); return 0; }
    if(!ConfigPath[0]) { SetViMessage(VI_ERROR, _T("Configuration folder unavailable; restart with --config PATH")); return 0; }
    if(ConfigUserDirectory[0] && !CreateDirectory(ConfigUserDirectory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        SetViMessage(VI_ERROR, _T("Cannot create user configuration directory (error %lu)"), GetLastError()); return 0;
    }
    const char *Error = NULL;
    if(!WriteConfigFile(ConfigPath, TRUE, &Error)) { SetViMessage(VI_ERROR, _T("Settings not saved: %hs (error %lu)"), Error, GetLastError()); return 0; }
    ConfigDirty = FALSE;
    SetViMessage(VI_NOTICE, _T("Saved configuration: %s"), ConfigPath);
    return 1;
}

static void CloseOverlay(void)
{
    free(PendingKills); PendingKills = NULL; PendingKillCount = 0;
    Overlay = OVERLAY_NONE; OverlayTop = 0; RenderInvalidated = TRUE;
}

static void BeginKill(void)
{
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
        if(Settings.tabs[Index].visible) { Settings.active_tab = Index; ConfigDirty = TRUE; ApplyActiveTab(); return; }
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
    ViDisableInput();
    ClearViMessage();
}

static void DispatchAction(wtop_ui_action Action, BOOL *Redraw)
{
    if(Action == WTOP_UI_NONE) return;
    CancelEditor();
    *Redraw = TRUE;
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
    case WTOP_UI_TREE: ActiveTab()->tree = !ActiveTab()->tree; ConfigDirty = TRUE; ApplyActiveTab(); break;
    case WTOP_UI_SORT: CloseOverlay(); Overlay = OVERLAY_SORT; SortChoice = ActiveTab()->sort_column; break;
    case WTOP_UI_PRIORITY_UP:
    case WTOP_UI_PRIORITY_DOWN:
        if(TaggedProcessListCount) {
            for(DWORD i = 0; i < TaggedProcessListCount; ++i) ChangePriority(&TaggedProcessList[i], Action == WTOP_UI_PRIORITY_UP ? 1 : -1);
        } else if(SelectionMatchesDisplay()) {
            process_identity Identity = { DisplayedProcessID, DisplayedCreationTime };
            ChangePriority(&Identity, Action == WTOP_UI_PRIORITY_UP ? 1 : -1);
        }
        break;
    case WTOP_UI_KILL: BeginKill(); break;
    case WTOP_UI_QUIT: RequestShutdown(); break;
    case WTOP_UI_TAG:
        if(SelectionMatchesDisplay()) { ToggleTaggedProcess(&ProcessList[SelectedProcessIndex]); DoScroll(SCROLL_DOWN, Redraw); }
        break;
    case WTOP_UI_UNTAG: TaggedProcessListCount = 0; break;
    case WTOP_UI_REVERSE: ActiveTab()->descending = !ActiveTab()->descending; ConfigDirty = TRUE; ApplyActiveTab(); break;
    case WTOP_UI_FOLLOW:
        if(SelectionMatchesDisplay()) {
            FollowProcess = !FollowProcess; FollowProcessID = DisplayedProcessID; FollowCreationTime = DisplayedCreationTime;
        }
        break;
    case WTOP_UI_SEARCH_NEXT: SearchNext(); break;
    case WTOP_UI_SEARCH_PREVIOUS: SearchPrevious(); break;
    case WTOP_UI_NEXT_TAB: ChangeTab(1); break;
    case WTOP_UI_PREVIOUS_TAB: ChangeTab(-1); break;
    case WTOP_UI_BAR: Settings.show_function_bar = !Settings.show_function_bar; ConfigDirty = TRUE; break;
    case WTOP_UI_CANCEL: CloseOverlay(); ViDisableInput(); ClearViMessage(); EmacsPrefix = FALSE; break;
    case WTOP_UI_SORT_CPU: ChangeProcessSortType(SORT_BY_PROCESSOR_TIME); break;
    case WTOP_UI_SORT_MEMORY: ChangeProcessSortType(SORT_BY_USED_MEMORY); break;
    case WTOP_UI_SORT_PID: ChangeProcessSortType(SORT_BY_ID); break;
    case WTOP_UI_SORT_LEFT: ChangeProcessSortType((process_sort_type)((ActiveTab()->sort_column + WTOP_MAX_COLUMNS - 1) % WTOP_MAX_COLUMNS)); break;
    case WTOP_UI_SORT_RIGHT: ChangeProcessSortType((process_sort_type)((ActiveTab()->sort_column + 1) % WTOP_MAX_COLUMNS)); break;
    case WTOP_UI_EMACS_PREFIX: EmacsPrefix = TRUE; SetViMessage(VI_NOTICE, _T("C-x: C-c quits, C-s saves; C-g cancels")); break;
    case WTOP_UI_SAVE: SavePresentationSettings(); break;
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
    ConfigDirty = TRUE;
}

static int SetupCount(void)
{
    switch(SetupPage) {
    case 0: return 6;
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
        if(Activate) {
            int Visible = 0;
            for(int i = 0; i < Settings.tab_count; ++i) Visible += Settings.tabs[i].visible;
            if(Settings.tabs[SetupIndex].visible && Visible == 1) { SetViMessage(VI_ERROR, _T("At least one tab must remain visible")); return; }
            Settings.tabs[SetupIndex].visible = !Settings.tabs[SetupIndex].visible;
            if(!Settings.tabs[Settings.active_tab].visible) ChangeTab(1);
        } else {
            int Next = SetupIndex + Direction;
            if(Next >= 0 && Next < Settings.tab_count) {
                wtop_tab Tab = Settings.tabs[SetupIndex];
                Settings.tabs[SetupIndex] = Settings.tabs[Next]; Settings.tabs[Next] = Tab;
                if(Settings.active_tab == SetupIndex) Settings.active_tab = Next;
                else if(Settings.active_tab == Next) Settings.active_tab = SetupIndex;
                SetupIndex = Next;
            }
        }
    } else Settings.colors[SetupIndex] = (Settings.colors[SetupIndex] + 256 + Direction) % 256;
    ConfigDirty = TRUE;
    RenderInvalidated = TRUE;
}

static void HandleOverlayKey(int Key, unsigned Modifiers, BOOL *Redraw)
{
    *Redraw = TRUE;
    if(Key == 27 || (Settings.keymap == WTOP_KEYMAP_EMACS && (Modifiers & WTOP_MOD_CTRL) && (Key == 'g' || Key == 'G' || Key == 7))) { CloseOverlay(); return; }
    wtop_ui_action Navigation = wtop_ui_key_action(Settings.keymap, Key, Modifiers);
    if(Navigation == WTOP_UI_UP) Key = WTOP_KEY_UP;
    if(Navigation == WTOP_UI_DOWN) Key = WTOP_KEY_DOWN;
    if(Navigation == WTOP_UI_PAGE_UP) Key = WTOP_KEY_PAGE_UP;
    if(Navigation == WTOP_UI_PAGE_DOWN) Key = WTOP_KEY_PAGE_DOWN;
    if(Navigation == WTOP_UI_TOP) Key = WTOP_KEY_HOME;
    if(Navigation == WTOP_UI_BOTTOM) Key = WTOP_KEY_END;
    if(Overlay == OVERLAY_HELP) {
        if(Key == WTOP_KEY_UP) OverlayTop = max(0, OverlayTop - 1);
        if(Key == WTOP_KEY_DOWN) OverlayTop = min(18, OverlayTop + 1);
        if(Key == WTOP_KEY_PAGE_UP) OverlayTop = max(0, OverlayTop - max(1, Height - 5));
        if(Key == WTOP_KEY_PAGE_DOWN) OverlayTop = min(18, OverlayTop + max(1, Height - 5));
        return;
    }
    if(Overlay == OVERLAY_KILL) {
        if(Key == 'n' || Key == 'N') { CloseOverlay(); return; }
        if(Key == 'y' || Key == 'Y') {
            DWORD Success = 0;
            for(DWORD i = 0; i < PendingKillCount; ++i) Success += KillIdentity(PendingKills[i].ID, PendingKills[i].CreationTime);
            SetViMessage(Success == PendingKillCount ? VI_NOTICE : VI_ERROR,
                _T("Termination: %lu succeeded, %lu refused/failed"), Success, PendingKillCount - Success);
            TaggedProcessListCount = 0;
            CloseOverlay();
        }
        return;
    }
    if(Overlay == OVERLAY_SORT) {
        if(Key == WTOP_KEY_UP) SortChoice = max(0, SortChoice - 1);
        if(Key == WTOP_KEY_DOWN) SortChoice = min(WTOP_MAX_COLUMNS - 1, SortChoice + 1);
        if(Key == '\r' || Key == ' ') { ChangeProcessSortType((process_sort_type)SortChoice); CloseOverlay(); }
        return;
    }
    if(Key == WTOP_KEY_LEFT) { SetupPage = (SetupPage + 4) % 5; SetupIndex = OverlayTop = 0; }
    else if(Key == WTOP_KEY_RIGHT) { SetupPage = (SetupPage + 1) % 5; SetupIndex = OverlayTop = 0; }
    else if(Key == WTOP_KEY_UP || (Settings.keymap == WTOP_KEYMAP_VI && Key == 'k')) SetupIndex = max(0, SetupIndex - 1);
    else if(Key == WTOP_KEY_DOWN || (Settings.keymap == WTOP_KEYMAP_VI && Key == 'j')) SetupIndex = min(SetupCount() - 1, SetupIndex + 1);
    else if(Key == WTOP_KEY_PAGE_UP) SetupIndex = max(0, SetupIndex - max(1, Height - 5));
    else if(Key == WTOP_KEY_PAGE_DOWN) SetupIndex = min(SetupCount() - 1, SetupIndex + max(1, Height - 5));
    else if(Key == WTOP_KEY_HOME) SetupIndex = 0;
    else if(Key == WTOP_KEY_END) SetupIndex = SetupCount() - 1;
    else if(Key == 's' || Key == 'S') SavePresentationSettings();
    else if(Key == '+' || Key == '=') SetupAdjust(1, FALSE);
    else if(Key == '-') SetupAdjust(-1, FALSE);
    else if(Key == ' ' || Key == '\r') SetupAdjust(1, TRUE);
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
    if(Y == FunctionBarRow) { DispatchAction(wtop_ui_function_action(wtop_ui_bar_hit(Width, X)), Redraw); return; }
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
                Settings.active_tab = i; ConfigDirty = TRUE; ApplyActiveTab(); *Redraw = TRUE; return;
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
    for(DWORD i = 0; i < Count; ++i) {
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
            DispatchAction(wtop_ui_function_action(Key - WTOP_KEY_F1), Redraw); continue;
        }
        if(InInputMode) {
            BOOL Cancel = Key == 27 || ((Modifiers & WTOP_MOD_CTRL) && (Key == 7 || Key == 'g' || Key == 'G'));
            if(ViHandleInputKey(Event)) {
                if(EditingFilter && Cancel) {
                    strcpy_s(ActiveTab()->filter, sizeof(ActiveTab()->filter), EditingOriginalFilter);
                    ConfigDirty = EditingWasDirty; ReadjustCursor();
                } else if(InInputMode && CurrentInputStr[0] == _T('\\')) SetPresentationFilter(CurrentInputStr + 1);
                if(!InInputMode) EditingFilter = FALSE;
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
        wtop_ui_action Action = wtop_ui_key_action(Settings.keymap, Key, Modifiers);
        unsigned Repeats = (Action == WTOP_UI_UP || Action == WTOP_UI_DOWN || Action == WTOP_UI_PAGE_UP || Action == WTOP_UI_PAGE_DOWN) ? min(Event->wRepeatCount, 32) : 1;
        for(unsigned Repeat = 0; Repeat < Repeats; ++Repeat) DispatchAction(Action, Redraw);
    }
}

static BOOL ApplyOptions(const wtop_options *Options)
{
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
    COORD Position = { (SHORT)ConsoleOriginX, (SHORT)(Row + ConsoleOriginY) };
    DWORD Written;
    FillConsoleOutputCharacter(ConsoleHandle, _T(' '), (DWORD)Width, Position, &Written);
    FillConsoleOutputAttribute(ConsoleHandle, (WORD)Settings.colors[WTOP_COLOR_BG], (DWORD)Width, Position, &Written);
    SetConCursorPos(0, (SHORT)Row);
    SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
}

static void DrawFunctionBar(void)
{
    if(FunctionBarRow < 0) return;
    ClearRow(FunctionBarRow);
    for(int i = 0; i < 10; ++i) {
        int Start, Length;
        wtop_ui_bar_span(Width, i, &Start, &Length);
        if(!Length) continue;
        SetConCursorPos((SHORT)Start, (SHORT)FunctionBarRow);
        SetColor((WORD)Settings.colors[WTOP_COLOR_FUNCTION_KEY]);
        TCHAR Key[8];
        _stprintf_s(Key, _countof(Key), _T("F%d"), i + 1);
        int KeyLength = min(Length, (int)_tcslen(Key));
        ConPrintf(_T("%.*s"), KeyLength, Key);
        SetColor((WORD)Settings.colors[WTOP_COLOR_MENU_BAR]);
        int Remaining = Length - KeyLength;
        if(Remaining) {
            TCHAR *Label = FromUtf8(wtop_ui_function_label(i, Length < 7));
            if(Label) { ConPrintf(_T("%-*.*s"), Remaining, Remaining, Label); free(Label); }
        }
    }
}

static const TCHAR *HelpLines[] = {
    _T("F1 Help   F2 Setup   F3 Search   F4 Filter   F5 Tree"),
    _T("F6 Sort   F7 Prio+   F8 Prio-   F9 Kill    F10 Quit"),
    _T("Arrows/PgUp/PgDn/Home/End navigate. Tab/Shift+Tab changes views."),
    _T("Space tags a lifetime-identified process; U clears tags; F follows."),
    _T("I reverses sort; P sorts CPU; M sorts memory; B hides/shows the bar."),
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

static void DrawOverlay(void)
{
    for(int Row = 0; Row < Height; ++Row) ClearRow(Row);
    SetConCursorPos(0, 0);
    SetColor((WORD)Settings.colors[WTOP_COLOR_HEADER]);
    const TCHAR *Title = Overlay == OVERLAY_HELP ? _T("Help") : Overlay == OVERLAY_SETUP ? _T("Setup") : Overlay == OVERLAY_SORT ? _T("Sort") : _T("Terminate");
    ConPrintf(_T("%s - Wtop [%hs]%s"), Title, wtop_keymap_name(Settings.keymap), ConfigDirty ? _T(" * unsaved") : _T(""));
    SetConCursorPos(0, 1); SetColor((WORD)Settings.colors[WTOP_COLOR_FG]);
    if(Overlay == OVERLAY_KILL) {
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
            ConPrintf(_T("%-*.*s"), Width, Width, Text);
        }
    }
    StatusRow = ViMessageActive() ? Height - (Settings.show_function_bar ? 2 : 1) : -1;
    if(StatusRow >= 0) { ClearRow(StatusRow); WriteVi(); }
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
        DrawPercentageBar(_T("Cmt"), SystemMemoryKnown ? UsedPageMemoryPerc : -1, (WORD)Settings.colors[WTOP_COLOR_COMMIT]);
        TCHAR Age[32]; FormatTimeString(Age, _countof(Age), UpTime); ConPrintf(_T("  Up: %s"), Age);
        break;
    }
    case WTOP_ROW_SUMMARY:
        ConPrintf(_T("%s (%lu CPUs in native view)"), CPUName, CPUCoreCount);
        break;
    }
}

static void DrawFrame(void)
{
    FunctionBarRow = Settings.show_function_bar ? Height - 1 : -1;
    if(Width < 40 || Height < 10) {
        ClearRow(0); ConPrintf(_T("Resize to at least 40x10; F10 quits."));
        VisibleProcessCount = RenderedRowCount = 0;
        DisplayedProcessID = 0; DisplayedCreationTime = 0;
        RenderInvalidated = TRUE; return;
    }
    if(Overlay != OVERLAY_NONE) { DrawOverlay(); return; }
    for(int Row = 0; Row < Height; ++Row) ClearRow(Row);
    ReadjustCursor();
    int Row = 0;
    int HeaderLimit = min(Settings.header_count, Height - 5);
    for(int i = 0; i < HeaderLimit; ++i) DrawHeaderRow(Row++, Settings.header_rows[i]);
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
    StatusRow = (InInputMode || ViMessageActive() || EmacsPrefix) ? Height - (Settings.show_function_bar ? 2 : 1) : -1;
    int EndRow = StatusRow >= 0 ? StatusRow : Settings.show_function_bar ? Height - 1 : Height;
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
    if(StatusRow >= 0) {
        ClearRow(StatusRow);
        if(InInputMode) {
            unsigned long Cursor = ViInputCursor();
            unsigned long Start = Cursor >= (unsigned long)Width ? Cursor - (unsigned long)Width + 1 : 0;
#ifdef UNICODE
            if(Start && CurrentInputStr[Start] >= 0xdc00 && CurrentInputStr[Start] <= 0xdfff) ++Start;
#endif
            SetColor((WORD)Settings.colors[WTOP_COLOR_FG]); ConPrintf(_T("%s"), CurrentInputStr + Start);
            /* Color the already-rendered cursor cell(s); do not rewrite half
               of a UTF-16 surrogate pair as a separate console character. */
            COORD Caret = { (SHORT)(ConsoleOriginX + Cursor - Start), (SHORT)(ConsoleOriginY + StatusRow) };
            DWORD Cells = 1, Painted;
#ifdef UNICODE
            if(CurrentInputStr[Cursor] >= 0xd800 && CurrentInputStr[Cursor] <= 0xdbff &&
               CurrentInputStr[Cursor + 1] >= 0xdc00 && CurrentInputStr[Cursor + 1] <= 0xdfff &&
               Cursor - Start + 1 < (unsigned long)Width) Cells = 2;
#endif
            FillConsoleOutputAttribute(ConsoleHandle, (WORD)Settings.colors[WTOP_COLOR_BG_HIGHLIGHT], Cells, Caret, &Painted);
        }
        else WriteVi();
    }
    DrawFunctionBar();
    RenderInvalidated = FALSE;
}

int _tmain(int argc, TCHAR *argv[])
{
    int Result = 0;
    BOOL Initialized = FALSE, HandlerRegistered = FALSE, ScreenCreated = FALSE;
    BOOL InputModeChanged = FALSE;
    wtop_settings_defaults(&Settings);
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
    if(Options.action != WTOP_RUN && Options.action != WTOP_WRITE_CONFIG) { Result = PrintInformation(&Options); goto Finish; }
    if(Options.mode == WTOP_GUI) {
        fputs("wtop: --gui is reserved; native GUI is not implemented. Use --tui or --snapshot.\n", stderr);
        Result = 2; goto Finish;
    }
    if(!ApplyOptions(&Options)) { fputs("wtop: invalid or excessive filter value\n", stderr); Result = 2; goto Finish; }
    if(!ReadConfigFile(Options.config_path, Options.no_config)) {
        fputs("wtop: cannot read configuration or configuration is invalid\n", stderr);
        Result = 2; goto Finish;
    }
    if(!wtop_settings_apply_options(&Settings, &Options, &Error)) {
        fprintf(stderr, "wtop: %s\n", Error); Result = 2; goto Finish;
    }
    ProcessSortType = ActiveTab()->tree ? SORT_BY_TREE : (process_sort_type)ActiveTab()->sort_column;
    SortOrder = ActiveTab()->descending ? DESCENDING : ASCENDING;
    if(Options.action == WTOP_WRITE_CONFIG) {
        TCHAR Destination[32768];
        if(!ResolveExplicitPath(Options.write_config_path, Destination, _countof(Destination))) {
            fputs("wtop: cannot resolve configuration destination\n", stderr); Result = 1; goto Finish;
        }
        if(!WriteConfigFile(Destination, FALSE, &Error)) {
            fprintf(stderr, "wtop: %s (Win32 error %lu)\n", Error, GetLastError()); Result = 1; goto Finish;
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
    PollInitialSystemInfo();
    PollSystemInfo();
    if(!PollProcessList(InteractiveMode ? 50 : (DWORD)Settings.interval_ms)) {
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
        InputModeChanged = TRUE; /* Restore the exact caller mode even after a partial setup failure. */
        ConfigureMouse();
        PollConsoleInfo();
    }
    ProcessListThread = CreateThread(NULL, 0, PollProcessListThreadProc, NULL, 0, NULL);
    if(!ProcessListThread) { Result = 1; goto Cleanup; }
    ULONGLONG LastDraw = 0;
    BOOL Redraw = TRUE;
    while(!ShouldStop()) {
        EnterCriticalSection(&SyncLock);
        if(PollConsoleInfo()) Redraw = TRUE;
        ProcessInput(&Redraw);
        ULONGLONG Now = GetTickCount64();
        if(Redraw || RedrawAtCursor || Now - LastDraw >= Settings.interval_ms) {
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
    free(ViewIndices); ViewIndices = NULL;
    free(RenderedRows); RenderedRows = NULL;
    free(PendingKills); PendingKills = NULL;
    if(Initialized) DeleteCriticalSection(&SyncLock);
    if(CleanupDoneEvent) SetEvent(CleanupDoneEvent);
    if(HandlerRegistered) SetConsoleCtrlHandler(CtrlHandler, FALSE);
    /* The two callback rendezvous handles have process lifetime: a control
       callback may still be returning after unregistration. Exit closes them. */
    if(CollectorError) { fprintf(stderr, "wtop: collector failed (Win32 error %lu)\n", CollectorError); Result = 1; }
Finish:
    wtop_config_document_free(&ConfigDocument);
    for(int i = 0; i < argc; ++i) free(Utf8Argv[i]);
    free(Utf8Argv);
    return Result;
}
