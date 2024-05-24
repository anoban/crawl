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

// Enables printing coloured outputs to console. May be unnecessary as Windows console by default seems to be sensitive to VTEs without manually enabling it.
bool ActivateVirtualTerminalEscapes(void);

// A convenient wrapper around WinHttp functions that allows to send a GET request and receive the response in one function call without having to deal with the cascade of WinHttp callbacks.
// Can handle gzip or DEFLATE compressed responses internally!
hint3_t HttpGet(_In_ const wchar_t* const restrict pwszServer, _In_ const wchar_t* const restrict pwszAccessPoint);

// Reads in the HTTP response content as a char buffer (automatic decompression will take place if the response is gzip or DEFLATE compressed)
char* ReadHttpResponse(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size);

// A variant of ReadHttpResponse that uses WinHttpReadDataEx internally to retrieve the whole content of the response at once unlike ReadHttpResponse which combines WinHttpQueryDataAvailable and WinHttpReadData to retrieve the contents in chunks, iteratively.
char* ReadHttpResponseEx(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size);

// Finds the start and end of the HTML div containing stable releases
range_t LocateStableReleasesDiv(_In_ const char* const restrict html, _In_ const uint64_t size);

// Extracts information of URLs and versions from the input string buffer, caller is obliged to free the memory allocated in return.begin.
results_t ParseStableReleases(_In_ const char* const restrict html, _In_ const uint64_t size);

// Coloured console outputs of the deserialized structs.
void PrintReleases(_In_ const results_t results, _In_ const char* const restrict system_python_version);

// Launches python.exe in a separate process, will use the python.exe in PATH in release mode and in debug mode the dummy ./python/x64/Debug/python.exe will be launched, with --version as argument
bool LaunchPythonExe(void);

// Reads and captures the stdout of the launched python.exe, by previous call to LaunchPythonExe.
bool ReadStdoutPythonExe(_Inout_ char* const restrict buffer, _In_ const DWORD size);

// A wrapper encapsulating LaunchPythonExe and LaunchPythonExe, for convenience.
bool GetSystemPythonExeVersion(_Inout_ char* const restrict version_buffer, _In_ const uint64_t buffsize);

// Utility function :: read a file from disk into a buffer in read-only mode, caller should take care of (free) the buffer post-use.
uint8_t* Open(_In_ const wchar_t* const restrict filename, _Inout_ size_t* const restrict size);

// Utility function :: serializes a buffer to disk, with overwrite privileges, caller should free the buffer post-serialization.
bool Serialize(_In_ const uint8_t* const restrict buffer, _In_ const uint32_t size, _In_ const wchar_t* restrict filename);
