#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_MEAN
#include <Windows.h>
#include <Winhttp.h>
#endif // _WIN32

#define BUFF_SIZE 100U

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
	uint32_t dwStructCount;
	uint32_t dwParsedStructCount;
} ParsedPyStructs;

#define RESP_BUFF_SIZE 1048576U		// 1 MiBs
#define N_PYTHON_RELEASES 100U

/*
// Prototypes.
*/

bool ActivateVirtualTerminalEscapes(void);
SCRHANDLES HttpGet(LPCWSTR pswzServerName, LPCWSTR pswzAccessPoint);
char* ReadHttpResponse(SCRHANDLES scrHandles);
char* GetStableReleases(char* pszHtmlBody, uint32_t dwSize, uint32_t* lpdwStRlsSize);
ParsedPyStructs DeserializeStableReleases(char* pszBody, uint32_t dwSize);
void PrintPythonReleases(ParsedPyStructs ppsResult, char* lpszInstalledVersion);
bool LaunchPython(void);
bool ReadFromPythonsStdout(char* lpszWriteBuffer, uint32_t dwBuffSize);
bool GetPythonVersion(char* lpszVersionBuffer, uint32_t dwBufferSize);