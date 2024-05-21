// dummy python.exe to test py-releases.exe

#if defined(_DEBUG) || defined(DEBUG) // do not build this project in release mode

    #include <assert.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <time.h>

int wmain(_In_opt_ int argc, _In_opt_ wchar_t* argv[]) {
    assert(argc == 2);
    srand(time(NULL));
    const int minor = (rand() % 5) + 1;
    const int major = (rand() % 12) + 1;

    if (!wcsncmp(argv[1], L"--version", 10LLU)) {
        wprintf_s(L"Python 3.%d.%d\n", major, minor);
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

#endif // defined(_DEBUG) || defined(DEBUG)