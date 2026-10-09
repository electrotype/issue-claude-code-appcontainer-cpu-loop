// acrun.c - run a program inside a Windows AppContainer (no capabilities = no network).
// Build (VS Developer Prompt):  cl /nologo acrun.c userenv.lib
// Usage:                        acrun.exe <program.exe> [args...]
// The program inherits the console, current directory and environment.
#include <windows.h>
#include <userenv.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv) {
  if (argc < 2) {
    fwprintf(stderr, L"usage: acrun.exe <program.exe> [args...]\n");
    return 2;
  }
  const wchar_t *name = L"acrun.repro";
  PSID sid = NULL;
  HRESULT hr = CreateAppContainerProfile(name, name, name, NULL, 0, &sid);
  if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) hr = DeriveAppContainerSidFromAppContainerName(name, &sid);
  if (FAILED(hr)) {
    fwprintf(stderr, L"AppContainer profile: 0x%08lx\n", (unsigned long)hr);
    return 1;
  }

  SECURITY_CAPABILITIES caps = {0};
  caps.AppContainerSid = sid;
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &size);
  LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
  InitializeProcThreadAttributeList(attrs, 1, 0, &size);
  UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps, sizeof(caps), NULL, NULL);

  // Command line: the arguments after acrun.exe, each quoted.
  size_t len = 1;
  for (int i = 1; i < argc; i++) len += wcslen(argv[i]) + 3;
  wchar_t *cmd = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(wchar_t));
  for (int i = 1; i < argc; i++) {
    wcscat_s(cmd, len, L"\"");
    wcscat_s(cmd, len, argv[i]);
    wcscat_s(cmd, len, i + 1 < argc ? L"\" " : L"\"");
  }

  STARTUPINFOEXW si = {0};
  si.StartupInfo.cb = sizeof(si);
  si.lpAttributeList = attrs;
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(argv[1], cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                      &si.StartupInfo, &pi)) {
    fwprintf(stderr, L"CreateProcess: %lu\n", GetLastError());
    return 1;
  }
  // Ctrl+C belongs to the child.
  SetConsoleCtrlHandler(NULL, TRUE);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  return (int)code;
}
