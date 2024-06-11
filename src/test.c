#ifdef __TEST__
    #include <pyreleases.h>

HANDLE64 hProcHeap; // handle to the current process ((HANDLE) -1)

int wmain(void) {
    hProcHeap = GetProcessHeap();

    #pragma region __TEST_LIB__
    assert(ActivateVirtualTerminalEscapes());

    DWORD dwFilesize                                 = 0;
    const unsigned char* const restrict pszPyWindows = Open(L"./Python Releases for Windows.txt", &dwFilesize);
    assert(pszPyWindows);
    assert(dwFilesize == 481268);

    const RANGE rStableReleases = LocateStableReleasesDiv(pszPyWindows, HTTP_RESPONSE_SIZE);
    assert(rStableReleases.dwBegin == 22447 && rStableReleases.dwEnd == 246093);

    const RESULTS reParsed = ParseStableReleases(pszPyWindows + rStableReleases.dwBegin, rStableReleases.dwEnd - rStableReleases.dwBegin);
    // assert(!reParsed.begin && (reParsed.dwCapacity > 0) && (reParsed.dwCount > 0));
    #pragma endregion __TEST_LIB__

    #pragma region __TEST_PIPES__
    CHAR           pszSystemPython[BUFF_SIZE] = { 0 };
    assert(!GetSystemPythonExeVersion(pszSystemPython, BUFF_SIZE));
    #pragma endregion __TEST_PIPES__

    // PrintReleases will handle empty instances of pszSystemPython internally.
    PrintReleases(reParsed, pszSystemPython);

    free(pszPyWindows);
    free(reParsed.begin);

    #pragma region __TEST_HTTP__

    #pragma endregion __TEST_HTTP__

    return EXIT_SUCCESS;
}

#endif // __TEST__
