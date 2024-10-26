#pragma once

///////////////////////////////////////////////////////////////////////////////
// DO NOT MIX WININET AND WINHTTP FUNCTIONS, THEY DO NOT INTEROPERATE WELL!! //
///////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_MEAN
#define BUFF_SIZE             100LLU
#define HTTP_RESPONSE_SIZE    2097152LLU // 2 MiB
#define N_PYTHON_RELEASES     100LLU
#define DOWNLOAD_URL_LENGTH   150LLU
#define VERSION_STRING_LENGTH 40LLU
#define EXECUTION_TIMEOUT     100 // millisecs

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

typedef struct _python {
        CHAR szVersion[VERSION_STRING_LENGTH];   // version information
        CHAR szDownloadUrl[DOWNLOAD_URL_LENGTH]; // download URL for amd64 releases
} python_t;

typedef struct tagHINT3 {
        HINTERNET hSession;    // session handle
        HINTERNET hConnection; // connection handle
        HINTERNET hRequest;    // request handle
} HINT3;

typedef struct _results {
        python_t*     begin;      // pointer to the head of a heap allocated array of python_ts.s
        unsigned long dwCapacity; // number of python_ts the heap allocated array can hold
        unsigned long dwCount;    // number of parsed python_t structs in the array
} results_t;

typedef struct _range {
        unsigned long begin;
        unsigned long end;
} range_t;

// Enables printing coloured outputs to console. May be unnecessary as Windows console by default seems to be sensitive to VTEs without manually enabling it.
bool __activate_win32_virtual_terminal_escapes(void);

// A convenient wrapper around WinHttp functions that allows to send a GET request and receive the response in one function call without having to deal with the cascade of WinHttp callbacks.
// Can handle gzip or DEFLATE compressed responses internally!
HINT3 http_get(_In_ LPCWSTR const restrict pwszServer, _In_ LPCWSTR const restrict pwszAccessPoint);

// Reads in the HTTP response content as a char buffer (automatic decompression will take place if the response is gzip or DEFLATE compressed)
PBYTE ReadHttpResponse(_In_ const HINT3 hi3Handles, _Inout_ PDWORD const restrict pdwRespSize);

// A variant of ReadHttpResponse that uses WinHttpReadDataEx internally to retrieve the whole content of the response at once unlike ReadHttpResponse which combines WinHttpQueryDataAvailable and WinHttpReadData to retrieve the contents in chunks, iteratively.
PBYTE ReadHttpResponseEx(_In_ const HINT3 hi3Handles, _Inout_ PDWORD const restrict pdwRespSize);

// Finds the start and end of the HTML div containing stable releases
range_t LocateStableReleasesDiv(_In_ PCSTR const restrict pcszHtml, _In_ const unsigned long dwSize);

// Extracts information of URLs and versions from the input string buffer, caller is obliged to free the memory allocated in return.begin.
results_t ParseStableReleases(_In_ PCSTR const restrict pcszHtml, _In_ const unsigned long dwSize);

// Coloured console outputs of the deserialized structs.
void PrintReleases(_In_ const results_t reResults, _In_ PCSTR const restrict pcszSystemPython);

// Launches python.exe in a separate process, will use the python.exe in PATH in release mode and in debug mode the dummy ./python/x64/Debug/python.exe will be launched, with --version as argument
bool LaunchPythonExe(void);

// Reads and captures the stdout of the launched python.exe, by previous call to LaunchPythonExe.
bool ReadStdoutPythonExe(_Inout_ PSTR const restrict pszBuffer, _In_ const unsigned long dwSize);

// A wrapper encapsulating LaunchPythonExe and LaunchPythonExe, for convenience.
bool GetSystemPythonExeVersion(_Inout_ PSTR const restrict pszVersion, _In_ const unsigned long dwSize);

// Utility function :: read a file from disk into a buffer in read-only mode, caller should take care of (free) the buffer post-use.
PBYTE Open(_In_ PCWSTR const restrict pcwszFileName, _Inout_ PDWORD const restrict pdwSize);

// Utility function :: serializes a buffer to disk, with overwrite privileges, caller should free the buffer post-serialization.
bool Serialize(_In_ const BYTE* const restrict Buffer, _In_ const unsigned long dwSize, _In_ PCWSTR const restrict pcwszFileName);
