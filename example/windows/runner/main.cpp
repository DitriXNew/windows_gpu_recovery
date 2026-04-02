#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>
#include <cstdio>

#include "flutter_window.h"
#include "utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Redirect stderr to a log file next to the exe.
  // [GPU_RECOVERY] messages and ANGLE errors go here.
  wchar_t log_path[MAX_PATH];
  GetModuleFileNameW(nullptr, log_path, MAX_PATH);
  wchar_t* slash = wcsrchr(log_path, L'\\');
  if (slash) *(slash + 1) = L'\0';
  wcscat_s(log_path, L"gpu_recovery.log");
  FILE* dummy;
  _wfreopen_s(&dummy, log_path, L"w", stderr);

  fprintf(stderr, "[LOG] Application started\n");
  fflush(stderr);

  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 720);
  if (!window.Create(L"example_with_recovery", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
