#include <pyreleases.h>

// Lookup https://learn.microsoft.com/en-us/previous-versions/hf4y5e3w(v=vs.140)?redirectedfrom=MSDN
// for mixing char types in ucrt io fucntions.

int wmain(void) {
    if (!ActivateVirtualTerminalEscapes()) fputws(L"Activation of VTEs failed.", stderr);

    const wchar_t* const SERVER        = L"www.python.org";
    const wchar_t* const ACCESS_POINT  = L"/downloads/windows/";

    // #ifdef _DEBUG
    // 	wchar_t cwd[200] = { 0 };
    // 	GetCurrentDirectoryW(200U, &cwd);
    // 	_putws(cwd);
    // #endif // _DEBUG

    const hint3_t        scr_struct    = HttpGet(SERVER, ACCESS_POINT);

    uint64_t             resp_size     = 0;

    // read_http_response will handle if scr_handles are NULLs.
    // no need for external error handling here.
    char* const          html_content  = ReadHttpResponse(scr_struct, &resp_size);

    // get_stable_releases_offset_range will handle NULL returns from read_http_response internally,
    // so again no need for main to handle errors explicitly.
    // in case of a NULL input, returned range will be {0, 0}.
    range_t              stable_ranges = get_stable_releases_offset_range(html_content, HTTP_RESPONSE_SIZE);

    if ((!stable_ranges.start) && (!stable_ranges.end)) {
        fwprintf_s(stderr, L"Error: get_stable_releases_offset_range returned {0, 0}.\n");
        goto cleanup;
    }

    // zero out the buffer downstream the end of stable releases, (i.e pre releases)
    memset(html_content + stable_ranges.end, 0U, resp_size - stable_ranges.end);

    results_t parse_results = deserialize_stable_releases(html_content + stable_ranges.start, stable_ranges.end - stable_ranges.start);

    // may happen due to malloc failures or invalid inputs.
    if (!parse_results.py_start) {
        fputws(L"Error: deserialize_stable_releases returned a NULL buffer.\n", stderr);
        goto cleanup;
    }

    char const py_version[BUFF_SIZE] = { 0 };
    if (!GetSystemPythonExeVersion(py_version, BUFF_SIZE)) fputws(L"Error: get_installed_python_version returned false.\n", stderr);

#ifdef _DEBUG
    wprintf_s(
        L"%llu python releases have been deserialized.\n"
        L"Installed python version is %S\n",
        parse_results.parsed_struct_count,
        py_version
    );
#endif // _DEBUG

    // print_python_releases will handle empty instances of py_version internally.
    PrintReleases(parse_results, py_version);

    free(html_content);
    free(parse_results.py_start);
    return 0;

cleanup:
    free(html_content);
    return 1;
}