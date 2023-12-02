#ifdef _DEBUG

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <time.h>

// A dummy python.exe to test py-releases.exe

int main(int argc, char* argv[]) {
    srand(time(NULL));
    int minor = 0, major = 0;

    major = (rand() % 5) + 1;
    minor = (rand() % 12) + 1;

    if (!strcmp(argv[1], "--version")) printf_s("Python 3.%d.%d\n", major, minor);

    return 0;
}

#endif // _DEBUG
