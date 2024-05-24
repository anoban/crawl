#include <pyreleases.h>

bool ActivateVirtualTerminalEscapes(void) {
    const void* const restrict hConsole = GetStdHandle(STD_OUTPUT_HANDLE); // HANDLE is just a typedef to void*
    DWORD dwConsoleMode                 = 0;

    if (hConsole == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in GetStdHandle.\n", GetLastError());
        return false;
    }

    if (!GetConsoleMode(hConsole, &dwConsoleMode)) {
        fwprintf_s(stderr, L"Error %lu in GetConsoleMode.\n", GetLastError());
        return false;
    }

    dwConsoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hConsole, dwConsoleMode)) {
        fwprintf_s(stderr, L"Error %lu in SetConsoleMode.\n", GetLastError());
        return false;
    }

    return true;
}

// return the offset of the buffer where the stable releases start.
range_t LocateStableReleasesDiv(_In_ const char* const restrict html, _In_ const uint64_t size) {
    range_t delimiters = { .begin = 0, .end = 0 };
    if (!html) return delimiters;

    uint64_t start = 0, end = 0; // NOLINT(readability-isolate-declaration)

    for (uint64_t i = 0; i < size; ++i) {
        // if the text matches the <h2> tag,
        if (html[i] == '<' && html[i + 1] == 'h' && html[i + 2] == '2' && html[i + 3] == '>') {
            // <h2>Stable Releases</h2>
            if (start == 0 && html[i + 4] == 'S' && html[i + 5] == 't' && html[i + 6] == 'a' && html[i + 7] == 'b' && html[i + 8] == 'l' &&
                html[i + 9] == 'e') {
                // the HTML body contains only a single <h2> tag with an inner text that starts with "Stable"
                // so ignoring the " Releases</h2> part for cycle trimming.
                // if the start offset has already been found, do not waste time in this body in subsequent
                // iterations -> short circuiting with the first conditional.
                start = (i + 24);
            }

            // <h2>Pre-releases</h2>
            if (html[i + 4] == 'P' && html[i + 5] == 'r' && html[i + 6] == 'e' && html[i + 7] == '-' && html[i + 8] == 'r' &&
                html[i + 9] == 'e') {
                // the HTML body contains only a single <h2> tag with an inner text that starts with "Pre"
                // so ignoring the "leases</h2> part for cycle trimming.
                end = (i - 1);
                // if found, break out of the loop.
                break;
            }
        }
    }

    delimiters.begin = start;
    delimiters.end   = end;

    return delimiters;
}

results_t ParseStableReleases(_In_ const char* const restrict html, _In_ const uint64_t size) {
    results_t results = { .begin = NULL, .capacity = 0, .count = 0 };

    // if the chunk is NULL or size is not greater than 0,
    if (!html || size <= 0) {
        fputws(L"Error in ParseStableReleases: Possible errors in previous call to LocateStableReleasesDiv!", stderr);
        return results;
    }

    python_t* releases = malloc(sizeof(python_t) * N_PYTHON_RELEASES);
    if (!releases) {
        fputws(L"Error: Memory allocation error in ParseStableReleases!", stderr);
        return results;
    }
    memset(releases, 0, sizeof(python_t) * N_PYTHON_RELEASES);

    unsigned lastwrite = 0; // counter to remember last deserialized struct.

    // start and end offsets of the version and url strings.
    unsigned urlbegin = 0, urlend = 0, versionbegin = 0, versionend = 0; // NOLINT(readability-isolate-declaration)

    // target template -> <a href="https://www.python.org/ftp/python/3.10.11/python-3.10.11-amd64.exe">

    // stores whether the release in an -amd64.exe format release (for x86-64 AMD platforms)
    // needed since other release types like arm64, amd32, zip files have similarly formatted urls that differ only at the end.
    bool is_amd64 = false;

    // (size - 100) to prevent reading past the buffer.

    for (unsigned i = 0; i < size - 100; ++i) {
        // targetting <a ....> tags
        if (html[i] == '<' && html[i + 1] == 'a') {
            if (html[i + 2] == ' ' && html[i + 3] == 'h' && html[i + 4] == 'r' && html[i + 5] == 'e' && html[i + 6] == 'f' &&
                html[i + 7] == '=' && html[i + 8] == '"' && html[i + 9] == 'h' && html[i + 10] == 't' && html[i + 11] == 't' &&
                html[i + 12] == 'p' && html[i + 13] == 's' && html[i + 14] == ':' && html[i + 15] == '/' && html[i + 16] == '/' &&
                html[i + 17] == 'w' && html[i + 18] == 'w' && html[i + 19] == 'w' && html[i + 20] == '.' && html[i + 21] == 'p' &&
                html[i + 22] == 'y' && html[i + 23] == 't' && html[i + 24] == 'h' && html[i + 25] == 'o' && html[i + 26] == 'n' &&
                html[i + 27] == '.' && html[i + 28] == 'o' && html[i + 29] == 'r' && html[i + 30] == 'g' && html[i + 31] == '/' &&
                html[i + 32] == 'f' && html[i + 33] == 't' && html[i + 34] == 'p' && html[i + 35] == '/' && html[i + 36] == 'p' &&
                html[i + 37] == 'y' && html[i + 38] == 't' && html[i + 39] == 'h' && html[i + 40] == 'o' && html[i + 41] == 'n' &&
                html[i + 42] == '/') {
                // targetting <a> tags in the form href="https://www.python.org/ftp/python/ ...>
                urlbegin     = i + 9;  // ...https://www.python.org/ftp/python/.....
                versionbegin = i + 43; // ...3.10.11/python-3.10.11-amd64.exe.....

                for (unsigned j = versionbegin; j < versionbegin + 15; ++j) { // check 15 chars downstream for the next forward slash
                    if (html[j] == '/') {                                     // ...3.10.11/....
                        versionend = j;
                        break;
                    }
                }

                // it'll be more efficient to selectively examine only the -amd64.exe releases! but the token needed to evaluate this occurs at the end of the url! YIKES!

                // the above equality checks will pass even for non -amd64.exe releases, so check the url's end for -amd64.exe
                // ...3.10.11/python-3.10.11-amd64.exe.....
                // (8 + versionend - versionbegin) will help us jump directly to -amd.exe
                // a stride of 8 bytes to skip over "/python-"
                // a stride of (versionend - versionbegin) bytes to skip over "3.10.11"
                for (unsigned k = versionend + 8 + versionend - versionbegin; k < versionend + 8 + versionend - versionbegin + 20;
                     ++k) { // .....-amd64.exe.....
                    if (html[k] == 'a' && html[k + 1] == 'm' && html[k + 2] == 'd' && html[k + 3] == '6' && html[k + 4] == '4' &&
                        html[k + 5] == '.' && html[k + 6] == 'e' && html[k + 7] == 'x' && html[k + 8] == 'e') {
                        urlend   = k + 9;
                        is_amd64 = true;
                        break;
                    }
                }
            }

            if (!is_amd64) continue; // if the release is not an -amd64.exe release,

            // #ifdef _DEBUG
            //             wprintf_s(L"version :: {%5u, %5u}\n", versionbegin, versionend);
            //             wprintf_s(L"url :: {%5u, %5u}\n", urlbegin, urlend);
            // #endif

            // deserialize the chars representing the release version to the struct's version field.
            memcpy_s((releases[lastwrite]).version, VERSION_STRING_LENGTH, html + versionbegin, versionend - versionbegin);
            // deserialize the chars representing the release url to the struct's download_url field.
            memcpy_s((releases[lastwrite]).download_url, DOWNLOAD_URL_LENGTH, html + urlbegin, urlend - urlbegin);

            lastwrite++; // move the write caret
            results.count++;
            is_amd64 = urlbegin = urlend = versionbegin = versionend = 0; // reset the flag & offsets.
        }
    }

    return (results_t) { .begin = releases, .capacity = N_PYTHON_RELEASES, .count = lastwrite };
}

void PrintReleases(_In_ const results_t results, _In_ const char* const restrict system_python_version) {
    // if somehow the system cannot find the installed python version, and an empty buffer is returned,
    const bool is_unavailable = !system_python_version ? true : false;

    // if the buffer is empty don't bother with these...
    if (!is_unavailable) {
        char version_number[BUFF_SIZE] = { 0 };

        for (uint64_t i = 7; i < BUFF_SIZE; ++i) {
            // ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
            // system_python_version will be in the form of "Python 3.10.5"
            // version number starts after offset 7. (@ 8)
            if ((system_python_version[i] >= 46) && (system_python_version[i] <= 57)) version_number[i - 7] = system_python_version[i];
            // if any other characters encountered,
            else
                break;
        }

        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            if (!strcmp(version_number, results.begin[i].version)) // to highlight the system Python version
                wprintf_s(L"|\x1b[35;47;1m   %-7S |  %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
            else
                wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
        _putws(L"-----------------------------------------------------------------------------------");

    } else { // do not bother with highlighting
        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
        _putws(L"-----------------------------------------------------------------------------------");
    }
}

uint8_t* Open(_In_ const wchar_t* const restrict filename, _Inout_ size_t* const restrict size) {
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
        fputws(L"Memory allocation error in Open\n", stderr);
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

bool Serialize(_In_ const uint8_t* const restrict buffer, _In_ const uint32_t size, _In_ const wchar_t* restrict filename) {
    const void* const restrict hfile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hfile == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        return false;
    }

    DWORD nbyteswritten = 0;
    if (!WriteFile(hfile, buffer, size, &nbyteswritten, NULL)) {
        fwprintf_s(stderr, L"Error %lu in WriteFile\n", GetLastError());
        CloseHandle(hfile);
        return false;
    }

    CloseHandle(hfile);

    return true;
}