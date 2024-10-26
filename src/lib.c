#include <project.h>

[[deprecated("not needed in modern Win32 applications")]] bool __cdecl __activate_win32_virtual_terminal_escapes(void) {
    const HANDLE64 restrict console_handle = GetStdHandle(STD_OUTPUT_HANDLE); // HANDLE is just a typedef to void*
    unsigned long console_mode             = 0;

    if (console_handle == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in GetStdHandle.\n", GetLastError());
        return false;
    }

    if (!GetConsoleMode(console_handle, &console_mode)) { // capture the current console mode
        fwprintf_s(stderr, L"Error %lu in GetConsoleMode.\n", GetLastError());
        return false;
    }

    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;  // enable virtual terminal escape sequences in the terminal
    if (!SetConsoleMode(console_handle, console_mode)) { // apply it to the current terminal
        fwprintf_s(stderr, L"Error %lu in SetConsoleMode.\n", GetLastError());
        return false;
    }

    return true;
}

// return the offset of the buffer where the stable releases start.
range_t __cdecl locate_stable_releases_htmldiv(_In_ const char* const restrict html, _In_ const unsigned long size) {
    range_t delimiters = { .begin = 0, .end = 0 };
    if (!html) return delimiters;

    unsigned long start = 0, end = 0; // NOLINT(readability-isolate-declaration)

    for (unsigned long i = 0; i < size; ++i) {
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

[[nodiscard]] results_t __cdecl parse_stable_releases(_In_ const char* const restrict html, _In_ const unsigned long size) {
    results_t results = { .begin = NULL, .capacity = 0, .count = 0 };

    // if the chunk is NULL or size is 0,
    if (!html || !size) {
        fputws(L"Error in " __FUNCTIONW__ " : possible errors in previous call to locate_stable_releases_htmldiv!", stderr);
        return results;
    }

    python_t* releases = malloc(sizeof(python_t) * N_PYTHON_RELEASES);
    if (!releases) [[unlikely]] {
        fputws(L"Error: memory allocation error inside " __FUNCTIONW__ "\n", stderr);
        return results;
    }
    memset(releases, 0, sizeof(python_t) * N_PYTHON_RELEASES);

    unsigned long last_write = 0; // counter to remember last deserialized struct.

    // start and end offsets of the version and url strings.
    unsigned long url_begin = 0, url_end = 0, version_begin = 0, version_end = 0; // NOLINT(readability-isolate-declaration)

    // target template -> <a href="https://www.python.org/ftp/python/3.10.11/python-3.10.11-amd64.exe">

    // stores whether the release in an -amd64.exe format release (for x86-64 AMD platforms)
    // needed since other release types like arm64, amd32, zip files have similarly formatted urls that differ only at the end.
    bool is_amd64 = false;

    // (size - 100) to prevent reading past the buffer.
    for (unsigned long i = 0; i < size - 100; ++i) {
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
                url_begin     = i + 9;  // ...https://www.python.org/ftp/python/.....
                version_begin = i + 43; // ...3.10.11/python-3.10.11-amd64.exe.....

                for (unsigned j = version_begin; j < version_begin + 15; ++j) { // check 15 chars downstream for the next forward slash
                    if (html[j] == '/') {                                       // ...3.10.11/....
                        version_end = j;
                        break;
                    }
                }

                // it'll be more efficient to selectively examine only the -amd64.exe releases! but the token needed to evaluate this occurs at the end of the url! YIKES!

                // the above equality checks will pass even for non -amd64.exe releases, so check the url's end for -amd64.exe
                // ...3.10.11/python-3.10.11-amd64.exe.....
                // (8 + versionend - versionbegin) will help us jump directly to -amd.exe
                // a stride of 8 bytes to skip over "/python-"
                // a stride of (versionend - versionbegin) bytes to skip over "3.10.11"

                // NOLINTNEXTLINE(readability-identifier-length)
                for (unsigned k = version_end + 8 + version_end - version_begin; k < version_end + 8 + version_end - version_begin + 20;
                     ++k) {
                    // ...-amd64.exe...
                    if (html[k] == 'a' && html[k + 1] == 'm' && html[k + 2] == 'd' && html[k + 3] == '6' && html[k + 4] == '4' &&
                        html[k + 5] == '.' && html[k + 6] == 'e' && html[k + 7] == 'x' && html[k + 8] == 'e') {
                        url_end  = k + 9;
                        is_amd64 = true;
                        break;
                    }
                }
            }

            if (!is_amd64) continue; // if the release is not an -amd64.exe release,

            // deserialize the chars representing the release version to the struct's version field.
            memcpy_s((releases[last_write]).version, PYTHON_VERSION_STRING_LENGTH, html + version_begin, version_end - version_begin);
            // deserialize the chars representing the release url to the struct's downloadurl field.
            memcpy_s((releases[last_write]).downloadurl, PYTHON_DOWNLOAD_URL_LENGTH, html + url_begin, url_end - url_begin);

            last_write++; // move the write caret
            results.count++;
            // NOLINTNEXTLINE(readability-implicit-bool-conversion) reset the flag & offsets.
            is_amd64 = url_begin = url_end = version_begin = version_end = 0;
        }
    }

    return (results_t) { .begin = releases, .capacity = N_PYTHON_RELEASES, .count = last_write };
}

void __cdecl print(_In_ const results_t results, _In_ const char* const restrict syspyversion) {
    // if somehow the system cannot find the installed python version, and an empty buffer is returned,
    const bool is_unavailable = !syspyversion; // NOLINT(readability-implicit-bool-conversion)

    // if the buffer is empty don't bother with these...
    if (!is_unavailable) [[unlikely]] {
        char version_number[BUFF_SIZE] = { 0 };

        for (unsigned long i = 7; i < BUFF_SIZE; ++i) {
            // ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
            // syspyversion will be in the form of "Python 3.10.5"
            // version number starts after offset 7. (@ 8)
            if ((syspyversion[i] >= 46) && (syspyversion[i] <= 57)) version_number[i - 7] = syspyversion[i];
            // if any other characters encountered,
            else
                break;
        }

        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            if (!strcmp(version_number, results.begin[i].version)) // to highlight the system Python version
                wprintf_s(L"|\x1b[35;47;1m   %-7S |  %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].downloadurl);
            else
                wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].downloadurl);
        _putws(L"-----------------------------------------------------------------------------------");

    } else { // do not bother with highlighting the installed version
        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].downloadurl);
        _putws(L"-----------------------------------------------------------------------------------");
    }
}

[[nodiscard("entails expensive file io"
)]] unsigned char* __cdecl __open(_In_ const wchar_t* const restrict filename, _Inout_ unsigned long* const restrict size) {
    unsigned long bytecount          = 0;
    LARGE_INTEGER fsize              = { .QuadPart = 0 };
    const void* const restrict hfile = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

    if (hfile == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        goto INVALID_HANDLE_ERR;
    }

    if (!GetFileSizeEx(hfile, &fsize)) {
        fwprintf_s(stderr, L"Error %lu in GetFileSizeEx\n", GetLastError());
        goto GET_FILESIZE_ERR;
    }

    unsigned char* const restrict buffer = malloc(fsize.QuadPart);
    if (!buffer) {
        fputws(L"Memory allocation error in __open\n", stderr);
        goto GET_FILESIZE_ERR;
    }

    if (!ReadFile(hfile, buffer, fsize.QuadPart, &bytecount, NULL)) {
        fwprintf_s(stderr, L"Error %lu in ReadFile\n", GetLastError());
        goto READFILE_ERR;
    }

    CloseHandle(hfile);
    *size = fsize.QuadPart;
    return buffer;

READFILE_ERR:
    free(buffer);
GET_FILESIZE_ERR:
    CloseHandle(hfile);
INVALID_HANDLE_ERR:
    *size = 0;
    return NULL;
}

[[nodiscard("entails expensive file io")]] bool __cdecl __serialize(
    _In_ const unsigned char* const restrict buffer, _In_ const unsigned long size, _In_ const wchar_t* const restrict filename
) {
    const HANDLE64 restrict hfile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned long nbytes_written  = 0;

    if (hfile == INVALID_HANDLE_VALUE) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in CreateFileW\n", GetLastError());
        return false;
    }
    // flush the buffer to file
    if (!WriteFile(hfile, buffer, size, &nbytes_written, NULL)) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in WriteFile\n", GetLastError());
        CloseHandle(hfile);
        return false;
    }

    CloseHandle(hfile);
    return true;
}
