#include <pyreleases.h>

/*
 * https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output?redirectedfrom=MSDN
 * Pipes are used in communication between two different processes.
 * There will be two pipes between two communicating processes.
 * These pipes can be named or anonymous.
 * These two pipes can be visualized like this:
 *
 * Parent process ------->  Child process      (pipe 1)
 * Child process  ------->  Parent process     (pipe 2)
 *
 * Pipes are unidirectional, information can be relayed in one direction only.
 * This is why we need two pipes for a bidirectional communication.
 *
 * Read end (right) of the pipe 1 serves as the stdin for the child process
 * and the write end (left) serves as the stdout for the parent process.
 *
 * The read end of pipe 2 serves as the stdin for the parent process
 * and the write end serves as the stdout for thr child process.
 *
 * The terms Parent & Child are used here because the second process is created by the first process!.
 *
 *
 * The read end of one pipe serves as standard input for the child process,
 * and the write end of the other pipe is the standard output for the child process.
 * These pipe handles are specified in the STARTUPINFO structure,
 * which makes them the standard handles inherited by the child process.
 *
 * The parent process uses the opposite ends of these two pipes to write to the child process's input
 * and read from the child process's output.
 * As specified in the SECURITY_ATTRIBUTES structure, these handles are also inheritable.
 * However, these handles must not be inherited. Therefore, before creating the child process,
 * the parent process uses the SetHandleInformation() function to ensure that the write handle for the child process's standard input
 * and the read handle for the child process's standard output cannot be inherited.
 *
 */

/*------------------------  Globals  ---------------------------------*/
// Note that here we are only interested in a oneway communication.
// We need the launcher process to read the python_t's stdout.

// Parent process
HANDLE this_process_stdin_handle   = NULL;

// Child process
HANDLE child_process_stdout_handle = NULL;

// this_process_stdin_handle and child_process_stdout_handle form the two ends of one pipe.
// hThisProcessStdout and hChildProcessStdin form the two ends of the other pipe.

bool LaunchPythonExe(void) {
    // Create a child process that uses the previously created pipes as stdin & stderr
    PROCESS_INFORMATION child_proc_info;
    STARTUPINFOW        child_proc_startup_info;
    uint32_t            wait_status;
    bool                proc_creation_status = false; // mark whether the process creation succeeds or not.

    // Zero out the above structs.
    memset(&child_proc_info, 0U, sizeof(PROCESS_INFORMATION));
    memset(&child_proc_startup_info, 0U, sizeof(STARTUPINFO));

    child_proc_startup_info.cb        = sizeof(STARTUPINFO);
    child_proc_startup_info.hStdError = child_proc_startup_info.hStdOutput = child_process_stdout_handle;
    child_proc_startup_info.dwFlags                                        = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    child_proc_startup_info.wShowWindow = SW_HIDE; // Prevents cmd window from flashing. Requires STARTF_USESHOWWINDOW in dwFlags.

    // Lookup: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
    // Passing the .exe's name in lpApplicationName causes error 2. "The system cannot find the file specified"
    // Pass the whole string to the lpCommandLine.

    // The Unicode version of this function, CreateProcessW, can modify the contents of this string.
    // Therefore, this parameter cannot be a pointer to read-only memory (such as a const variable or
    // a literal string). If this parameter is a constant string, the function may cause an access
    // violation.

#ifndef _DEBUG
    wchar_t invoke_command[100] = L"python.exe --version";
#elif defined _DEBUG
    // py-releases.exe is invoked from the solution's root directory,
    // so this path needs to be relative to the solution's root directory.
    wchar_t invoke_command[100] = L"./python/x64/Debug/python.exe --version";
#endif // !_DEBUG

    // The lpApplicationName parameter can be NULL. In that case, the module name must be the first white
    // space�delimited token in the lpCommandLine string. If you are using a long file name that contains
    // a space, use quoted strings to indicate where the file name ends and the arguments begin; otherwise,
    // the file name is ambiguous.

    proc_creation_status = CreateProcessW(
        NULL, // assumes python.exe is in path.
              // lpCommandline must be a modifiable string (wchar_t array)
              // Passing a constant string will raise an access violation exception.
        invoke_command,
        NULL,
        NULL,
        true,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &child_proc_startup_info,
        &child_proc_info
    );

    // If invocation failed,
    if (!proc_creation_status) {
        fwprintf_s(stderr, L"Error %lu in CreateProcessW.\n", GetLastError());
        return false;
    }

    // Wait 100 milliseconds until python_t.exe finishes.
    wait_status = WaitForSingleObject(child_proc_info.hProcess, 100U);
    switch (wait_status) {
        case (WAIT_ABANDONED) :
            fputws(L"Mutex object was not released by the child thread before the caller thread terminated.\n", stderr);
            break;
        case (WAIT_TIMEOUT) : fputws(L"The time-out interval elapsed, and the object's state is nonsignaled.\n", stderr); break;
        case (WAIT_FAILED) :
            fwprintf_s(stderr, L"Error %lu: Wait failed.\n", GetLastError());
            break;
            // WAIT_OBJECT_0 0x00000000L -> The state of the specified object is signaled.
        default : break;
    }

    // Close handles to the child process and its primary thread.
    CloseHandle(child_proc_info.hProcess);
    CloseHandle(child_proc_info.hThread);

    return true;
}

bool ReadStdoutPythonExe(_Inout_ char* const restrict write_buffer, _In_ const uint64_t buffsize) {
    // Reads python_t.exe's stdout and writes it to the buffer.
    // If size(stdout) > size(buffer), write will happen until there's no space in the buffer.

    uint64_t nbytes_read      = 0;
    bool     pipe_read_status = ReadFile(this_process_stdin_handle, write_buffer, buffsize, &nbytes_read, NULL);

    if (!pipe_read_status) fwprintf_s(stderr, L"Error %lu in ReadFile.\n", GetLastError());

    // Close the child's ends of the pipe.
    CloseHandle(child_process_stdout_handle);

    return pipe_read_status;
}

bool GetSystemPythonExeVersion(_Inout_ char* const restrict version_buffer, _In_ const uint64_t buffsize) {
    // A struct to specify the security attributes of the pipes.
    // .bInheritHandle = true makes pipe handles inheritable.
    SECURITY_ATTRIBUTES pipe_security_attrs = { .bInheritHandle       = true,
                                                .lpSecurityDescriptor = NULL,
                                                .nLength              = sizeof(SECURITY_ATTRIBUTES) };

    // Creating Child process ------> Parent process pipe.
    if (!CreatePipe(&this_process_stdin_handle, &child_process_stdout_handle, &pipe_security_attrs, 0)) {
        fwprintf_s(stderr, L"Error %lu in CreatePipe.\n", GetLastError());
        return false;
    }

    // Make the parent process's handles uninheritable.
    if (!SetHandleInformation(this_process_stdin_handle, HANDLE_FLAG_INHERIT, 0U)) {
        fwprintf_s(stderr, L"Error %lu in SetHandleInformation.\n", GetLastError());
        return false;
    }

    bool launch_status = LaunchPythonExe(), read_status = false;

    if (launch_status) {
        read_status = ReadStdoutPythonExe(version_buffer, buffsize);
        if (!read_status) {
            fwprintf_s(stderr, L"Error %lu in read_pythons_stdout.\n", GetLastError());
            return false;
        }
    } else {
        fwprintf_s(stderr, L"Error %lu in launch_python.\n", GetLastError());
        return false;
    }

    return true;
}
