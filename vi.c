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

#include "vi.h"
#include "ntop.h"
#include "util.h"
#include <conio.h>
#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#include <errno.h>

TCHAR *CurrentInputStr;
int InInputMode = 0;
static DWORD InputIndex;

typedef int (*cmd_fn)(DWORD Argc, TCHAR **Argv);

typedef struct cmd {
	TCHAR *Name;
	cmd_fn CmdFunc;
} cmd;

#define COMMAND_FUNC(Name) static int Name##_func(DWORD Argc, TCHAR **Argv)
#define COMMAND_ALIAS(Name, AliasName) static int Name##_func(DWORD Argc, TCHAR **Argv) { return AliasName##_func(Argc, Argv); }
#define COMMAND(Name) { _T(#Name), Name##_func }

static TCHAR *History[256];
static DWORD HistoryCount;
static DWORD HistoryIndex;

static void PushToHistory(TCHAR *Str)
{
	if(HistoryCount == _countof(History)) {
        free(History[0]);
        memmove(History, History + 1, (_countof(History) - 1) * sizeof(*History));
        --HistoryCount;
    }
    int Length = (int)_tcsclen(Str);
	History[HistoryCount] = malloc(sizeof **History * (Length + 1));
    if(!History[HistoryCount]) { SetViMessage(VI_ERROR, _T("History entry omitted: out of memory")); HistoryIndex = HistoryCount; return; }
	_tcscpy_s(History[HistoryCount], Length+1, Str);
	HistoryCount++;
	HistoryIndex = HistoryCount;
}

static void HistoryPrevious(TCHAR **Str)
{
	HistoryIndex--;
	_tcscpy_s(*Str, DEFAULT_STR_SIZE, History[HistoryIndex]);
	InputIndex = (DWORD)_tcsclen(*Str);
}

static void HistoryNext(TCHAR **Str)
{
	HistoryIndex++;
	_tcscpy_s(*Str, DEFAULT_STR_SIZE, History[HistoryIndex]);
	InputIndex = (DWORD)_tcsclen(*Str);
}

COMMAND_FUNC(kill)
{
    if(!Argc) { SetViMessage(VI_ERROR, _T("Usage: kill PID(s)")); return 1; }
    int Result = 0;
    for(DWORD i = 0; i < Argc; ++i) {
        TCHAR *End;
        errno = 0;
        unsigned long Pid = _tcstoul(Argv[i], &End, 10);
        if(!Argv[i][0] || Argv[i][0] == _T('-') || Argv[i][0] == _T('+') || !Pid || *End || errno == ERANGE) {
            SetViMessage(VI_ERROR, _T("Not a valid PID: %s"), Argv[i]); Result = 1; continue;
        }
        if(!KillObservedProcess((DWORD)Pid)) Result = 1;
    }
    return Result;
}

COMMAND_FUNC(tree)
{
	UNREFERENCED_PARAMETER(Argv);

	if(Argc != 0) {
		SetViMessage(VI_ERROR, _T("Error: trailing characters"));
		return 1;
	}

	ChangeProcessSortType(SORT_BY_TREE);

	return 0;
}

COMMAND_FUNC(exec)
{
	if(Argc == 0) {
		SetViMessage(VI_ERROR, _T("Usage: exec COMMAND"));
		return 1;
	}

	TCHAR CommandLine[4096] = { 0 };
    /* TryExec passes the original native command line as one argument. */
    if(Argc != 1 || _tcslen(Argv[0]) >= _countof(CommandLine)) {
        SetViMessage(VI_ERROR, _T("Invalid or excessive command line"));
        return 1;
    }
    _tcscpy_s(CommandLine, _countof(CommandLine), Argv[0]);

	STARTUPINFO StartupInfo;
	PROCESS_INFORMATION ProcInfo;
	ZeroMemory(&StartupInfo, sizeof(StartupInfo));
	ZeroMemory(&ProcInfo, sizeof(ProcInfo));
	StartupInfo.cb = sizeof(StartupInfo);

	BOOL Ret = CreateProcess(0, CommandLine, 0, 0, FALSE, 0, 0, 0, &StartupInfo, &ProcInfo);

	if(!Ret) {
		SetViMessage(VI_ERROR, _T("Failed to create process: 0x%08x"), GetLastError());
		return 1;
	}

	CloseHandle(ProcInfo.hThread);
    CloseHandle(ProcInfo.hProcess);
    return 0;
}

COMMAND_FUNC(q)
{
	UNREFERENCED_PARAMETER(Argc);
	UNREFERENCED_PARAMETER(Argv);

	RequestShutdown();
    return 0;
}
COMMAND_ALIAS(quit, q);

COMMAND_FUNC(sort)
{
	if(Argc != 1) {
		SetViMessage(VI_ERROR, _T("Usage: sort COLUMN"));
		return 1;
	}

	process_sort_type NewSortType;
	if(!GetProcessSortTypeFromName(Argv[0], &NewSortType)) {
		SetViMessage(VI_ERROR, _T("Unknown column: %s"), Argv[0]);
		return 1;
	}

	ChangeProcessSortType(NewSortType);

	return 0;
}

COMMAND_FUNC(search)
{
	if(Argc != 1) {
		SetViMessage(VI_ERROR, _T("Usage: search pattern"));
		return 1;
	}

	if(Argv[0][0] != _T('\0')) {
		StartSearch(Argv[0]);
	}

	return 0;
}

static cmd Commands[] = {
	COMMAND(exec),
	COMMAND(kill),
	COMMAND(q),
	COMMAND(quit),
	COMMAND(sort),
	COMMAND(tree),
	COMMAND(search),
};

typedef struct cmd_parse_result {
	TCHAR *Name;
	DWORD Argc;
	TCHAR **Args;
} cmd_parse_result;

static BOOL PushArg(cmd_parse_result *ParseResult)
{
    TCHAR **Args = realloc(ParseResult->Args, (size_t)(ParseResult->Argc + 1) * sizeof(*Args));
    if(!Args) return FALSE;
    ParseResult->Args = Args;
    Args[ParseResult->Argc] = calloc(DEFAULT_STR_SIZE, sizeof(TCHAR));
    if(!Args[ParseResult->Argc]) return FALSE;
    ++ParseResult->Argc;
    return TRUE;
}

static void FreeCmdParseResult(cmd_parse_result *ParseResult)
{
	free(ParseResult->Name);
	for(DWORD i = 0; i < ParseResult->Argc; i++) {
		free(ParseResult->Args[i]);
	}
	free(ParseResult->Args);
}

static TCHAR *EatSpaces(TCHAR *Str)
{
	while(_istspace(IntFromTChar(*Str))) {
		Str++;
	}
	return Str;
}

/* Bounded command parsing. Every loop consumes input or returns an error. */
static BOOL ParseCommand(TCHAR *Str, cmd_parse_result *Result)
{
    memset(Result, 0, sizeof(*Result));
    Result->Name = calloc(DEFAULT_STR_SIZE, sizeof(*Result->Name));
    if(!Result->Name) return FALSE;
    Str = EatSpaces(Str);
    size_t Length = 0;
    while(_istalpha(IntFromTChar(*Str))) {
        if(Length + 1 >= DEFAULT_STR_SIZE) goto Error;
        Result->Name[Length++] = *Str++;
    }
    if(!Length || (*Str && !_istspace(IntFromTChar(*Str)))) goto Error;
    while(*(Str = EatSpaces(Str))) {
        if(Result->Argc >= 128) goto Error;
        if(!PushArg(Result)) goto Error;
        TCHAR *Arg = Result->Args[Result->Argc - 1];
        BOOL Quoted = *Str == _T('"');
        if(Quoted) ++Str;
        Length = 0;
        while(*Str && (Quoted ? *Str != _T('"') : !_istspace(IntFromTChar(*Str)))) {
            if(Length + 1 >= DEFAULT_STR_SIZE || (!Quoted && *Str == _T('"'))) goto Error;
            Arg[Length++] = *Str++;
        }
        Arg[Length] = 0;
        if(Quoted) {
            if(*Str != _T('"')) goto Error;
            ++Str;
            if(*Str && !_istspace(IntFromTChar(*Str))) goto Error;
        }
    }
    return TRUE;
Error:
    FreeCmdParseResult(Result);
    return FALSE;
}

static void TryExec(TCHAR *Str)
{
    cmd_parse_result ParseResult;
    /* Preserve Windows executable/argument quoting for the inherited :exec command. */
    if(!_tcsnicmp(Str, _T("exec"), 4) && _istspace(IntFromTChar(Str[4]))) {
        TCHAR *Raw = EatSpaces(Str + 4);
        if(!*Raw) SetViMessage(VI_ERROR, _T("Usage: exec COMMAND"));
        else exec_func(1, &Raw);
        return;
    }

	/* Search has an alias */
	if(Str[0] == _T('/')) {
		TCHAR *Args[1];
		Args[0] = Str + 1;
		search_func(1, Args);
		return;
	}

	if(!ParseCommand(Str, &ParseResult)) {
		SetViMessage(VI_ERROR, _T("parse error"));
		return;
	}

	for(DWORD i = 0; i < _countof(Commands); i++) {
		cmd *Command = &Commands[i];

		if(_tcsicmp(Command->Name, ParseResult.Name) == 0) {
			Command->CmdFunc(ParseResult.Argc, ParseResult.Args);
			FreeCmdParseResult(&ParseResult);
			return;
		}
	}

	SetViMessage(VI_ERROR, _T("Not an editor command: %s"), ParseResult.Name);
	FreeCmdParseResult(&ParseResult);
}

int ViInit(void)
{
    CurrentInputStr = calloc(DEFAULT_STR_SIZE, sizeof *CurrentInputStr);
    return CurrentInputStr != NULL;
}

void ViDestroy(void)
{
    free(CurrentInputStr);
    CurrentInputStr = NULL;
    for(DWORD i = 0; i < HistoryCount; ++i) { free(History[i]); History[i] = NULL; }
    HistoryCount = HistoryIndex = InputIndex = 0;
    InInputMode = 0;
}

void ViEnableInput(TCHAR InitialKey)
{
	memset(CurrentInputStr, 0, DEFAULT_STR_SIZE * sizeof *CurrentInputStr);
	CurrentInputStr[0] = InitialKey;

	ClearViMessage();
	InInputMode = 1;
	InputIndex = 1;
}

void ViDisableInput(void)
{
	CurrentInputStr[0] = 0;
	InInputMode = 0;

	FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
}

int ViHandleInputKey(KEY_EVENT_RECORD *KeyEvent)
{
	switch(KeyEvent->wVirtualKeyCode) {
	case VK_UP:
		if(HistoryCount != 0 && HistoryIndex != 0) {
			HistoryPrevious(&CurrentInputStr);
			return 1;
		}
		break;
	case VK_DOWN:
		if(HistoryCount && HistoryIndex < HistoryCount - 1) {
			HistoryNext(&CurrentInputStr);
			return 1;
		} 
		break;
	case VK_BACK:
		if(InputIndex != 0) {
			CurrentInputStr[--InputIndex] = _T('\0');
			if(InputIndex == 0) {
				ViDisableInput();
			}
			return 1;
		}
		break;
	case VK_ESCAPE:
		ViDisableInput();
		return 1;
	case VK_RETURN:
		ViExecInput();
		return 1;
	default:
		if(_istprint(IntFromTChar(KeyEvent->uChar.UnicodeChar)) && InputIndex + 1 < DEFAULT_STR_SIZE) {
			CurrentInputStr[InputIndex++] = KeyEvent->uChar.UnicodeChar;
            CurrentInputStr[InputIndex] = 0;
			return 1;
		}
	}

	return 0;
}

void ViExecInput(void)
{
	TCHAR *Cmd = CurrentInputStr;

	/*
	 * Ignore all preceeding colons and spaces
	 */
	while(*Cmd == _T(':') || _istspace(IntFromTChar(*Cmd))) {
		Cmd++;
	}

	/*
	 * We check for empty string here because empty strings shouldn't throw errors
	 */
	if(*Cmd != _T('\0')) {
		TryExec(Cmd);
		PushToHistory(CurrentInputStr);
	}

	ViDisableInput();
}
