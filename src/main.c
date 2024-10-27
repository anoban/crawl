#include <project.h>

int wmain(void) {
    unsigned long response_size          = 0;
    wchar_t       server[BUFF_SIZE]      = L"www.python.org";
    wchar_t       accesspoint[BUFF_SIZE] = L"/downloads/windows/";

    const hinternet_triple_t handles     = http_get(server, accesspoint);

    // read_http_response or read_http_response_ex will handle if handles are NULLs, no need for external error handling here.
    const char* const restrict html_text = read_http_response_ex(handles, &response_size);

    // locate_stable_releases_htmldiv will handle NULL returns from read_http_response internally,
    // so again no need for main to handle errors explicitly.
    // in case of a NULL input, returned range will be {0, 0}.
    const range_t stable_releases        = locate_stable_releases_htmldiv(html_text, HTTP_RESPONSE_SIZE); // works correctly :)

    if (!stable_releases.begin && !stable_releases.end) {
        fputws(L"Error: Call to locate_stable_releases_htmldiv failed!\n", stderr);
        // __serialize(pszHtmlText, response_size, L"./response.gzip");
        goto CLEANUP;
    }

    const results_t parsed_results = parse_stable_releases(html_text + stable_releases.begin, stable_releases.end - stable_releases.begin);

    // may happen due to malloc failures or invalid inputs.
    if (!parsed_results.begin) {
        fputws(L"Error: Call to parse_stable_releases failed!\n", stderr);
        goto CLEANUP;
    }

    char syspy[BUFF_SIZE] = { 0 }; // system python
    if (!get_system_python_version(syspy, BUFF_SIZE)) fputws(L"Error: Call to get_system_python_version failed!\n", stderr);

    // print will handle empty instances of syspy internally.
    print(parsed_results, syspy);

    free(html_text);
    free(parsed_results.begin);
    return EXIT_SUCCESS;

CLEANUP:
    free(html_text);
    return EXIT_FAILURE;
}
