#include <pyreleases.h>

// may be unnecessary, Windows console by default seems to be sensitive to VTEs without manually enabling it.
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

// a convenient wrapper around WinHttp functions.
// allows to send a GET request and receive the response in one function call without having to deal with the cascade of WinHttp callbacks.
hint3_t HttpGet(_In_ const wchar_t* const restrict pwszServer, _In_ const wchar_t* const restrict pwszAccessPoint) {
    // WinHttpOpen returns a valid session handle if successful, or NULL otherwise.
    // first of the WinHTTP functions called by an application.
    // initializes internal WinHTTP data structures and prepares for future calls from the application.
    const HINTERNET hSession = WinHttpOpen(
        // impersonating Firefox to avoid request denials.
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:126.0) Gecko/20100101 Firefox/126.0", // modified on 22-05-2024
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) {
        fwprintf_s(stderr, L"Error %lu in WinHttpOpen.\n", GetLastError());
        goto PREMATURE_RETURN;
    }

    // WinHttpConnect specifies the initial target server of an HTTP request and returns an HINTERNET connection handle
    // to an HTTP session for that initial target.
    // returns a valid connection handle to the HTTP session if the connection is successful, or NULL otherwise.
    const HINTERNET hConnection = WinHttpConnect(
        hSession,
        pwszServer,
        INTERNET_DEFAULT_HTTP_PORT, // uses port 80 for HTTP and port 443 for HTTPS.
        0
    );

    if (!hConnection) {
        fwprintf_s(stderr, L"Error %lu in WinHttpConnect.\n", GetLastError());
        goto CLOSE_SESSION_HANDLE;
    }

    // WinHttpOpenRequest creates an HTTP request handle.
    // an HTTP request handle holds a request to send to an HTTP server and contains all RFC822/MIME/HTTP headers to be sent as part of the
    // request.
    const HINTERNET hRequest = WinHttpOpenRequest(
        hConnection,
        L"GET",
        pwszAccessPoint,
        NULL, // pointer to a string that contains the HTTP version. If this parameter is NULL, the function uses HTTP/1.1
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, // pointer to a null-terminated array of string pointers that
        // specifies media types accepted by the client.
        // WINHTTP_DEFAULT_ACCEPT_TYPES, no types are accepted by the client.
        // typically, servers handle a lack of accepted types as indication that the client accepts
        // only documents of type "text/*"; that is, only text documents & no pictures or other binary files
        0
    );

    if (!hRequest) {
        fwprintf_s(stderr, L"Error %lu in the WinHttpOpenRequest.\n", GetLastError());
        goto CLOSE_CONNECTION_HANDLE;
    }

    // WinHttpSendRequest sends the specified request to the HTTP server and returns true if successful, or false otherwise.
    const BOOL status = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, // pointer to a string that contains the additional headers to append to the request.
        0,                             // an unsigned long integer value that contains the length, in characters, of the additional headers.
        WINHTTP_NO_REQUEST_DATA,       // pointer to a buffer that contains any optional data to send immediately after the request headers
        0,                             // an unsigned long integer value that contains the length, in bytes, of the optional data.
        0,                             // an unsigned long integer value that contains the length, in bytes, of the total data sent.
        0
    ); // a pointer to a pointer-sized variable that contains an application-defined value that is passed, with the request handle, to
        // any callback functions.

    if (!status) {
        fwprintf_s(stderr, L"Error %lu in the WinHttpSendRequest.\n", GetLastError());
        goto CLOSE_REQUEST_HANDLE;
    }

    // these 3 handles need to be closed by the caller.
    return (hint3_t) { .session = hSession, .connection = hConnection, .request = hRequest };

// cleanup
CLOSE_REQUEST_HANDLE:
    WinHttpCloseHandle(hRequest);
CLOSE_CONNECTION_HANDLE:
    WinHttpCloseHandle(hConnection);
CLOSE_SESSION_HANDLE:
    WinHttpCloseHandle(hSession);
PREMATURE_RETURN:
    return (hint3_t) { .session = NULL, .connection = NULL, .request = NULL };
}

char* ReadHttpResponse(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size) {
    // if the call to HttpGet() failed,
    if (handles.session == NULL || handles.connection == NULL || handles.request == NULL) {
        fputws(L"ReadHttpResponse failed! (Errors in previous call to HttpGet)\n", stderr);
        return NULL;
    }

    // NOLINTBEGIN(readability-isolate-declaration)
    const HINTERNET hSession = handles.session, hConnection = handles.connection,
                    hRequest    = handles.request; // unpack the handles for convenience
    // NOLINTEND(readability-isolate-declaration)

    // calling malloc first and then calling realloc in a do while loop is terribly inefficient for a simple app sending a single GET request.
    // we'll malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write can start from where the last write terminated

    char* const restrict buffer = malloc(HTTP_RESPONSE_SIZE);
    if (!buffer) {
        fputws(L"Memory allocation error in ReadHttpResponse!", stderr);
        return NULL;
    }
    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

    const BOOL bIsReceived = WinHttpReceiveResponse(hRequest, NULL);
    if (!bIsReceived) {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        free(buffer);
        return NULL;
    }

    DWORD dwTotalBytesRead = 0, dwBytesCurrentQuery = 0, dwBytesReadCurrentQuery = 0; // NOLINT(readability-isolate-declaration)

    do {
        // for every iteration, zero these counters since these are specific to each query.
        dwBytesCurrentQuery = dwBytesReadCurrentQuery = 0;

        if (!WinHttpQueryDataAvailable(hRequest, &dwBytesCurrentQuery)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpQueryDataAvailable.\n", GetLastError());
            break;
        }

        if (!dwBytesCurrentQuery) break; // if there aren't any more bytes to read,

        if (!WinHttpReadData(hRequest, buffer + dwTotalBytesRead, dwBytesCurrentQuery, &dwBytesReadCurrentQuery)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpReadData.\n", GetLastError());
            break;
        }

        dwTotalBytesRead += dwBytesReadCurrentQuery;

        if (dwTotalBytesRead >= (HTTP_RESPONSE_SIZE - 128U)) {
            fputws(L"Warning: Truncation of response due to insufficient memory!\n", stderr);
            break;
        }

#ifdef _DEBUG
        wprintf_s(L"This query collected %lu bytes from the response\n", dwBytesCurrentQuery);
#endif // _DEBUG

    } while (dwBytesCurrentQuery > 0); // while there's still data in the response,

    // using regular CloseHandle() to close HINTERNET handles will (did) crash the debug session.
    WinHttpCloseHandle(hSession);
    WinHttpCloseHandle(hConnection);
    WinHttpCloseHandle(hRequest);

    *response_size = dwTotalBytesRead;
    return buffer;
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

// caller is obliged to free the memory allocated in return.begin.
results_t ParseStableReleases(_In_ const char* const restrict stable_releases, _In_ const uint64_t size) {
    results_t results = { .begin = NULL, .capacity = 0, .count = 0 };

    // if the chunk is NULL or size is not greater than 0,
    if (!stable_releases || size <= 0) return results;

    python_t* releases = malloc(sizeof(python_t) * N_PYTHON_RELEASES);
    if (!releases) {
        fputws(L"Error: Memory allocation error in ParseStableReleases!", stderr);
        return results;
    }
    memset(releases, 0, sizeof(python_t) * N_PYTHON_RELEASES);

    uint64_t lastwrite = 0; // counter to remember last deserialized struct.

    // start and end offsets of the version and url strings.
    uint64_t urlbegin = 0, urlend = 0, versionbegin = 0, versionend = 0; // NOLINT(readability-isolate-declaration)

    // target template -> <a href="https://www.python.org/ftp/python/3.10.11/python-3.10.11-amd64.exe">

    // stores whether the release in an -amd64.exe format release (for x86-64 AMD platforms)
    // needed since other release types like arm64, amd32, zip files have similarly formatted urls that differ only at the end.
    bool is_amd64 = false;

    // (size - 100) to prevent reading past the buffer.
    for (unsigned i = 0; i < (size - 100); ++i) {
        if (stable_releases[i] == '<' && stable_releases[i + 1] == 'a') { // targetting <a> tags
            if (stable_releases[i + 2] == ' ' && stable_releases[i + 3] == 'h' && stable_releases[i + 4] == 'r' &&
                stable_releases[i + 5] == 'e' && stable_releases[i + 6] == 'f' && stable_releases[i + 7] == '=' &&
                stable_releases[i + 8] == '"' && stable_releases[i + 9] == 'h' && stable_releases[i + 10] == 't' &&
                stable_releases[i + 11] == 't' && stable_releases[i + 12] == 'p' && stable_releases[i + 13] == 's' &&
                stable_releases[i + 14] == ':' && stable_releases[i + 15] == '/' && stable_releases[i + 16] == '/' &&
                stable_releases[i + 17] == 'w' && stable_releases[i + 18] == 'w' && stable_releases[i + 19] == 'w' &&
                stable_releases[i + 20] == '.' && stable_releases[i + 21] == 'p' && stable_releases[i + 22] == 'y' &&
                stable_releases[i + 23] == 't' && stable_releases[i + 24] == 'h' && stable_releases[i + 25] == 'o' &&
                stable_releases[i + 26] == 'n' && stable_releases[i + 27] == '.' && stable_releases[i + 28] == 'o' &&
                stable_releases[i + 29] == 'r' && stable_releases[i + 30] == 'g' && stable_releases[i + 31] == '/' &&
                stable_releases[i + 32] == 'f' && stable_releases[i + 33] == 't' && stable_releases[i + 34] == 'p' &&
                stable_releases[i + 35] == '/' && stable_releases[i + 36] == 'p' && stable_releases[i + 37] == 'y' &&
                stable_releases[i + 38] == 't' && stable_releases[i + 39] == 'h' && stable_releases[i + 40] == 'o' &&
                stable_releases[i + 41] == 'n' && stable_releases[i + 42] == '/') {
                // targetting <a> tags in the form href="https://www.python.org/ftp/python/ ...>
                urlbegin     = i + 9;                                         // ...https://www.python.org/ftp/python/.....
                versionbegin = i + 43;                                        // ...3.10.11/python-3.10.11-amd64.exe.....

                for (unsigned j = versionbegin; j < versionbegin + 15; ++j) { // check 15 chars downstream for the next forward slash
                    if (stable_releases[j] == '/') {                          // ...3.10.11/....
                        versionend = versionbegin + j - 1;
                        break;
                    }
                }
                // it'll be more efficient to selectively examine only the -amd64.exe releases! but the token needed to evaluate this occurs at the end of the url! YIKES!

                // the above equality checks will pass even for non -amd64.exe releases, so check the url's end for -amd64.exe
                // ...3.10.11/python-3.10.11-amd64.exe.....
                // (8 + versionend - versionbegin) will help us jump directly to -amd.exe
                // a stride of 8 bytes to skip over "/python-"
                // a stride of (versionend - versionbegin) bytes to skip over "3.10.11"
                for (unsigned k = versionend + 8 + versionend - versionbegin; k < 20; ++k) { // .....-amd64.exe.....
                    if (stable_releases[k + 43] == 'a' && stable_releases[k + 44] == 'm' && stable_releases[k + 45] == 'd' &&
                        stable_releases[k + 46] == '6' && stable_releases[k + 47] == '4' && stable_releases[k + 48] == '.' &&
                        stable_releases[k + 49] == 'e' && stable_releases[k + 50] == 'x' && stable_releases[k + 51] == 'e') {
                        urlend   = k + 52;
                        is_amd64 = true;
                        break;
                    }
                }
            }

            // if the release is not an -amd64.exe release,
            if (!is_amd64) continue;

            /* Zeroed the whole malloced buffer, at line 283. So, this is unnecessary now. */
            // Zero the struct fields.
            // memset(py_releases[last_deserialized_struct_offset].version_string, 0, 40);
            // memset(py_releases[last_deserialized_struct_offset].amd64_download_url, 0, 150);

            // Copy the chars representing the release version to the deserialized struct's
            // version_string field.
            memcpy_s((releases[lastwrite]).version, 40U, (stable_releases + versionbegin), (versionend - versionbegin));

            // Copy the chars representing the release url to the deserialized struct's
            // amd64_download_url field.
            memcpy_s((releases[lastwrite]).download_url, 150U, (stable_releases + urlbegin), (urlend - urlbegin));

            // Increment the counter for last deserialized struct by one.
            lastwrite++;

            // Increment the deserialized struct counter in parsedstructs_t by one.
            results.count++;

            // Reset the flag.
            is_amd64 = false;

            // Reset the offsets.
            urlbegin = urlend = versionbegin = versionend = 0;

            // If the release is not an amd64.exe,
        }
    }

    results.begin = releases;

    return results;
}

void PrintReleases(_In_ const results_t results, _In_ const char* const restrict system_python_version) {
    // if somehow the system cannot find the installed python version, and a empty buffer is returned,
    const bool is_unavailable = (!system_python_version) ? true : false;

    // if the buffer is empty don't bother with these...
    if (!is_unavailable) {
        char python_version[BUFF_SIZE] = { 0 };

        for (uint64_t i = 7; i < BUFF_SIZE; ++i) {
            // ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
            // system_python_version will be in the form of "Python 3.10.5"
            // Numeric version starts after offset 7. (@ 8)
            if ((system_python_version[i] >= 46) && (system_python_version[i] <= 57)) python_version[i - 7] = system_python_version[i];
            // if any other characters encountered,
            else
                break;
        }

        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            if (!strcmp(python_version, results.begin[i].version))
                wprintf_s(L"|\x1b[35;47;1m   %-7S |  %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
            else
                wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
        _putws(L"-----------------------------------------------------------------------------------");

    } // !is_unavailable
    else {
        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < results.count; ++i)
            wprintf_s(L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", results.begin[i].version, results.begin[i].download_url);
        _putws(L"-----------------------------------------------------------------------------------");
    }
}
