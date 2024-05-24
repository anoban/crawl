#include <pyreleases.h>

BOOL ActivateVirtualTerminalEscapes(VOID) {
    const VOID* const restrict hConsole = GetStdHandle(STD_OUTPUT_HANDLE); // HANDLE is just a typedef to VOID*
    DWORD dwConsoleMode                 = 0;

    if (hConsole == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in GetStdHandle.\n", GetLastError());
        return FALSE;
    }

    if (!GetConsoleMode(hConsole, &dwConsoleMode)) {
        fwprintf_s(stderr, L"Error %lu in GetConsoleMode.\n", GetLastError());
        return FALSE;
    }

    dwConsoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hConsole, dwConsoleMode)) {
        fwprintf_s(stderr, L"Error %lu in SetConsoleMode.\n", GetLastError());
        return FALSE;
    }

    return TRUE;
}

// return the offset of the buffer where the stable releases start.
RANGE LocateStableReleasesDiv(_In_ PCSTR const restrict pcszHtml, _In_ const DWORD dwSize) {
    RANGE rDelimiters = { .dwBegin = 0, .dwEnd = 0 };
    if (!pcszHtml) return rDelimiters;

    DWORD dwStart = 0, dwEnd = 0; // NOLINT(readability-isolate-declaration)

    for (DWORD i = 0; i < dwSize; ++i) {
        // if the text matches the <h2> tag,
        if (pcszHtml[i] == '<' && pcszHtml[i + 1] == 'h' && pcszHtml[i + 2] == '2' && pcszHtml[i + 3] == '>') {
            // <h2>Stable Releases</h2>
            if (dwStart == 0 && pcszHtml[i + 4] == 'S' && pcszHtml[i + 5] == 't' && pcszHtml[i + 6] == 'a' && pcszHtml[i + 7] == 'b' &&
                pcszHtml[i + 8] == 'l' && pcszHtml[i + 9] == 'e') {
                // the HTML body contains only a single <h2> tag with an inner text that starts with "Stable"
                // so ignoring the " Releases</h2> part for cycle trimming.
                // if the start offset has already been found, do not waste time in this body in subsequent
                // iterations -> short circuiting with the first conditional.
                dwStart = (i + 24);
            }

            // <h2>Pre-releases</h2>
            if (pcszHtml[i + 4] == 'P' && pcszHtml[i + 5] == 'r' && pcszHtml[i + 6] == 'e' && pcszHtml[i + 7] == '-' &&
                pcszHtml[i + 8] == 'r' && pcszHtml[i + 9] == 'e') {
                // the HTML body contains only a single <h2> tag with an inner text that starts with "Pre"
                // so ignoring the "leases</h2> part for cycle trimming.
                dwEnd = (i - 1);
                // if found, break out of the loop.
                break;
            }
        }
    }

    rDelimiters.dwBegin = dwStart;
    rDelimiters.dwEnd   = dwEnd;

    return rDelimiters;
}

RESULTS ParseStableReleases(_In_ PCSTR const restrict pcszHtml, _In_ const DWORD dwSize) {
    RESULTS reResults = { .begin = NULL, .dwCapacity = 0, .dwCount = 0 };

    // if the chunk is NULL or size is not greater than 0,
    if (!pcszHtml || dwSize <= 0) {
        fputws(L"Error in ParseStableReleases: Possible errors in previous call to LocateStableReleasesDiv!", stderr);
        return reResults;
    }

    PYTHON* pReleases = malloc(sizeof(PYTHON) * N_PYTHON_RELEASES);
    if (!pReleases) {
        fputws(L"Error: Memory allocation error in ParseStableReleases!", stderr);
        return reResults;
    }
    memset(pReleases, 0, sizeof(PYTHON) * N_PYTHON_RELEASES);

    DWORD dwLastWrite = 0; // counter to remember last deserialized struct.

    // start and end offsets of the version and url strings.
    DWORD dwUrlBegin = 0, dwUrlEnd = 0, dwVersionBegin = 0, dwVersionEnd = 0; // NOLINT(readability-isolate-declaration)

    // target template -> <a href="https://www.python.org/ftp/python/3.10.11/python-3.10.11-amd64.exe">

    // stores whether the release in an -amd64.exe format release (for x86-64 AMD platforms)
    // needed since other release types like arm64, amd32, zip files have similarly formatted urls that differ only at the end.
    BOOL bIsAmd64 = FALSE;

    // (size - 100) to prevent reading past the buffer.
    for (DWORD i = 0; i < dwSize - 100; ++i) {
        // targetting <a ....> tags
        if (pcszHtml[i] == '<' && pcszHtml[i + 1] == 'a') {
            if (pcszHtml[i + 2] == ' ' && pcszHtml[i + 3] == 'h' && pcszHtml[i + 4] == 'r' && pcszHtml[i + 5] == 'e' &&
                pcszHtml[i + 6] == 'f' && pcszHtml[i + 7] == '=' && pcszHtml[i + 8] == '"' && pcszHtml[i + 9] == 'h' &&
                pcszHtml[i + 10] == 't' && pcszHtml[i + 11] == 't' && pcszHtml[i + 12] == 'p' && pcszHtml[i + 13] == 's' &&
                pcszHtml[i + 14] == ':' && pcszHtml[i + 15] == '/' && pcszHtml[i + 16] == '/' && pcszHtml[i + 17] == 'w' &&
                pcszHtml[i + 18] == 'w' && pcszHtml[i + 19] == 'w' && pcszHtml[i + 20] == '.' && pcszHtml[i + 21] == 'p' &&
                pcszHtml[i + 22] == 'y' && pcszHtml[i + 23] == 't' && pcszHtml[i + 24] == 'h' && pcszHtml[i + 25] == 'o' &&
                pcszHtml[i + 26] == 'n' && pcszHtml[i + 27] == '.' && pcszHtml[i + 28] == 'o' && pcszHtml[i + 29] == 'r' &&
                pcszHtml[i + 30] == 'g' && pcszHtml[i + 31] == '/' && pcszHtml[i + 32] == 'f' && pcszHtml[i + 33] == 't' &&
                pcszHtml[i + 34] == 'p' && pcszHtml[i + 35] == '/' && pcszHtml[i + 36] == 'p' && pcszHtml[i + 37] == 'y' &&
                pcszHtml[i + 38] == 't' && pcszHtml[i + 39] == 'h' && pcszHtml[i + 40] == 'o' && pcszHtml[i + 41] == 'n' &&
                pcszHtml[i + 42] == '/') {
                // targetting <a> tags in the form href="https://www.python.org/ftp/python/ ...>
                dwUrlBegin     = i + 9;  // ...https://www.python.org/ftp/python/.....
                dwVersionBegin = i + 43; // ...3.10.11/python-3.10.11-amd64.exe.....

                for (unsigned j = dwVersionBegin; j < dwVersionBegin + 15; ++j) { // check 15 chars downstream for the next forward slash
                    if (pcszHtml[j] == '/') {                                     // ...3.10.11/....
                        dwVersionEnd = j;
                        break;
                    }
                }

                // it'll be more efficient to selectively examine only the -amd64.exe releases! but the token needed to evaluate this occurs at the end of the url! YIKES!

                // the above equality checks will pass even for non -amd64.exe releases, so check the url's end for -amd64.exe
                // ...3.10.11/python-3.10.11-amd64.exe.....
                // (8 + versionend - versionbegin) will help us jump directly to -amd.exe
                // a stride of 8 bytes to skip over "/python-"
                // a stride of (versionend - versionbegin) bytes to skip over "3.10.11"
                for (unsigned k = dwVersionEnd + 8 + dwVersionEnd - dwVersionBegin;
                     k < dwVersionEnd + 8 + dwVersionEnd - dwVersionBegin + 20;
                     ++k) { // .....-amd64.exe.....
                    if (pcszHtml[k] == 'a' && pcszHtml[k + 1] == 'm' && pcszHtml[k + 2] == 'd' && pcszHtml[k + 3] == '6' &&
                        pcszHtml[k + 4] == '4' && pcszHtml[k + 5] == '.' && pcszHtml[k + 6] == 'e' && pcszHtml[k + 7] == 'x' &&
                        pcszHtml[k + 8] == 'e') {
                        dwUrlEnd = k + 9;
                        bIsAmd64 = TRUE;
                        break;
                    }
                }
            }

            if (!bIsAmd64) continue; // if the release is not an -amd64.exe release,

            // #ifdef _DEBUG
            //             wprintf_s(L"version :: {%5u, %5u}\n", versionbegin, versionend);
            //             wprintf_s(L"url :: {%5u, %5u}\n", urlbegin, urlend);
            // #endif

            // deserialize the chars representing the release version to the struct's version field.
            memcpy_s((pReleases[dwLastWrite]).szVersion, VERSION_STRING_LENGTH, pcszHtml + dwVersionBegin, dwVersionEnd - dwVersionBegin);
            // deserialize the chars representing the release url to the struct's szDownloadUrl field.
            memcpy_s((pReleases[dwLastWrite]).szDownloadUrl, DOWNLOAD_URL_LENGTH, pcszHtml + dwUrlBegin, dwUrlEnd - dwUrlBegin);

            dwLastWrite++; // move the write caret
            reResults.dwCount++;
            bIsAmd64 = dwUrlBegin = dwUrlEnd = dwVersionBegin = dwVersionEnd = 0; // reset the flag & offsets.
        }
    }

    return (RESULTS) { .begin = pReleases, .dwCapacity = N_PYTHON_RELEASES, .dwCount = dwLastWrite };
}

VOID PrintReleases(_In_ const RESULTS reResults, _In_ PCSTR const restrict pcszSystemPython) {
    // if somehow the system cannot find the installed python version, and an empty buffer is returned,
    const BOOL bIsUnavailable = !pcszSystemPython ? TRUE : FALSE;

    // if the buffer is empty don't bother with these...
    if (!bIsUnavailable) {
        CHAR szVersionNumber[BUFF_SIZE] = { 0 };

        for (DWORD i = 7; i < BUFF_SIZE; ++i) {
            // ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
            // system_python_version will be in the form of "Python 3.10.5"
            // version number starts after offset 7. (@ 8)
            if ((pcszSystemPython[i] >= 46) && (pcszSystemPython[i] <= 57)) szVersionNumber[i - 7] = pcszSystemPython[i];
            // if any other characters encountered,
            else
                break;
        }

        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < reResults.dwCount; ++i)
            if (!strcmp(szVersionNumber, reResults.begin[i].szVersion)) // to highlight the system Python szVersion
                wprintf_s(L"|\x1b[35;47;1m   %-7S |  %-66S \x1b[m|\n", reResults.begin[i].szVersion, reResults.begin[i].szDownloadUrl);
            else
                wprintf_s(
                    L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", reResults.begin[i].szVersion, reResults.begin[i].szDownloadUrl
                );
        _putws(L"-----------------------------------------------------------------------------------");

    } else { // do not bother with highlighting
        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < reResults.dwCount; ++i)
            wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", reResults.begin[i].szVersion, reResults.begin[i].szDownloadUrl);
        _putws(L"-----------------------------------------------------------------------------------");
    }
}

PBYTE Open(_In_ PCWSTR const restrict pcwszFileName, _Inout_ PDWORD const restrict pdwSize) {
    DWORD         dwByteCount        = 0;
    LARGE_INTEGER liFsize            = { .QuadPart = 0 };
    const void* const restrict hFile = CreateFileW(pcwszFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        goto INVALID_HANDLE_ERR;
    }

    if (!GetFileSizeEx(hFile, &liFsize)) {
        fwprintf_s(stderr, L"Error %lu in GetFileSizeEx\n", GetLastError());
        goto GET_FILESIZE_ERR;
    }

    PBYTE const restrict Buffer = malloc(liFsize.QuadPart);
    if (!Buffer) {
        fputws(L"Memory allocation error in Open\n", stderr);
        goto GET_FILESIZE_ERR;
    }

    if (!ReadFile(hFile, Buffer, liFsize.QuadPart, &dwByteCount, NULL)) {
        fwprintf_s(stderr, L"Error %lu in ReadFile\n", GetLastError());
        goto READFILE_ERR;
    }

    CloseHandle(hFile);
    *pdwSize = liFsize.QuadPart;
    return Buffer;

READFILE_ERR:
    free(Buffer);
GET_FILESIZE_ERR:
    CloseHandle(hFile);
INVALID_HANDLE_ERR:
    *pdwSize = 0;
    return NULL;
}

BOOL Serialize(_In_ const BYTE* const restrict Buffer, _In_ const DWORD dwSize, _In_ PCWSTR const restrict pcwszFileName) {
    const VOID* const restrict hfile = CreateFileW(pcwszFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hfile == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        return FALSE;
    }

    DWORD dwBytesWritten = 0;
    if (!WriteFile(hfile, Buffer, dwSize, &dwBytesWritten, NULL)) {
        fwprintf_s(stderr, L"Error %lu in WriteFile\n", GetLastError());
        CloseHandle(hfile);
        return FALSE;
    }

    CloseHandle(hfile);

    return TRUE;
}
