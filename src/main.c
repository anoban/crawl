#include <pyreleases.h>

// Lookup https://learn.microsoft.com/en-us/previous-versions/hf4y5e3w(v=vs.140)?redirectedfrom=MSDN

int wmain(void) {
    if (!ActivateVirtualTerminalEscapes())
        fputws(L"Win32 Virtual Terminal Escape sequences are not enabled! Programme output will fall back to black and white!", stderr);

    const wchar_t* const restrict SERVER       = L"www.python.org";
    const wchar_t* const restrict ACCESS_POINT = L"/downloads/windows/";
    const hint3_t handles                      = HttpGet(SERVER, ACCESS_POINT);
    uint64_t      response_size                = 0;

    // ReadHttpResponse will handle if handles are NULLs, no need for external error handling here.
    char* const restrict html_text             = ReadHttpResponse(handles, &response_size);
    puts(html_text);

    // LocateStableReleasesDiv will handle NULL returns from ReadHttpResponse internally,
    // so again no need for main to handle errors explicitly.
    // in case of a NULL input, returned range will be {0, 0}.
    const range_t stable = LocateStableReleasesDiv(html_text, HTTP_RESPONSE_SIZE);

    if ((!stable.begin) && (!stable.end)) {
        fputws(L"Error: Call to LocateStableReleasesDiv failed!", stderr);
        goto cleanup;
    }

    // zero out the buffer downstream the end of stable releases, (i.e pre releases)
    memset(html_text + stable.end, 0U, response_size - stable.end);

    results_t parsed = ParseStableReleases(html_text + stable.begin, stable.end - stable.begin);

    // may happen due to malloc failures or invalid inputs.
    if (!parsed.begin) {
        fputws(L"Error: deserialize_stable_releases returned a NULL buffer.\n", stderr);
        goto cleanup;
    }

    char const python_version[BUFF_SIZE] = { 0 };
    if (!GetSystemPythonExeVersion(python_version, BUFF_SIZE)) fputws(L"Error: Call to GetSystemPythonVersion failed!", stderr);

#ifdef _DEBUG
    wprintf_s(
        L"%llu python releases have been deserialized.\n"
        L"Installed python version is %S\n",
        parsed.count,
        python_version
    );
#endif // _DEBUG

    // PrintReleases will handle empty instances of py_version internally.
    PrintReleases(parsed, python_version);

    free(html_text);
    free(parsed.begin);
    return EXIT_SUCCESS;

cleanup:
    free(html_text);
    return EXIT_FAILURE;
}
