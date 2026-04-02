#include "flutter_window.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <optional>
#include <cstdio>
#include <shlobj.h>

#include "flutter/generated_plugin_registrant.h"
#include <windows_gpu_recovery/gpu_recovery_message.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

/// Writes a marker file next to the exe so the new Dart VM knows
/// it was restarted due to GPU recovery.
static void SetRecoveryFlag() {
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  wchar_t* last_slash = wcsrchr(exe_path, L'\\');
  if (last_slash) *(last_slash + 1) = L'\0';
  wcscat_s(exe_path, L"gpu_recovery.marker");

  FILE* f = nullptr;
  _wfopen_s(&f, exe_path, L"w");
  if (f) {
    fprintf(f, "1");
    fclose(f);
  }
}

/// Destroys the Flutter engine with VEH crash protection.
///
/// flutter_controller_.reset() calls the engine destructor which calls
/// eglTerminate → ANGLE cleanup → Renderer11::release(). During cleanup,
/// ANGLE tries to Release dead D3D COM objects which may crash with
/// ACCESS_VIOLATION. The VEH (installed by the plugin) catches these
/// and skips the faulting instructions. The key side effect: eglTerminate
/// sets Display::mInitialized = false, so the next eglInitialize does
/// a full reinitialization with a fresh D3D device.
static void DestroyEngineSafe(
    std::unique_ptr<flutter::FlutterViewController>& controller) {
  controller.reset();
}

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }
  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // ---------------------------------------------------------------
  // GPU RECOVERY — handle BEFORE Flutter's message dispatch.
  //
  // Strategy:
  //   1. Write recovery marker for new Dart VM.
  //   2. Destroy or leak old engine (Release vs Debug).
  //   3. Wait for GPU to fully recover.
  //   4. Create new FlutterViewController → fresh ANGLE/D3D/Skia.
  //   5. Re-register plugins, attach view, force first frame.
  // ---------------------------------------------------------------
  if (message == WM_GPU_RECOVERY) {
    fprintf(stderr, "[GPU_RECOVERY] WM_GPU_RECOVERY received\n");
    fflush(stderr);

    // Step 1: Recovery marker.
    SetRecoveryFlag();

    // Step 2: Destroy old engine.
    fprintf(stderr, "[GPU_RECOVERY] Destroying old engine...\n");
    fflush(stderr);
    DestroyEngineSafe(flutter_controller_);
    fprintf(stderr, "[GPU_RECOVERY] Old engine destroyed\n");
    fflush(stderr);

    // Step 3: Poll until GPU is ready instead of fixed Sleep.
    // Create a test D3D11 device every 200ms — when it succeeds
    // and GetDeviceRemovedReason() == S_OK, the GPU is healthy.
    {
      const int kMaxAttempts = 50;   // 50 * 200ms = 10 seconds max
      const DWORD kPollMs = 200;
      bool gpu_ready = false;

      for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
        Sleep(kPollMs);

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) continue;

        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(0, &adapter))) continue;

        Microsoft::WRL::ComPtr<ID3D11Device> test_device;
        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &test_device, &level, nullptr);

        if (SUCCEEDED(hr) && test_device->GetDeviceRemovedReason() == S_OK) {
          fprintf(stderr, "[GPU_RECOVERY] GPU ready after %d ms\n",
                  attempt * kPollMs);
          fflush(stderr);
          gpu_ready = true;
          break;
        }
      }

      if (!gpu_ready) {
        fprintf(stderr, "[GPU_RECOVERY] GPU not ready after 10s — attempting anyway\n");
        fflush(stderr);
      }
    }

    // Step 4: Create fresh engine.
    fprintf(stderr, "[GPU_RECOVERY] Creating new engine...\n");
    fflush(stderr);

    RECT frame = GetClientArea();
    flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
        frame.right - frame.left, frame.bottom - frame.top, project_);

    if (!flutter_controller_->engine() || !flutter_controller_->view()) {
      fprintf(stderr, "[GPU_RECOVERY] FATAL: failed to create engine\n");
      fflush(stderr);
      return 0;
    }

    // Step 5: Re-register plugins and attach view.
    RegisterPlugins(flutter_controller_->engine());
    SetChildContent(flutter_controller_->view()->GetNativeWindow());
    flutter_controller_->ForceRedraw();

    fprintf(stderr, "[GPU_RECOVERY] Engine recreated successfully\n");
    fflush(stderr);
    return 0;
  }

  // Normal Flutter message dispatch.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      if (flutter_controller_ && flutter_controller_->engine()) {
        flutter_controller_->engine()->ReloadSystemFonts();
      }
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
