#include <pyreleases.h>

// may be unnecessary, since Windows consoles seem to be sensitive to VTEs without manually customizing the
// Win32 console mode API. At least in these days. MS examples often include this step though! :(

bool ActivateVirtualTerminalEscapes(void) {
    HANDLE hConsole     = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  console_mode = 0;

    if (hConsole == INVALID_HANDLE_VALUE) {
        fwprintf_s(stderr, L"Error %lu in GetStdHandle.\n", GetLastError());
        return false;
    }

    if (!GetConsoleMode(hConsole, &console_mode)) {
        fwprintf_s(stderr, L"Error %lu in GetConsoleMode.\n", GetLastError());
        return false;
    }

    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hConsole, console_mode)) {
        fwprintf_s(stderr, L"Error %lu in SetConsoleMode.\n", GetLastError());
        return false;
    }

    return true;
}

hint3_t HttpGet(_In_ const wchar_t* const restrict server, _In_ const wchar_t* restrict accesspoint) {
    // a convenient wrapper around WinHttp functions.
    // allows to send a GET request and receive the response in one function call without having to deal with the cascade of WinHttp
    // callbacks.

    // WinHttpOpen returns a valid session handle if successful, or NULL otherwise.
    // first of the WinHTTP functions called by an application.
    // initializes internal WinHTTP data structures and prepares for future calls from the application.
    const HINTERNET hSession = WinHttpOpen(
        // impersonating Firefox to avoid request denials.
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/116.0",
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
        server,
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
        accesspoint,
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
    const bool status = WinHttpSendRequest(
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

    // unpack the handles for convenience
    const HINTERNET hSession = handles.session, hConnection = handles.connection, hRequest = handles.request;

    // Calling malloc first and then calling realloc in a do while loop is terribly inefficient for a
    // simple app sending a single GET request.
    // So, malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write operation can start from there such that we can prevent
    // overwriting previously written memory.

    char* const restrict buffer = (char*) malloc(HTTP_RESPONSE_SIZE); // now that's 1 MiB.
    if (!buffer) {
        fputws(L"Memory allocation error in ReadHttpResponse!", stderr);
        return NULL;
    }
    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

    const bool is_received = WinHttpReceiveResponse(hRequest, NULL);
    if (!is_received) {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        free(buffer);
        return NULL;
    }

    uint64_t total_bytes_in_response = 0, total_bytes_read_from_response = 0;
    // uint32_t because Win32 expects DWORDs.
    uint32_t bytes_in_current_query = 0, bytes_read_from_current_query = 0;

    do {
        // for every iteration, zero these counters since these are specific to each query.
        bytes_in_current_query = bytes_read_from_current_query = 0;

        if (!WinHttpQueryDataAvailable(hRequest, &bytes_in_current_query)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpQueryDataAvailable.\n", GetLastError());
            break;
        }

        // If there aren't any more bytes to read,
        if (!bytes_in_current_query) break;

        if (!WinHttpReadData(hRequest, buffer + total_bytes_read_from_response, bytes_in_current_query, &bytes_read_from_current_query)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpReadData.\n", GetLastError());
            break;
        }

        // Increment the total counters.
        total_bytes_in_response        += bytes_in_current_query;
        total_bytes_read_from_response += bytes_read_from_current_query;

        if (total_bytes_read_from_response >= (HTTP_RESPONSE_SIZE - 128U)) {
            fputws(L"Warning: Truncation of response due to insufficient memory!\n", stderr);
            break;
        }

        // while there's data in the response to be read,
    } while (bytes_in_current_query > 0);

    // using the base CloseHandle() here will (did) crash the debug session.
    WinHttpCloseHandle(hSession);
    WinHttpCloseHandle(hConnection);
    WinHttpCloseHandle(hRequest);

#ifdef _DEBUG
    wprintf_s(L"%llu bytes have been received in total.\n", total_bytes_read_from_response);
#endif // _DEBUG

    (*response_size) = total_bytes_read_from_response;
    return buffer;
}

// return the offset of the buffer where the stable releases start.
range_t LocateStableReleasesDiv(_In_ const char* const restrict html, _In_ const uint64_t size) {
    range_t delimiters = { .begin = 0, .end = 0 };
    if (!html) return delimiters;

    uint64_t start_offset = 0, end_offset = 0;

    for (uint64_t i = 0; i < size; ++i) {
        // if the text matches the <h2> tag,
        if (html[i] == '<' && html[i + 1] == 'h' && html[i + 2] == '2' && html[i + 3] == '>') {
            // <h2>Stable Releases</h2>
            if (start_offset == 0 && html[i + 4] == 'S' && html[i + 5] == 't' && html[i + 6] == 'a' && html[i + 7] == 'b' &&
                html[i + 8] == 'l' && html[i + 9] == 'e') {
                // The HTML body contains only a single <h2> tag with an inner text that starts with "Stable"
                // so ignoring the " Releases</h2> part for cycle trimming.
                // If the start offset has already been found, do not waste time in this body in subsequent
                // iterations -> short circuiting with the first conditional.
                start_offset = (i + 24);
            }

            // <h2>Pre-releases</h2>
            if (html[i + 4] == 'P' && html[i + 5] == 'r' && html[i + 6] == 'e' && html[i + 7] == '-' && html[i + 8] == 'r' &&
                html[i + 9] == 'e') {
                // The HTML body contains only a single <h2> tag with an inner text that starts with "Pre"
                // so ignoring the "leases</h2> part for cycle trimming.
                end_offset = (i - 1);
                // If found, break out of the loop.
                break;
            }
        }
    }

    delimiters.begin = start_offset;
    delimiters.end   = end_offset;

    return delimiters;
}

results_t ParseStableReleases(_In_ const char* restrict stable_releases_chunk, _In_ const uint64_t size) {
    // Caller is obliged to free the memory in return.begin.

    // A struct to be returned by this function
    // Holds a pointer to the first python_t struct in the malloced buffer -> begin
    // Number of structs in the allocated memory -> struct_count
    // Number of deserialized structs -> parsed_struct_count

    results_t parse_results = { .begin = NULL, .capacity = N_PYTHON_RELEASES, .count = 0 };

    // if the chunk start is 0 or size is not greater than 0,
    if ((!stable_releases_chunk[0]) || (size <= 0)) return parse_results;

    // Allocate memory for N_PYTHON_RELEASES python_t structs.
    python_t* py_releases = (python_t*) malloc(sizeof(python_t) * N_PYTHON_RELEASES);

    // If malloc failed,
    if (!py_releases) {
        fputws(L"Error: malloc returned NULL in deserialize_stable_releases.\n", stderr);
        return parse_results;
    }

    // Zero out the malloced memory.
    memset(py_releases, 0, sizeof(python_t) * N_PYTHON_RELEASES);

    // A counter to remember last deserialized python_t struct.
    uint64_t last_deserialized_struct_offset = 0;

    // Start and end offsets of the version and url strings.
    uint64_t url_start_offset = 0, url_end_offset = 0, version_start_offset = 0, version_end_offset = 0;

    // Target template ->
    // <a href="https://www.python_t.org/ftp/python_t/3.10.11/python_t-3.10.11-amd64.exe">

    // A bool to keep identify whether the release in a amd64.exe format release.
    // needed since other release types like arm64, amd32, zip files have similarly formatted urls
    // that differ only at the end.
    // Thus, this conditional is needed to skip over those releases
    bool is_amd64 = false;

    // (size - 100) to prevent reading past the buffer.
    for (uint64_t i = 0; i < (size - 100); ++i) {
        if (stable_releases_chunk[i] == '<' && stable_releases_chunk[i + 1] == 'a') {
            if (stable_releases_chunk[i + 2] == ' ' && stable_releases_chunk[i + 3] == 'h' && stable_releases_chunk[i + 4] == 'r' &&
                stable_releases_chunk[i + 5] == 'e' && stable_releases_chunk[i + 6] == 'f' && stable_releases_chunk[i + 7] == '=' &&
                stable_releases_chunk[i + 8] == '"' && stable_releases_chunk[i + 9] == 'h' && stable_releases_chunk[i + 10] == 't' &&
                stable_releases_chunk[i + 11] == 't' && stable_releases_chunk[i + 12] == 'p' && stable_releases_chunk[i + 13] == 's' &&
                stable_releases_chunk[i + 14] == ':' && stable_releases_chunk[i + 15] == '/' && stable_releases_chunk[i + 16] == '/' &&
                stable_releases_chunk[i + 17] == 'w' && stable_releases_chunk[i + 18] == 'w' && stable_releases_chunk[i + 19] == 'w' &&
                stable_releases_chunk[i + 20] == '.' && stable_releases_chunk[i + 21] == 'p' && stable_releases_chunk[i + 22] == 'y' &&
                stable_releases_chunk[i + 23] == 't' && stable_releases_chunk[i + 24] == 'h' && stable_releases_chunk[i + 25] == 'o' &&
                stable_releases_chunk[i + 26] == 'n' && stable_releases_chunk[i + 27] == '.' && stable_releases_chunk[i + 28] == 'o' &&
                stable_releases_chunk[i + 29] == 'r' && stable_releases_chunk[i + 30] == 'g' && stable_releases_chunk[i + 31] == '/' &&
                stable_releases_chunk[i + 32] == 'f' && stable_releases_chunk[i + 33] == 't' && stable_releases_chunk[i + 34] == 'p' &&
                stable_releases_chunk[i + 35] == '/' && stable_releases_chunk[i + 36] == 'p' && stable_releases_chunk[i + 37] == 'y' &&
                stable_releases_chunk[i + 38] == 't' && stable_releases_chunk[i + 39] == 'h' && stable_releases_chunk[i + 40] == 'o' &&
                stable_releases_chunk[i + 41] == 'n' && stable_releases_chunk[i + 42] == '/') {
                url_start_offset     = i + 9;
                version_start_offset = i + 43;

                for (uint32_t j = 0; j < 50; ++j) {
                    if (stable_releases_chunk[i + j + 43] == '/') {
                        version_end_offset = i + j + 43;
                        break;
                    }
                }

                // The above equality checks will pass even for non <>amd64.exe releases :(
                // So, check the url's ending for <>amd64.exe
                for (uint32_t j = 0; j < 50; ++j) {
                    if (stable_releases_chunk[i + j + 43] == 'a' && stable_releases_chunk[i + j + 44] == 'm' &&
                        stable_releases_chunk[i + j + 45] == 'd' && stable_releases_chunk[i + j + 46] == '6' &&
                        stable_releases_chunk[i + j + 47] == '4' && stable_releases_chunk[i + j + 48] == '.' &&
                        stable_releases_chunk[i + j + 49] == 'e' && stable_releases_chunk[i + j + 50] == 'x' &&
                        stable_releases_chunk[i + j + 51] == 'e') {
                        url_end_offset = i + j + 52;
                        // If every char checks out, set the flag true.
                        is_amd64       = true;
                        break;
                    }
                }
            }

            // If the release is indeed an amd64.exe release,
            if (is_amd64) {
                /* Zeroed the whole malloced buffer, at line 283. So, this is unnecessary now. */
                // Zero the struct fields.
                // memset(py_releases[last_deserialized_struct_offset].version_string, 0, 40);
                // memset(py_releases[last_deserialized_struct_offset].amd64_download_url, 0, 150);

                // Copy the chars representing the release version to the deserialized struct's
                // version_string field.
                memcpy_s(
                    (py_releases[last_deserialized_struct_offset]).version,
                    40U,
                    (stable_releases_chunk + version_start_offset),
                    (version_end_offset - version_start_offset)
                );

                // Copy the chars representing the release url to the deserialized struct's
                // amd64_download_url field.
                memcpy_s(
                    (py_releases[last_deserialized_struct_offset]).download_url,
                    150U,
                    (stable_releases_chunk + url_start_offset),
                    (url_end_offset - url_start_offset)
                );

                // Increment the counter for last deserialized struct by one.
                last_deserialized_struct_offset++;

                // Increment the deserialized struct counter in parsedstructs_t by one.
                parse_results.count++;

                // Reset the flag.
                is_amd64         = false;

                // Reset the offsets.
                url_start_offset = url_end_offset = version_start_offset = version_end_offset = 0;

                // If the release is not an amd64.exe,
            } else
                continue;
        }
    }

    parse_results.begin = py_releases;

    return parse_results;
}

void PrintReleases(_In_ const results_t parse_results, _In_ const char* restrict installed_python_version) {
    // if somehow the system cannot find the installed python version, and a empty buffer is returned,
    bool is_empty = (*installed_python_version) == 0 ? true : false;

    // if the buffer is empty don't bother with these...
    if (!is_empty) {
        char python_version[BUFF_SIZE] = { 0 };

        for (uint64_t i = 7; i < BUFF_SIZE; ++i) {
            // ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
            // installed_python_version will be in the form of "Python 3.10.5"
            // Numeric version starts after offset 7. (@ 8)
            if ((installed_python_version[i] >= 46) && (installed_python_version[i] <= 57))
                python_version[i - 7] = installed_python_version[i];
            // if any other characters encountered,
            else
                break;
        }

        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < parse_results.count; ++i) {
            if (!strcmp(python_version, parse_results.begin[i].version)) {
                wprintf_s(L"|\x1b[35;47;1m   %-7S |  %-66S \x1b[m|\n", parse_results.begin[i].version, parse_results.begin[i].download_url);
            } else {
                wprintf_s(
                    L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n",
                    parse_results.begin[i].version,
                    parse_results.begin[i].download_url
                );
            }
        }
        _putws(L"-----------------------------------------------------------------------------------");

    } // !is_empty
    else {
        _putws(L"-----------------------------------------------------------------------------------");
        wprintf_s(L"|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n", L"Version", L"Download URL");
        _putws(L"-----------------------------------------------------------------------------------");
        for (uint64_t i = 0; i < parse_results.count; ++i) {
            wprintf_s(
                L"|\x1b[91m   %-7S \x1b[m| \x1b[32m %-66S \x1b[m|\n", parse_results.begin[i].version, parse_results.begin[i].download_url
            );
        }
        _putws(L"-----------------------------------------------------------------------------------");
    }

    return;
}
