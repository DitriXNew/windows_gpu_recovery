#include "windows_gpu_recovery_plugin.h"

#include <flutter/standard_method_codec.h>
#include <psapi.h>

#include <cstdio>
#include <memory>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define GPU_LOG(msg) \
  fprintf(stderr, "[GPU_RECOVERY] " msg "\n"); fflush(stderr)
#define GPU_LOGF(fmt, ...) \
  fprintf(stderr, "[GPU_RECOVERY] " fmt "\n", __VA_ARGS__); fflush(stderr)

namespace windows_gpu_recovery {

// ---------------------------------------------------------------------------
// Vectored Exception Handler
// ---------------------------------------------------------------------------
// Catches ACCESS_VIOLATION inside flutter_windows.dll during engine
// destruction (ANGLE tries to Release dead D3D COM objects). Instead of
// crashing, we skip the faulting instruction so eglTerminate can finish
// and clear the per-process EGLDisplay singleton.

static DWORD64 g_flutter_dll_base = 0;
static DWORD64 g_flutter_dll_end = 0;
static bool g_exception_handler_active = false;
static int g_exceptions_caught = 0;

LONG CALLBACK GpuRecoveryExceptionHandler(PEXCEPTION_POINTERS info) {
  if (!g_exception_handler_active)
    return EXCEPTION_CONTINUE_SEARCH;
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    return EXCEPTION_CONTINUE_SEARCH;

  DWORD64 rip = info->ContextRecord->Rip;

  // Only handle crashes inside flutter_windows.dll.
  if (rip < g_flutter_dll_base || rip >= g_flutter_dll_end)
    return EXCEPTION_CONTINUE_SEARCH;

  g_exceptions_caught++;
  if (g_exceptions_caught <= 5 || g_exceptions_caught % 100 == 0) {
    GPU_LOGF("Caught AV #%d at RIP=flutter_windows.dll+0x%llx",
             g_exceptions_caught,
             (unsigned long long)(rip - g_flutter_dll_base));
  }

  // --- Decode x64 instruction length to skip past it ---
  BYTE* p = (BYTE*)rip;
  int pos = 0;

  // 1. Legacy prefixes
  while (p[pos] == 0x66 || p[pos] == 0x67 || p[pos] == 0xF2 ||
         p[pos] == 0xF3 || p[pos] == 0x2E || p[pos] == 0x3E ||
         p[pos] == 0x26 || p[pos] == 0x36 || p[pos] == 0x64 ||
         p[pos] == 0x65 || p[pos] == 0xF0)
    pos++;

  // 2. REX prefix (0x40-0x4F)
  if (p[pos] >= 0x40 && p[pos] <= 0x4F) pos++;

  // 3. Opcode
  BYTE opcode = p[pos++];
  bool has_modrm = false;
  int imm_size = 0;

  if (opcode == 0x0F) {
    // Two-byte opcode
    opcode = p[pos++];
    has_modrm = true;
  } else {
    if ((opcode & 0xC0) == 0x00 && (opcode & 0x07) < 6) has_modrm = true;
    if (opcode >= 0x80 && opcode <= 0x8F) has_modrm = true;
    if (opcode >= 0x88 && opcode <= 0x8F) has_modrm = true;
    if (opcode >= 0xD8 && opcode <= 0xDF) has_modrm = true;
    if (opcode == 0x63 || opcode == 0x69 || opcode == 0x6B) has_modrm = true;
    if (opcode == 0xC0 || opcode == 0xC1) has_modrm = true;
    if (opcode == 0xC6 || opcode == 0xC7) has_modrm = true;
    if (opcode == 0xD0 || opcode == 0xD1 || opcode == 0xD2 || opcode == 0xD3) has_modrm = true;
    if (opcode == 0xF6 || opcode == 0xF7) has_modrm = true;
    if (opcode == 0xFE || opcode == 0xFF) has_modrm = true;

    if (opcode == 0x80 || opcode == 0x82 || opcode == 0x83 ||
        opcode == 0xC0 || opcode == 0xC1 || opcode == 0x6B ||
        opcode == 0xC6) imm_size = 1;
    if (opcode == 0x81 || opcode == 0x69 || opcode == 0xC7) imm_size = 4;
  }

  // 4. ModRM + SIB + displacement
  if (has_modrm) {
    BYTE modrm = p[pos++];
    BYTE mod = modrm >> 6;
    BYTE rm = modrm & 0x07;

    bool has_sib = (mod != 3 && rm == 4);
    if (has_sib) {
      BYTE sib = p[pos++];
      if (mod == 0 && (sib & 0x07) == 5) pos += 4;
    }

    if (mod == 0 && rm == 5) pos += 4;
    else if (mod == 1) pos += 1;
    else if (mod == 2) pos += 4;
  }

  // 5. Immediate
  pos += imm_size;
  if (pos < 1) pos = 1;
  if (pos > 15) pos = 15;

  // Advance past the crashing instruction.
  info->ContextRecord->Rip = rip + pos;
  info->ContextRecord->Rax = 0;
  info->ContextRecord->Rdx = 0;
  info->ContextRecord->EFlags |= 0x40;  // ZF=1 (treat dead ptrs as null)

  return EXCEPTION_CONTINUE_EXECUTION;
}

/// Installs the VEH and records flutter_windows.dll address range.
static void InstallExceptionHandler() {
  HMODULE hmod = GetModuleHandleA("flutter_windows.dll");
  if (!hmod) return;

  MODULEINFO mi;
  if (GetModuleInformation(GetCurrentProcess(), hmod, &mi, sizeof(mi))) {
    g_flutter_dll_base = (DWORD64)mi.lpBaseOfDll;
    g_flutter_dll_end = g_flutter_dll_base + mi.SizeOfImage;
    GPU_LOGF("flutter_windows.dll: 0x%llx — 0x%llx (%u bytes)",
             (unsigned long long)g_flutter_dll_base,
             (unsigned long long)g_flutter_dll_end,
             (unsigned int)mi.SizeOfImage);
  }

  AddVectoredExceptionHandler(1, GpuRecoveryExceptionHandler);
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

// static
void WindowsGpuRecoveryPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<WindowsGpuRecoveryPlugin>(
      registrar, registrar->GetView());
  registrar->AddPlugin(std::move(plugin));
}

WindowsGpuRecoveryPlugin::WindowsGpuRecoveryPlugin(
    flutter::PluginRegistrarWindows* registrar,
    flutter::FlutterView* view)
    : registrar_(registrar), view_(view) {

  // Install crash catcher (idempotent — safe to call on re-registration
  // after engine recreation).
  InstallExceptionHandler();

  // Create sentinel device on the same DXGI adapter ANGLE uses.
  if (view_) {
    IDXGIAdapter* adapter = view_->GetGraphicsAdapter();
    if (adapter) {
      D3D_FEATURE_LEVEL fl;
      HRESULT hr = D3D11CreateDevice(
          adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
          nullptr, 0, D3D11_SDK_VERSION,
          &sentinel_device_, &fl, nullptr);
      if (SUCCEEDED(hr)) GPU_LOG("Sentinel D3D device created");
    }
  }

  // Hook into Flutter's message dispatch for WM_TIMER.
  proc_delegate_id_ = registrar_->RegisterTopLevelWindowProcDelegate(
      [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
          -> std::optional<LRESULT> {
        return HandleWindowProc(hwnd, msg, wp, lp);
      });

  GPU_LOG("Plugin initialized");
}

WindowsGpuRecoveryPlugin::~WindowsGpuRecoveryPlugin() {
  // Deactivate VEH filtering (handler stays registered but becomes no-op
  // for non-flutter AVs).
  g_exception_handler_active = false;

  if (host_hwnd_) KillTimer(host_hwnd_, kGpuWatchdogTimerId);
  if (proc_delegate_id_ != 0)
    registrar_->UnregisterTopLevelWindowProcDelegate(proc_delegate_id_);
}

std::optional<LRESULT> WindowsGpuRecoveryPlugin::HandleWindowProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  // Start watchdog on first message (captures host HWND).
  if (host_hwnd_ == nullptr) {
    host_hwnd_ = hwnd;
    GPU_LOG("Watchdog started (2 s interval)");
    SetTimer(hwnd, kGpuWatchdogTimerId, kWatchdogIntervalMs, nullptr);
  }

  if (message == WM_TIMER && wparam == kGpuWatchdogTimerId) {
    if (IsDeviceLost() && !recovery_requested_) {
      GPU_LOG("Device loss detected — activating exception handler");
      recovery_requested_ = true;
      g_exception_handler_active = true;
      g_exceptions_caught = 0;
      KillTimer(hwnd, kGpuWatchdogTimerId);

      // Tell the host window to recreate the engine.
      GPU_LOG("Posting WM_GPU_RECOVERY");
      PostMessage(hwnd, WM_GPU_RECOVERY, 0, 0);
    }
    return 0;
  }

  return std::nullopt;
}

bool WindowsGpuRecoveryPlugin::IsDeviceLost() {
  if (!sentinel_device_) return false;
  return sentinel_device_->GetDeviceRemovedReason() != S_OK;
}

}  // namespace windows_gpu_recovery
