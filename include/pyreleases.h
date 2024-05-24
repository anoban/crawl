#pragma once

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_MEAN
#define BUFF_SIZE             100LLU
#define HTTP_RESPONSE_SIZE    2097152LLU // 2 MiB
#define N_PYTHON_RELEASES     100LLU
#define DOWNLOAD_URL_LENGTH   150LLU
#define VERSION_STRING_LENGTH 40LLU

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

typedef struct {
        char version[VERSION_STRING_LENGTH];    // version information
        char download_url[DOWNLOAD_URL_LENGTH]; // download URL for amd64 releases
} python_t;

typedef struct {
        HINTERNET session;    // session handle
        HINTERNET connection; // connection handle
        HINTERNET request;    // request handle
} hint3_t;

typedef struct {
        python_t* begin;    // pointer to the head of a heap allocated array of python_ts.s
        uint64_t  capacity; // number of python_ts the heap allocated array can hold
        uint64_t  count;    // number of parsed python_t structs in the array
} results_t;

typedef struct {
        uint64_t begin;
        uint64_t end;
} range_t;

bool ActivateVirtualTerminalEscapes(void);

hint3_t HttpGet(_In_ const wchar_t* const restrict pwszServer, _In_ const wchar_t* const restrict pwszAccessPoint);

char* ReadHttpResponse(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size);

range_t LocateStableReleasesDiv(_In_ const char* const restrict html, _In_ const uint64_t size);

results_t ParseStableReleases(_In_ const char* const restrict html, _In_ const uint64_t size);

void PrintReleases(_In_ const results_t results, _In_ const char* const restrict system_python_version);

bool LaunchPythonExe(void);

bool ReadStdoutPythonExe(_Inout_ char* const restrict buffer, _In_ const DWORD size);

bool GetSystemPythonExeVersion(_Inout_ char* const restrict version_buffer, _In_ const uint64_t buffsize);

uint8_t* Open(_In_ const wchar_t* const restrict filename, _Inout_ size_t* const restrict size);

bool Serialize(_In_ const uint8_t* const restrict buffer, _In_ const uint32_t size, _In_ const wchar_t* restrict filename);

char* ReadHttpResponseEx(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size);