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
// We need the launcher process to read the Python's stdout.

// Parent process
HANDLE hThisProcessStdin = NULL;

// Child process
HANDLE hChildProcessStdout = NULL;

// hThisProcessStdin and hChildProcessStdout form the two ends of one pipe.
// hThisProcessStdout and hChildProcessStdin form the two ends of the other pipe.


bool launch_python(void) {

    // Create a child process that uses the previously created pipes as stdin & stderr
    PROCESS_INFORMATION piChildProcInfo;
    STARTUPINFOW siChildProcStartupInfo;
    uint32_t dwWaitStatus;
    bool bSuccess = FALSE;      // mark whether the process creation succeeds or not.

    // Zero the PROCESS_INFORMATION structure.
    memset(&piChildProcInfo, 0, sizeof(PROCESS_INFORMATION));

    // Initialize STARTUPINFO structure.
    memset(&siChildProcStartupInfo, 0, sizeof(STARTUPINFO));

    siChildProcStartupInfo.cb = sizeof(STARTUPINFO);
    siChildProcStartupInfo.hStdError = siChildProcStartupInfo.hStdOutput = hChildProcessStdout;
    siChildProcStartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    siChildProcStartupInfo.wShowWindow = SW_HIDE;   // Prevents cmd window from flashing. Requires STARTF_USESHOWWINDOW in dwFlags.

    // Lookup: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
    // Passing the .exe's name in lpApplicationName causes error 2. "The system cannot find the file specified"
    // Pass the whole string to the lpCommandLine.
    wchar_t pswzInvocation[] = L"python.exe --version";
    bSuccess = CreateProcessW(NULL,     // assumes python.exe is in path.
        // lpCommandline must be a modifiable string (wchar_t array)
        // Passing a constant string will raise an access violation exception.
        pswzInvocation,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &siChildProcStartupInfo,
        &piChildProcInfo);

    // If invocation failed,
    if (!bSuccess) {
        fprintf_s(stderr, "Error %lu in CreateProcessW.\n", GetLastError());
        return FALSE;
    }

     // Wait 100 milliseconds until Python.exe finishes.
     dwWaitStatus = WaitForSingleObject(piChildProcInfo.hProcess, 100U);
     switch (dwWaitStatus) {
     case (WAIT_ABANDONED):
         fprintf_s(stderr, "Mutex object was not released by the child thread before the caller thread terminated.\n");
         break;
     case(WAIT_TIMEOUT):
         fprintf_s(stderr, "The time-out interval elapsed, and the object's state is nonsignaled.\n");
         break;
     case(WAIT_FAILED):
         fprintf_s(stderr, "Error %lu: Wait failed.\n", GetLastError());
         break;
         // WAIT_OBJECT_0 0x00000000L -> The state of the specified object is signaled.
     default:
         break;
     }

    // Close handles to the child process and its primary thread.
    CloseHandle(piChildProcInfo.hProcess);
    CloseHandle(piChildProcInfo.hThread);

    return TRUE;
}


bool ReadFromPythonsStdout(char* lpszWriteBuffer, uint32_t dwBuffSize) {

    // Reads Python.exe's stdout and writes it to the buffer.
    // If size(stdout) > size(buffer), write will happen until there's no space in the buffer.

    uint32_t dwnBytesRead = 0;
    bool bSuccess = FALSE;

    bSuccess = ReadFile(hThisProcessStdin, lpszWriteBuffer,
        dwBuffSize, &dwnBytesRead, NULL);


    // Close the child's ends of the pipe.
    CloseHandle(hChildProcessStdout);

#ifdef _DEBUG
    printf_s("%lu bytes read from Python's stdout.\n", dwnBytesRead);
    puts(lpszWriteBuffer);
#endif

    return bSuccess;
}


bool GetPythonVersion(char* lpszVersionBuffer, uint32_t dwBufferSize) {

    // A struct to specify the security attributes of the pipes.
    SECURITY_ATTRIBUTES saPipeSecAttrs;

    saPipeSecAttrs.nLength = sizeof(saPipeSecAttrs);
    // This makes pipe handles inheritable.
    saPipeSecAttrs.bInheritHandle = TRUE;
    saPipeSecAttrs.lpSecurityDescriptor = NULL;

    // Creating Child process ------> Parent process pipe.
    if (!CreatePipe(&hThisProcessStdin, &hChildProcessStdout, &saPipeSecAttrs, 0)) {
        fprintf_s(stderr, "Error %lu in creating Child -> Parent pipe.\n", GetLastError());
        return FALSE;
    }

    // Make the parent process's handles uninheritable.
    if (!SetHandleInformation(hThisProcessStdin, HANDLE_FLAG_INHERIT, 0U)) {
        fprintf_s(stderr, "Error %lu in disabling handle inheritance.\n", GetLastError());
        return FALSE;
    }

    bool bLaunchedPython = LaunchPython();
    bool bRead = FALSE;

    if (bLaunchedPython) {
        bRead = ReadFromPythonsStdout(lpszVersionBuffer, dwBufferSize);
        if (!bRead) {
            fprintf_s(stderr, "Error %lu in reading from Python.exe's stdout.\n", GetLastError());
            return FALSE;
        }
    }
    else {
        fprintf_s(stderr, "Error %lu in launching Python.exe.\n", GetLastError());
        return FALSE;
    }

    return TRUE;

}
