#pragma once

//---------------------------------------------------------------------------//
//                                                                           //
// DO NOT MIX WININET AND WINHTTP FUNCTIONS, THEY DO NOT INTEROPERATE WELL!! //
//                                                                           //
//---------------------------------------------------------------------------//

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_MEAN
#define BUFF_SIZE                    (1LLU << 6)
#define HTTP_RESPONSE_SIZE           2097152LLU // 2 MiB
#define N_PYTHON_RELEASES            100LLU
#define PYTHON_DOWNLOAD_URL_LENGTH   150LLU
#define PYTHON_VERSION_STRING_LENGTH 40LLU
#define EXECUTION_TIMEOUT            100LLU // milliseconds

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Windows.h>
#include <winhttp.h>

#ifdef _DEBUG
    #define dbgwprintf_s(...) fwprintf_s(stderr, __VA_ARGS__)
#else
    #define dbgwprintf_s(...)
#endif // _DEBUG

#pragma comment(lib, "Winhttp.lib") // need this for the WinHttp routines

typedef struct _python {
        char version[PYTHON_VERSION_STRING_LENGTH];   // version information
        char downloadurl[PYTHON_DOWNLOAD_URL_LENGTH]; // download URL for amd64 releases
} python_t;

typedef struct _hinternet_triple {
        HINTERNET session;    // session handle
        HINTERNET connection; // connection handle
        HINTERNET request;    // request handle
} hinternet_triple_t;

typedef struct _results {
        python_t*     begin;    // pointer to the head of a heap allocated array of python_ts.s
        unsigned long count;    // number of parsed python_t structs in the array
        unsigned long capacity; // number of python_ts the heap allocated array can hold
} results_t;

typedef struct _range {
        unsigned long begin;
        unsigned long end;
} range_t;

// enables printing coloured outputs to console. unnecessary as Windows console by default seems to be sensitive to VTE without manually enabling them
[[deprecated("not needed in modern Win32 applications")]] bool __cdecl __activate_win32_virtual_terminal_escapes(void);

// a convenient wrapper around WinHttp functions that allows sending a GET request and receiving the response back in one function call without
// having to deal with the cascade of WinHttp callbacks, can handle gzip or DEFLATE compressed responses internally!
[[nodiscard("entails expensive http io"
)]] hinternet_triple_t __cdecl http_get(_In_ const wchar_t* const restrict server, _In_ const wchar_t* const restrict accesspoint);

// reads in the HTTP response content as a char buffer (automatic decompression will take place if the response is gzip or DEFLATE compressed)
[[nodiscard("entails expensive http io"
)]] char* __cdecl read_http_response(_In_ const hinternet_triple_t handles, _Inout_ unsigned long* const restrict size);

// an advanced variant of read_http_response that uses WinHttpReadDataEx internally to retrieve the whole content of the response at once unlike
// read_http_response which combines WinHttpQueryDataAvailable and WinHttpReadData to retrieve the contents in chunks, iteratively
[[nodiscard("entails expensive http io"
)]] char* __cdecl read_http_response_ex(_In_ const hinternet_triple_t handles, _Inout_ unsigned long* const restrict size);

// finds the start and end of the HTML div containing the stable releases section of the python.org downloads page
range_t __cdecl locate_stable_releases_htmldiv(_In_ const char* const restrict html, _In_ const unsigned long size);

// extracts information of URLs and versions from the input string buffer, caller is responsible for freeing the memory allocated in return.begin
[[nodiscard]] results_t __cdecl parse_stable_releases(_In_ const char* const restrict html, _In_ const unsigned long size);

// Coloured console outputs of the deserialized structs.
void __cdecl print(_In_ const results_t results, _In_ const char* const restrict syspyversion);

// Launches python.exe in a separate process, will use the python.exe in PATH in release mode and in debug mode the dummy ./python/x64/Debug/python.exe will be launched, with --version as argument
bool __cdecl launch_python(void);

// Reads and captures the stdout of the launched python.exe, by previous call to launch_python.
bool __cdecl read_stdout_python(_Inout_ char* const restrict buffer, _In_ const unsigned long size);

// A wrapper encapsulating launch_python and launch_python, for convenience.
bool __cdecl get_system_python_version(_Inout_ char* const restrict version, _In_ const unsigned long size);

// Utility function :: read a file from disk into a buffer in read-only mode, caller should take care of (free) the buffer post-use.
[[nodiscard("entails expensive file io"
)]] unsigned char* __cdecl __open(_In_ const wchar_t* const restrict filename, _Inout_ unsigned long* const restrict size);

// Utility function :: serializes a buffer to disk, with overwrite privileges, caller should free the buffer post-serialization.
[[nodiscard("entails expensive file io")]] bool __cdecl __serialize(
    _In_ const unsigned char* const restrict buffer, _In_ const unsigned long size, _In_ const wchar_t* const restrict filename
);
