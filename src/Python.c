#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// A dummy python_t.exe to test py-releases.exe

#ifdef __TEST__

int main(int argc, char* argv[]) {

    srand(time(NULL));
    int minor = 0, major = 0;

    minor = (rand() % 5) + 5;
    major = (rand() % 9) + 1;

    if (!strcmp(argv[1], "--version")) {
        printf_s("python_t 3.%d.%d\n", minor, major);
    }
    return 0;
}

#endif // __TEST__