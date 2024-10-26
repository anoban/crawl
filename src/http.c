#include <pyreleases.h>

extern HANDLE64 process_heap_handle; // defined in main.c & test.c and initialized inside wmain()

[[nodiscard("entails expensive http io")]] hinternet_triple_t http_get(
    _In_ const wchar_t* const restrict server, _In_ const wchar_t* const restrict accesspoint
) {
    // WinHttpOpen returns a valid session handle if successful, or NULL otherwise.
    // first of the WinHTTP functions called by an application.
    // initializes internal WinHTTP data structures and prepares for future calls from the application.
    const HINTERNET session_handle = WinHttpOpen(
        // impersonating Firefox to avoid request denials.
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:126.0) Gecko/20100101 Firefox/126.0", // modified on 22-05-2024
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session_handle) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in WinHttpOpen.\n", GetLastError());
        goto PREMATURE_RETURN;
    }

    // as of 24/05/2024 www.python.org/downloads/windows/ sends a gzipped file in response to a GET request
    // this used to be a regular text (HTML) file. so, to decompress and get the text output there are two options!
    // 1) specify WINHTTP_DECOMPRESSION_FLAG_ALL in WinHttp, so the library will handle DEFLATE/gzip decoding internally
    // 2) get the raw compressed bytes and do the decoding by hand
    // I'm opting for the first option :p
    // https://learn.microsoft.com/en-us/windows/win32/wininet/content-encoding
    const int __enable                     = 1;
    // WINHTTP_OPTION_DECOMPRESSION and WINHTTP_DECOMPRESSION_FLAG_ALL need to be set using separate calls to WinHttpSetOption
    // combining the two flags with bitwise or in a single call to WinHttpSetOption won't (didn't) work!
    bool      is_winhttp_decoding_enabled  = WinHttpSetOption( // NOLINT(readability-implicit-bool-conversion)
        session_handle,
        WINHTTP_OPTION_DECOMPRESSION,
        &__enable,
        sizeof(__enable)
    );
    is_winhttp_decoding_enabled           &= WinHttpSetOption(session_handle, WINHTTP_DECOMPRESSION_FLAG_ALL, &__enable, sizeof(__enable));

    if (!is_winhttp_decoding_enabled) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in the WinHttpSetOption, DEFLATE/gzip decompression request failed!\n", GetLastError());
        goto CLOSE_SESSION_HANDLE;
    }

    // WinHttpConnect specifies the initial target server of an HTTP request and returns an HINTERNET connection handle
    // to an HTTP session for that initial target.
    // returns a valid connection handle to the HTTP session if the connection is successful, or NULL otherwise.
    const HINTERNET connection_handle = WinHttpConnect(
        session_handle,
        server,
        INTERNET_DEFAULT_HTTP_PORT, // uses port 80 for HTTP and port 443 for HTTPS.
        0
    );

    if (!connection_handle) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in WinHttpConnect.\n", GetLastError());
        goto CLOSE_SESSION_HANDLE;
    }

    // WinHttpOpenRequest creates an HTTP request handle.
    // an HTTP request handle holds a request to send to an HTTP server and contains all RFC822/MIME/HTTP headers to be sent as part of the
    // request.
    const HINTERNET request_handle = WinHttpOpenRequest(
        connection_handle,
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

    if (!request_handle) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in the WinHttpOpenRequest.\n", GetLastError());
        goto CLOSE_CONNECTION_HANDLE;
    }

    // WinHttpSendRequest sends the specified request to the HTTP server and returns true if successful, or false otherwise.
    const bool is_request_sent = WinHttpSendRequest( // NOLINT(readability-implicit-bool-conversion)
        request_handle,
        WINHTTP_NO_ADDITIONAL_HEADERS, // pointer to a string that contains the additional headers to append to the request.
        0,                             // an unsigned long integer value that contains the length, in characters, of the additional headers.
        WINHTTP_NO_REQUEST_DATA,       // pointer to a buffer that contains any optional data to send immediately after the request headers
        0,                             // an unsigned long integer value that contains the length, in bytes, of the optional data.
        0,                             // an unsigned long integer value that contains the length, in bytes, of the total data sent.
        0
    ); // a pointer to a pointer-sized variable that contains an application-defined value that is passed, with the request handle, to
        // any callback functions.

    if (!is_request_sent) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in the WinHttpSendRequest.\n", GetLastError());
        goto CLOSE_REQUEST_HANDLE;
    }

    // these 3 handles need to be closed by the caller.
    return (hinternet_triple_t) { .session = session_handle, .connection = connection_handle, .request = request_handle };

// cleanup
CLOSE_REQUEST_HANDLE:
    WinHttpCloseHandle(request_handle);
CLOSE_CONNECTION_HANDLE:
    WinHttpCloseHandle(connection_handle);
CLOSE_SESSION_HANDLE:
    WinHttpCloseHandle(session_handle);
PREMATURE_RETURN:
    return (hinternet_triple_t) { .session = NULL, .connection = NULL, .request = NULL };
}

[[nodiscard("entails expensive http io")]] unsigned char* read_http_response(
    _In_ const hinternet_triple_t handles, _Inout_ unsigned long* const restrict pdwRespSize
) {
    // if the call to http_get() failed,
    if (!handles.session || !handles.connection || !handles.request) [[unlikely]] {
        fputws(L"read_http_response failed! (Errors in previous call to http_get)\n", stderr);
        return NULL;
    }

    // NOLINTNEXTLINE(readability-isolate-declaration)
    unsigned long  total_bytes_read = 0, bytes_in_current_query = 0, bytes_read_from_current_query = 0;
    bool           is_failure      = false;
    unsigned char* buffer          = NULL;

    // NOLINTNEXTLINE(readability-isolate-declaration) - unpack the handles for convenience
    const HINTERNET session_handle = handles.session, connection_handle = handles.connection, request_handle = handles.request;

    const bool is_response_received = WinHttpReceiveResponse(request_handle, NULL); // NOLINT(readability-implicit-bool-conversion)
    if (!is_response_received) [[unlikely]] {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        is_failure = true;
        goto PREMATURE_RETURN;
    }

    // calling malloc first and then calling realloc in a do while loop is terribly inefficient for a simple app sending a single GET request.
    // we'll malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write can start from where the last write terminated
    buffer = malloc(HTTP_RESPONSE_SIZE);
    if (!buffer) [[unlikely]] {
        fputws(L"Memory allocation error in read_http_response!\n", stderr);
        is_failure = true;
        goto PREMATURE_RETURN;
    }

    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

    do {
        // for every iteration, zero these counters since these are specific to each query.
        bytes_in_current_query = bytes_read_from_current_query = 0;

        if (!WinHttpQueryDataAvailable(request_handle, &bytes_in_current_query)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpQueryDataAvailable.\n", GetLastError());
            break;
        }

        if (!bytes_in_current_query) break; // if there aren't any more bytes to read,

        if (!WinHttpReadData(request_handle, buffer + total_bytes_read, bytes_in_current_query, &bytes_read_from_current_query)) {
            fwprintf_s(stderr, L"Error %lu in WinHttpReadData.\n", GetLastError());
            break;
        }

        total_bytes_read += bytes_read_from_current_query;

        if (total_bytes_read >= (HTTP_RESPONSE_SIZE - 128U)) [[unlikely]] {
            fputws(L"Warning: Truncation of response due to insufficient memory!\n", stderr);
            break;
        }

#ifdef _DEBUG
        wprintf_s(L"This query collected %lu bytes from the response\n", bytes_in_current_query);
#endif // _DEBUG

    } while (bytes_in_current_query > 0); // while there's still data in the response,

PREMATURE_RETURN:
    // using regular CloseHandle() to close HINTERNET handles will (did) crash the debug session.
    WinHttpCloseHandle(session_handle);
    WinHttpCloseHandle(connection_handle);
    WinHttpCloseHandle(request_handle);
    *pdwRespSize = total_bytes_read;
    return is_failure ? NULL : buffer;
}

[[nodiscard("entails expensive http io")]] char* read_http_response_ex(
    _In_ const hinternet_triple_t handles, _Inout_ unsigned long* const restrict response_size
) {
    if (!handles.session || !handles.connection || !handles.request) {
        fputws(L"read_http_response_ex failed! (Errors in previous call to http_get)\n", stderr);
        return NULL;
    }

    unsigned long total_bytes_read = 0;
    bool          is_failure       = false;
    char*         buffer           = NULL;

    // NOLINTNEXTLINE(readability-isolate-declaration) - unpack the handles for convenience
    const HINTERNET session_handle = handles.session, connection_handle = handles.connection, request_handle = handles.request;

    const bool is_response_received = WinHttpReceiveResponse(request_handle, NULL); // NOLINT(readability-implicit-bool-conversion)
    if (!is_response_received) {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        is_failure = true;
        goto PREMATURE_RETURN;
    }

    // calling malloc first and then calling realloc in a do while loop is terribly inefficient for a simple app sending a single GET request.
    // we'll malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write can start from where the last write terminated
    buffer = malloc(HTTP_RESPONSE_SIZE);
    if (!buffer) {
        fputws(L"Memory allocation error in read_http_response_ex!\n", stderr);
        is_failure = true;
        goto PREMATURE_RETURN;
    }
    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

    const unsigned long response_read_status = // will be 0 if the call succeeds
        WinHttpReadDataEx(request_handle, buffer, HTTP_RESPONSE_SIZE, &total_bytes_read, WINHTTP_READ_DATA_EX_FLAG_FILL_BUFFER, 0, NULL);
    // WINHTTP_READ_DATA_EX_FLAG_FILL_BUFFER will condition the WinHttpReadDataEx to return only after all the bytes in the response have been collected in the buffer
    // without this we'd have to read the response in chunks using a loop, checking every time for bytes remaining

    if (response_read_status) { // dwReadStatus != 0
        fwprintf_s(stderr, L"Error %lu in WinHttpReadDataEx.\n", GetLastError());
        free(buffer);
        is_failure = true;
    }

PREMATURE_RETURN:
    // using regular CloseHandle() to close HINTERNET handles will (did) crash the debug session.
    WinHttpCloseHandle(session_handle);
    WinHttpCloseHandle(connection_handle);
    WinHttpCloseHandle(request_handle);

    *response_size = total_bytes_read;
    return is_failure ? NULL : buffer;
}
