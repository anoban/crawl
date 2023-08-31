#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_MEAN
#endif // _WIN32
#define BUFF_SIZE 100U

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <Windows.h>
#include <Winhttp.h>

#pragma comment(lib, "Winhttp")
#pragma warning(disable: 4710)

typedef struct {
	char pszVersionString[40];
	char pszAMD64DownloadURL[150];
} Python;

typedef struct {
	HINTERNET hSession;
	HINTERNET hConnection;
	HINTERNET hRequest;
} SCRHANDLES;

typedef struct {
	Python* pyStart;
	DWORD dwStructCount;
	DWORD dwParsedStructCount;
} ParsedPyStructs;

#define RESP_BUFF_SIZE 1048576U		// 1 MiBs
#define N_PYTHON_RELEASES 100U

/*
// Prototypes.
*/

BOOL ActivateVirtualTerminalEscapes(VOID);
SCRHANDLES HttpGet(LPCWSTR pswzServerName, LPCWSTR pswzAccessPoint);
LPSTR ReadHttpResponse(SCRHANDLES scrHandles);
LPSTR GetStableReleases(LPSTR pszHtmlBody, DWORD dwSize, LPDWORD lpdwStRlsSize);
ParsedPyStructs DeserializeStableReleases(LPSTR pszBody, DWORD dwSize);
VOID PrintPythonReleases(ParsedPyStructs ppsResult, LPSTR lpszInstalledVersion);
BOOL LaunchPython(VOID);
BOOL ReadFromPythonsStdout(LPSTR lpszWriteBuffer, DWORD dwBuffSize);
BOOL GetPythonVersion(LPSTR lpszVersionBuffer, DWORD dwBufferSize);