// dummy python.exe to test py-releases.exe

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
        const int major = (rand() % 5) + 1;
        const int minor = (rand() % 13) + 1;

        wprintf_s(L"Python 3.%d.%d\n", major, minor);
        return EXIT_SUCCESS;
    }

    fputws(L"This is a dummy python.exe that can only respond when invoked with a single argument \"--version\"\n", stderr);
    return EXIT_FAILURE;
}

#endif
