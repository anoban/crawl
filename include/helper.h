#pragma once

#include <pyreleases.h>

static uint8_t* Open(_In_ const wchar_t* const restrict filename, _Inout_ size_t* const restrict size) {
    DWORD          nbytes  = 0UL;
    LARGE_INTEGER  liFsize = { .QuadPart = 0LLU };
    const HANDLE64 hFile   = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        goto INVALID_HANDLE_ERR;
    }

    if (!GetFileSizeEx(hFile, &liFsize)) {
        fwprintf_s(stderr, L"Error %lu in GetFileSizeEx\n", GetLastError());
        goto GET_FILESIZE_ERR;
    }

    uint8_t* const restrict buffer = malloc(liFsize.QuadPart);
    if (!buffer) {
        fwprintf_s(stderr, L"Error %lu in malloc\n", GetLastError());
        goto GET_FILESIZE_ERR;
    }

    if (!ReadFile(hFile, buffer, liFsize.QuadPart, &nbytes, NULL)) {
        fwprintf_s(stderr, L"Error %lu in ReadFile\n", GetLastError());
        goto READFILE_ERR;
    }

    CloseHandle(hFile);
    *size = liFsize.QuadPart;
    return buffer;

READFILE_ERR:
    free(buffer);
GET_FILESIZE_ERR:
    CloseHandle(hFile);
INVALID_HANDLE_ERR:
    *size = 0;
    return NULL;
}