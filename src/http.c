#include <pyreleases.h>

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

    // as of 24/05/2024 www.python.org/downloads/windows/ sends a gzipped file in response to a GET request
    // this used to be a regular text (HTML) file. so, to decompress and get the text output there are two options!
    // 1) specify WINHTTP_DECOMPRESSION_FLAG_ALL in WinHttp, so the library will handle DEFLATE/gzip decoding internally
    // 2) get the raw compressed bytes and do the decoding by hand
    // I'm opting for the first option :p
    // https://learn.microsoft.com/en-us/windows/win32/wininet/content-encoding
    const BOOL bEnableDecoding   = TRUE;
    // DO NOT MIX WININET AND WINHTTP FUNCTIONS, THEY DO NOT WORK IN HARMONY
    const BOOL bSetWinHttpDecode = WinHttpSetOption(hSession, WINHTTP_DECOMPRESSION_FLAG_ALL, &bEnableDecoding, sizeof(BOOL));
    if (!bSetWinHttpDecode) {
        fwprintf_s(stderr, L"Error %lu in the InternetSetOptionW, DEFLATE/gzip decompression request failed!\n", GetLastError());
        goto CLOSE_SESSION_HANDLE;
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
    const BOOL bStatus = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, // pointer to a string that contains the additional headers to append to the request.
        0,                             // an unsigned long integer value that contains the length, in characters, of the additional headers.
        WINHTTP_NO_REQUEST_DATA,       // pointer to a buffer that contains any optional data to send immediately after the request headers
        0,                             // an unsigned long integer value that contains the length, in bytes, of the optional data.
        0,                             // an unsigned long integer value that contains the length, in bytes, of the total data sent.
        0
    ); // a pointer to a pointer-sized variable that contains an application-defined value that is passed, with the request handle, to
        // any callback functions.

    if (!bStatus) {
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
                    hRequest = handles.request; // unpack the handles for convenience
    // NOLINTEND(readability-isolate-declaration)

    const BOOL bIsReceived   = WinHttpReceiveResponse(hRequest, NULL);
    if (!bIsReceived) {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        goto PREMATURE_RETURN;
    }

    // calling malloc first and then calling realloc in a do while loop is terribly inefficient for a simple app sending a single GET request.
    // we'll malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write can start from where the last write terminated
    char* const restrict buffer = malloc(HTTP_RESPONSE_SIZE);
    if (!buffer) {
        fputws(L"Memory allocation error in ReadHttpResponse!\n", stderr);
        goto PREMATURE_RETURN;
    }
    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

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

PREMATURE_RETURN:

    return NULL;
}

char* ReadHttpResponseEx(_In_ const hint3_t handles, _Inout_ uint64_t* const restrict response_size) {
    // if the call to HttpGet() failed,
    if (handles.session == NULL || handles.connection == NULL || handles.request == NULL) {
        fputws(L"ReadHttpResponseEx failed! (Errors in previous call to HttpGet)\n", stderr);
        return NULL;
    }

    // NOLINTBEGIN(readability-isolate-declaration)
    const HINTERNET hSession = handles.session, hConnection = handles.connection,
                    hRequest = handles.request; // unpack the handles for convenience
                                                // NOLINTEND(readability-isolate-declaration)

    // calling malloc first and then calling realloc in a do while loop is terribly inefficient for a simple app sending a single GET request.
    // we'll malloc all the needed memory beforehand and use a moving pointer to keep track of the
    // last write offset, so the next write can start from where the last write terminated

    const BOOL bIsReceived   = WinHttpReceiveResponse(hRequest, NULL);
    if (!bIsReceived) {
        fwprintf_s(stderr, L"Error %lu in WinHttpReceiveResponse.\n", GetLastError());
        return NULL;
    }

    char* const restrict buffer = malloc(HTTP_RESPONSE_SIZE);
    if (!buffer) {
        fputws(L"Memory allocation error in ReadHttpResponseEx!\n", stderr);
        return NULL;
    }
    memset(buffer, 0U, HTTP_RESPONSE_SIZE); // zero out the buffer.

    DWORD dwTotalBytesRead = 0;
    WinHttpReadDataEx(hRequest, buffer, HTTP_RESPONSE_SIZE, &dwTotalBytesRead, WINHTTP_READ_DATA_EX_FLAG_FILL_BUFFER, 0, NULL);

    // using regular CloseHandle() to close HINTERNET handles will (did) crash the debug session.
    WinHttpCloseHandle(hSession);
    WinHttpCloseHandle(hConnection);
    WinHttpCloseHandle(hRequest);

    *response_size = dwTotalBytesRead;
    return buffer;
}