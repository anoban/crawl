// dummy python.exe to test crawl.exe

#if defined(_DEBUG) || defined(DEBUG) // do not build this project in release mode

    #include <assert.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <time.h>

int wmain(_In_opt_ int argc, _In_opt_ wchar_t* argv[]) {
    assert(argc == 2); // this project will only build in debug mode, so assert will always work :)

    if (!wcsncmp(argv[1], L"--version", 10LLU)) {
        srand((unsigned) time(NULL));
        const int major = (rand() % 10) + 6;
        const int minor = (rand() % 14);

        fwprintf_s(stdout, L"Python 3.%d.%d\n", major, minor); // this won't work always :(
        return EXIT_SUCCESS;
    }

    fputws(L"This is a dummy python.exe that can only respond when invoked with a single argument \"--version\"\n", stderr);
    return EXIT_FAILURE;
}

#endif
