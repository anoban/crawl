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
#pragma warning(disable: 4820)		// struct padding

typedef struct {
	char version_string[40];
	char amd64_download_url[150];
} python_t;

typedef struct {
	HINTERNET session_handle;
	HINTERNET connection_handle;
	HINTERNET request_handle;
} hscr_t;

typedef struct {
	python_t* py_start;
	uint64_t struct_count;
	uint64_t parsed_struct_count;
} parsedstructs_t;

#define RESP_BUFF_SIZE 1048576U		// 1 MiBs
#define N_PYTHON_RELEASES 100U

/*
// Prototypes.
*/

bool activate_vtes(void);

hscr_t http_get(_In_ const wchar_t* restrict pswzServerName, _In_ const wchar_t* restrict pswzAccessPoint);

char* read_http_response(_In_ const hscr_t hscr_t);

char* get_stable_releases(_In_ const char* restrict html_body, _In_ const uint32_t size,
						  _In_ uint32_t* const restrict stable_releases_chunk_size);

parsedstructs_t deserialize_stable_releases(_In_ const char* restrict stable_releases_chunk,
											_In_ const uint64_t size);

void print_python_releases(_In_ const parsedstructs_t parse_results, _In_ const char* restrict installed_python_version);

bool launch_python(void);

bool read_pythons_stdout(_Inout_ const char* restrict write_buffer, _In_ const uint64_t buffsize);

bool get_installed_python_version(_Inout_ const char* restrict version_buffer, _In_ const uint64_t buffsize);