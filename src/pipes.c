#include <project.h>

// https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output?redirectedfrom=MSDN
// pipes are used in one-way communication between two different processes.
// we will need two pipes connecting the two processes, these pipes can be named (optionally)

// parent process ------->  child process      (pipe 1)
// child process  ------->  parent process     (pipe 2)

// pipes are unidirectional, information can be relayed in one direction only, hence the need for two pipes to facilitate a bidirectional communication

// read end (right) of the pipe 1 serves as the stdin for the child process and the write end (left) serves as the stdout for the parent process.
// read end of pipe 2 serves as the stdin for the parent process and the write end serves as the stdout for thr child process.

// terms parent process & child process are used here because the second process (python.exe) is spawned by the first process

// the read end of first pipe serves as stdin for the child process and the write end serves as the stdout for the parent process
// and the write end of second pipe is the stdout for the child process and the read end is the stdin for the parent process
// these pipe handles are specified in the STARTUPINFO structure, which makes them the standard handles inherited by the child process

// parent process uses the opposite ends of these two pipes to write to the child process's input and read from the child process's output.
// as specified in the SECURITY_ATTRIBUTES structure, these handles are also inheritable, but Microsoft clarifies these handles must not be inherited.
// thus, before creating the child process, the parent process uses the SetHandleInformation() function to ensure that the write handle for the child process's stdout
// and the read handle for the child process's stdout cannot be inherited.

// here we are only interested in a one-way communication, we just need this application to read python.exe's stdout through the pipe
static HANDLE64 this_process_stdin_handle = NULL, python_stdout_handle = NULL; //

// launches python.exe that uses the previously created pipe as stdin & stderr
bool launch_python(void) {
    PROCESS_INFORMATION python_proc_info    = { 0 };
    const STARTUPINFOW  python_startup_info = {
         .cb          = sizeof(STARTUPINFO),
         .hStdError   = python_stdout_handle,
         .hStdOutput  = python_stdout_handle,
         .dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES,
         .wShowWindow = SW_HIDE // prevents cmd window from flashing, requires STARTF_USESHOWWINDOW in dwFlags.
    };

    // lookup: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
    // passing the executable's name in lpApplicationName and its arguments in lpCommandLine causes error 2. "The system cannot find the file specified"
    // pass the whole command (executable name + arguments) in lpCommandLine.

    // CreateProcessW, can modify the contents of lpCommandLine
    // therefore, this parameter cannot be a pointer to read-only memory (such as a const wchar* or a string literal).
    // if this parameter such, an access violation will be raised

#if defined(_DEBUG) || defined(DEBUG)
    wchar_t command[BUFF_SIZE] = L"./python/bin/Debug/python.exe --version";
#else
    wchar_t command[BUFF_SIZE] = L"python.exe --version";
#endif

    // the lpApplicationName parameter can be NULL. In that case, the module name must be the first white
    // space delimited token in the lpCommandLine string. If you are using a long file name that contains
    // a space, use quoted strings to indicate where the file name ends and the arguments begin; otherwise,
    // the file name is ambiguous.

    const bool is_created = CreateProcessW( // NOLINT(readability-implicit-bool-conversion)
        NULL,                               // assumes python.exe is in path.
        command, // lpCommandline must be a modifiable string, passing a string literal will (did) raise an access violation exception
        NULL,
        NULL,
        true,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &python_startup_info,
        &python_proc_info
    );

    if (!is_created) { // if the invocation failed
        fwprintf_s(stderr, L"Error %lu in CreateProcessW.\n", GetLastError());
        return false;
    }

    // wait 100 milliseconds until python.exe finishes.
    const unsigned long wait_status = WaitForSingleObject(python_proc_info.hProcess, EXECUTION_TIMEOUT);
    switch (wait_status) {
        case WAIT_ABANDONED :
            fputws(L"Mutex object was not released by the child thread before the caller thread terminated.\n", stderr);
            break;
        case WAIT_TIMEOUT : fputws(L"The time-out interval has elapsed, and the object's state is nonsignaled.\n", stderr); break;
        case WAIT_FAILED :
            fwprintf_s(stderr, L"Error %lu: Wait failed.\n", GetLastError());
            break;
            // WAIT_OBJECT_0 0x00000000L -> The state of the specified object is signaled, wait success
        default : break;
    }

    // close handles to the child process and its primary thread.
    CloseHandle(python_proc_info.hProcess);
    CloseHandle(python_proc_info.hThread);

    return true;
}

// reads python.exe's stdout and writes it to the buffer.
bool ReadStdoutPythonExe(_Inout_ PSTR const restrict pszBuffer, _In_ const unsigned long dwSize) {
    // if size(stdout) > size(buffer), write will be truncated

    unsigned long dwReadBytes     = 0;
    const bool    bPipeReadStatus = ReadFile(this_process_stdin_handle, pszBuffer, dwSize, &dwReadBytes, NULL);

    if (!bPipeReadStatus) fwprintf_s(stderr, L"Error %lu in ReadFile.\n", GetLastError());

    // close the child's ends of the pipe.
    CloseHandle(python_stdout_handle);
    return bPipeReadStatus;
}

bool GetSystemPythonExeVersion(_Inout_ PSTR const restrict pszVersion, _In_ const unsigned long dwSize) {
    // a struct to specify the security attributes of the pipes.
    // .bInheritHandle = true makes pipe handles inheritable.
    const SECURITY_ATTRIBUTES saSecurityAttrs = { .bInheritHandle       = true,
                                                  .lpSecurityDescriptor = NULL,
                                                  .nLength              = sizeof(SECURITY_ATTRIBUTES) };

    // creating child process ------> parent process pipe.
    if (!CreatePipe(&this_process_stdin_handle, &python_stdout_handle, &saSecurityAttrs, 0)) {
        fwprintf_s(stderr, L"Error %lu in CreatePipe.\n", GetLastError());
        return false;
    }

    // make the parent process's handles uninheritable.
    if (!SetHandleInformation(this_process_stdin_handle, HANDLE_FLAG_INHERIT, false)) {
        fwprintf_s(stderr, L"Error %lu in SetHandleInformation.\n", GetLastError());
        return false;
    }

    const bool bLaunchStatus = launch_python();
    if (!bLaunchStatus) {
        fwprintf_s(stderr, L"Error %lu in launch_python.\n", GetLastError());
        return false;
    }

    const bool bReadStatus = ReadStdoutPythonExe(pszVersion, dwSize);
    if (!bReadStatus) {
        fwprintf_s(stderr, L"Error %lu in ReadStdoutPythonExe.\n", GetLastError());
        return false;
    }

    return true;
}
