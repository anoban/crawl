#include <pyreleases.h>

HANDLE64 hProcHeap; // handle to the current process ((HANDLE) -1)

int wmain(void) {
    if (!__activate_win32_virtual_terminal_escapes())
        fputws(L"Win32 Virtual Terminal Escape sequences are not enabled! Programme output will fall back to black and white!\n", stderr);

    hProcHeap                              = GetProcessHeap(); // initialize the global process heap handle
    unsigned long dwRespSize                       = 0;

    WCHAR       pwszServer[BUFF_SIZE]      = L"www.python.org";
    WCHAR       pwszAccessPoint[BUFF_SIZE] = L"/downloads/windows/";
    const HINT3 hi3Handles                 = http_get(pwszServer, pwszAccessPoint);

    // ReadHttpResponse or ReadHttpResponseEx will handle if handles are NULLs, no need for external error handling here.
    const BYTE* const restrict pszHtmlText = ReadHttpResponseEx(hi3Handles, &dwRespSize);

    // LocateStableReleasesDiv will handle NULL returns from ReadHttpResponse internally,
    // so again no need for main to handle errors explicitly.
    // in case of a NULL input, returned range will be {0, 0}.
    const range_t rStableReleases            = LocateStableReleasesDiv(pszHtmlText, HTTP_RESPONSE_SIZE); // works correctly :)

    if (!rStableReleases.begin && !rStableReleases.end) {
        fputws(L"Error: Call to LocateStableReleasesDiv failed!\n", stderr);
        // Serialize(pszHtmlText, response_size, L"./response.gzip");
        goto CLEANUP;
    }

    const results_t reParsed = ParseStableReleases(pszHtmlText + rStableReleases.begin, rStableReleases.end - rStableReleases.begin);

    // may happen due to malloc failures or invalid inputs.
    if (!reParsed.begin) {
        fputws(L"Error: Call to ParseStableReleases failed!\n", stderr);
        goto CLEANUP;
    }

    CHAR pszSystemPython[BUFF_SIZE] = { 0 };
    if (!GetSystemPythonExeVersion(pszSystemPython, BUFF_SIZE)) fputws(L"Error: Call to GetSystemPythonVersion failed!\n", stderr);

    // PrintReleases will handle empty instances of pszSystemPython internally.
    PrintReleases(reParsed, pszSystemPython);

    free(pszHtmlText);
    free(reParsed.begin);
    return EXIT_SUCCESS;

CLEANUP:
    free(pszHtmlText);
    return EXIT_FAILURE;
}
