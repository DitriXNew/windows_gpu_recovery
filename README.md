# windows_gpu_recovery

[![pub.dev](https://img.shields.io/pub/v/windows_gpu_recovery.svg)](https://pub.dev/packages/windows_gpu_recovery)
[![pub points](https://img.shields.io/pub/points/windows_gpu_recovery)](https://pub.dev/packages/windows_gpu_recovery/score)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![platform: Windows](https://img.shields.io/badge/platform-Windows-0078D4?logo=windows)](https://pub.dev/packages/windows_gpu_recovery)

Flutter Windows plugin that recovers from `EGL_CONTEXT_LOST` / `DXGI_ERROR_DEVICE_REMOVED` after system sleep, Hyper-V save/restore, TDR timeout, or GPU driver reset. Without this plugin, the app freezes permanently with a white screen.

> **The recovery does NOT work when a debugger is attached** (VS Code `flutter run`, `F5`, or any external debugger). This is a Win32 debugging model limitation — the debugger intercepts exceptions before the plugin can handle them. **Run the compiled exe directly for testing and production.**

## What it does

After GPU device loss, a Flutter Windows app freezes forever — rendering stops, the window goes white, Windows marks it "Not Responding". CPU logic (Dart isolate, timers, method channels) continues working, but no frames are drawn.

This plugin:
1. **Detects** GPU device loss via a persistent sentinel D3D11 device (2-second polling)
2. **Destroys** the dead Flutter engine with crash protection (Vectored Exception Handler catches ANGLE cleanup crashes)
3. **Recreates** a fresh `FlutterViewController` with new ANGLE display, new D3D device, new Skia context
4. **Preserves state** — app saves state to SharedPreferences before recreation, restores on startup

The window stays open. The process stays alive. User sees a brief white flash (~1-3 seconds), then the app is back with restored state.

## Integration

### 1. Add the dependency

```yaml
dependencies:
  windows_gpu_recovery:
    path: ../windows_gpu_recovery  # or git URL
```

### 2. Modify `windows/runner/flutter_window.cpp`

Add the `WM_GPU_RECOVERY` handler. This is the only C++ change needed:

```cpp
#include <windows_gpu_recovery/gpu_recovery_message.h>

// In FlutterWindow::MessageHandler, BEFORE the Flutter message dispatch:
if (message == WM_GPU_RECOVERY) {
  // Write a recovery marker file for the new Dart VM.
  // (Your state-saving logic here — or use SharedPreferences from Dart side)

  // Destroy old engine — VEH catches ANGLE cleanup crashes.
  // eglTerminate clears the per-process Display singleton.
  flutter_controller_.reset();

  // Poll until GPU is ready (adapter recovered).
  // D3D11CreateDevice every 200ms, max 10 seconds.
  // (See example_with_recovery for full polling implementation)
  Sleep(3000);  // simple version

  // Create fresh engine.
  RECT frame = GetClientArea();
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  flutter_controller_->ForceRedraw();
  return 0;
}
```

### 3. Persist state on Dart side

The Dart VM restarts on recreation (new isolate). SharedPreferences (on disk) survives:

```dart
// Save state continuously (every significant change):
final prefs = await SharedPreferences.getInstance();
await prefs.setInt('counter', _counter);

// On startup, check for recovery marker:
final exeDir = File(Platform.resolvedExecutable).parent.path;
final marker = File('$exeDir/gpu_recovery.marker');
if (marker.existsSync()) {
  marker.deleteSync();
  final savedCounter = prefs.getInt('counter') ?? 0;
  // Restore state, show banner, etc.
}
```

### 4. No Dart-side initialization needed

The plugin works entirely at the native C++ level. Just adding the dependency is enough — the sentinel device and VEH are set up automatically on plugin registration.

## Testing

> Recovery does NOT work under a debugger. Always test by running the exe directly.

### Triggering GPU device loss

**`dxcap -forcetdr`** (recommended — clean, repeatable):
```cmd
:: Run as Administrator. Part of Windows SDK / Visual Studio.
dxcap -forcetdr
```
Install: `C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64\dxcap.exe`.

Other methods:
- **System sleep** — close laptop lid or Start > Sleep (real hardware only, not VMs)
- **Hyper-V** — Save VM + Start VM from host (no `WM_POWERBROADCAST` delivered)
- **Device Manager** — Disable/Enable GPU adapter (not over RDP)
- **PowerShell** — `Disable-PnpDevice` / `Enable-PnpDevice` (as Administrator)

### A/B test procedure

1. `flutter build windows --release` in both `example` and `example_without_recovery`
2. Launch both exe files directly (not via VS Code)
3. Click the counter several times
4. Run `dxcap -forcetdr` as Administrator
5. Observe:
   - **Without plugin** (red theme): frozen white screen, counter lost
   - **With plugin** (green theme): brief white flash, counter restored, green banner

### Logs

The `example` app writes to `gpu_recovery.log` next to the exe:
```
[GPU_RECOVERY] Sentinel D3D device created
[GPU_RECOVERY] Plugin initialized
[GPU_RECOVERY] Watchdog started (2 s interval)
[GPU_RECOVERY] Device loss detected — activating exception handler
[GPU_RECOVERY] Posting WM_GPU_RECOVERY
[GPU_RECOVERY] WM_GPU_RECOVERY received
[GPU_RECOVERY] Destroying old engine...
[GPU_RECOVERY] Old engine destroyed
[GPU_RECOVERY] GPU ready after 200 ms
[GPU_RECOVERY] Creating new engine...
[GPU_RECOVERY] Plugin initialized
[GPU_RECOVERY] Engine recreated successfully
```

## What survives vs what restarts

```
SURVIVES (same process):            RESTARTS (new engine):
  Win32 window (same HWND)            Dart VM (new isolate)
  C++ process (same PID)              Riverpod/Provider state
  SharedPreferences (on disk)         Navigation stack
  FlutterSecureStorage (on disk)      In-memory caches
  Files, network connections          Stream subscriptions
```

## Project structure

```
windows_gpu_recovery/
  lib/windows_gpu_recovery.dart           — library declaration (no Dart API needed)
  windows/
    windows_gpu_recovery_plugin.cpp       — sentinel device + VEH + watchdog
    windows_gpu_recovery_plugin.h
    include/.../gpu_recovery_message.h    — WM_GPU_RECOVERY constant
    CMakeLists.txt                        — links dxgi, d3d11
  example/                               — counter app + plugin + state restore
  example_without_recovery/               — counter app, no plugin (freezes)
```

---

<details>
<summary><b>How it works internally</b></summary>

## Detection: sentinel D3D11 device

A persistent D3D11 device is created on the same DXGI adapter that ANGLE uses (`FlutterDesktopViewGetGraphicsAdapter`). This sentinel device stays alive for the plugin's lifetime. When the GPU resets, `GetDeviceRemovedReason()` on the sentinel returns `DXGI_ERROR_DEVICE_REMOVED` — permanently, even after the adapter recovers (~100ms).

A freshly created test device would succeed (the adapter is back), so polling with temporary devices gives false negatives. The sentinel device shares the fate of ANGLE's internal device — both get the same `DEVICE_REMOVED` status.

## Recovery: VEH-protected engine recreation

### The problem

`flutter_controller_.reset()` calls the engine destructor → `eglTerminate` → ANGLE cleanup → `Renderer11::release()` → tries to `Release()` dead D3D COM objects → `ACCESS_VIOLATION` crash.

Without crash protection, the process dies. With `.release()` (leak without destructor), `eglTerminate` is never called → the per-process ANGLE `EGLDisplay` singleton stays in `mInitialized = true` state → new engine gets the same dead display.

### The solution

A Vectored Exception Handler (VEH) is installed via `AddVectoredExceptionHandler`. It catches `EXCEPTION_ACCESS_VIOLATION` inside `flutter_windows.dll`'s address range and skips the faulting instruction:

1. Decodes x64 instruction length (handles REX prefixes, ModRM, SIB, displacements)
2. Advances RIP past the crashing instruction
3. Sets RAX=0, RDX=0 (fake null/success return values)
4. Sets EFLAGS ZF=1 (dead pointers treated as null in subsequent conditional jumps)

With VEH active, `flutter_controller_.reset()` runs the full destructor. ANGLE's cleanup partially crashes (VEH catches), but `eglTerminate` reaches `Display::mInitialized = false` — clearing the singleton. The new `FlutterViewController` calls `eglInitialize`, which sees `mInitialized == false` and performs a full reinitialization: new `Renderer11`, new `D3D11Device`, new EGL contexts.

### Why approach 6 (without VEH) failed

In approach 6, `.release()` was used to avoid crashes. The engine was leaked, `eglTerminate` never ran, the Display singleton stayed poisoned. The VEH (developed for approach 9's binary patching experiments) turned out to be the critical enabler — it lets the destructor run safely, which clears the singleton.

### Debugger limitation

When a debugger is attached, Windows sends first-chance exceptions to the debugger **before** the VEH. The Dart debug adapter treats any unhandled exception as fatal and terminates the process. This is a Win32 debugging model constraint, not a plugin bug. The plugin works correctly when no debugger is attached.

</details>

---

<details>
<summary><b>Investigation log — for those who enjoy a detective story</b></summary>

## The problem

Flutter Windows applications become permanently unresponsive after any event that triggers a D3D11 device removal. The Flutter engine has no recovery path. Issue [flutter#124194](https://github.com/flutter/flutter/issues/124194) has been open since April 2023 at P2 priority with no fix or assigned engineer.

We conducted an exhaustive investigation: 11 approaches, 5 binary patches to `flutter_windows.dll`, a custom x64 instruction length decoder, a Vectored Exception Handler, and interactive reverse engineering with x64dbg.

## The rendering stack

```
Layer 7: Vsync scheduler (DWM)     — DEAD (swap chain gone)
Layer 6: Flutter frame scheduling   — IDLE (no vsync = no frames)
Layer 5: Skia GrDirectContext       — ABANDONED (auto-detected)
Layer 4: EGL Context (mContextLost) — PERMANENTLY LOST
Layer 3: EGL Display (mDeviceLost)  — PERMANENTLY LOST
Layer 2: ANGLE Renderer11 (D3D11)   — REMOVED
Layer 1: D3D11 adapter (hardware)   — RECOVERED in ~100ms
```

## Approach 1: InvalidateRect

Force repaint. Result: `eglMakeCurrent` returns `EGL_CONTEXT_LOST`. Can't paint with dead context.

## Approach 2: WM_POWERBROADCAST + WM_SIZE

Intercept resume from sleep, force resize. Result: swap chain resized but EGL context is dead. Also, `WM_POWERBROADCAST` not delivered in Hyper-V.

## Approach 3: RunOnSeparateThread

`UIThreadPolicy::RunOnSeparateThread`. Result: fixes performance regression (#178916), not context loss.

## Approach 4: SW_HIDE/SW_SHOW on Flutter child HWND

Plugin accesses `FlutterDesktopViewGetHWND`. Hide/show triggers surface recreation. Result: new surface on dead context → still fails.

## Approach 5: Sentinel D3D11 device

Create persistent device on ANGLE's adapter, poll `GetDeviceRemovedReason()`. **Works for detection.** Temporary test devices give false negatives (adapter recovers in ~100ms).

## Approach 6: FlutterViewController recreation

Destroy old controller, create new. Finding: `.reset()` crashes (ANGLE cleanup). `.release()` leaks but new engine gets same dead `EGLDisplay` singleton. Dead end without VEH.

## Approach 7: Binary patching — ANGLE recovery (5 patches)

ANGLE has `restoreLostDevice()` infrastructure. Blocked by two checks:

**Patches 1-2:** NOP `isResetNotificationEnabled` check in `restoreLostDevice`:
```
Pattern: 89 c1 b0 01 ba 0e 30 00 00 84 c9 75
Patch:   75 XX → 90 90
```

**Patches 3-5:** Skip `isContextLost` checks in `ValidateContext` (block before `ValidateDisplay`):
```
Patch 3: 0x00b33247: 74 → EB
Patch 4: 0x00b3585b: 74 → EB
Patch 5: 0x00b387f5: 74 → EB
```

Result: `restoreLostDevice` reached but `Renderer11::resetDevice()` → `release()` crashes on dead COM objects.

## Approach 8: ForceRedraw loop

After device loss, DWM stops vsync. `ForceRedraw` bypasses via `ScheduleFrame`. Result: frames scheduled but all fail on dead ANGLE context. With patches, `release()` crashes.

## Approach 9: Vectored Exception Handler

Custom x64 instruction decoder catches ACCESS_VIOLATION inside `flutter_windows.dll`. Skips faulting instructions with ZF=1. Result: 300+ AVs caught, app stays alive, but `release()` + `initialize()` compiler-inlined → skipping release skips initialize too.

## Approach 10: x64dbg reverse engineering

Found `GpuSurfaceGLSkia::AcquireFrame` by error string reference. Traced to `GrDirectContext*` at `[this+0x10]`. Found `fAbandoned` at `[sub+0xD8]` — already true. Found `releaseResourcesAndAbandonContext` — never called. Manually invoked via register injection — early return (already abandoned). Nobody creates replacement `GrDirectContext`.

## Approach 10b-c: Code cave + JMP patch

Found `release()` + `initialize()` inlined into 630-byte function. Crash at +431 (`cmp [r13+0x38E0], 0`). Found conditional `je +0x23` at +424 that skips to initialize. Patched to `jmp` (one byte). Result: release skipped, but initialize reads dead Renderer11 fields → VEH returns 0 → `D3D11CreateDevice` gets wrong params.

## 9 barriers deep

| # | Barrier | How we broke it |
|---|---------|----------------|
| 1 | restoreLostDevice blocked | Patch: NOP jnz |
| 2 | isContextLost blocks first | Patch: je → jmp |
| 3 | Vsync dead | ForceRedraw loop |
| 4 | release() crashes | VEH |
| 5 | release+initialize inlined | Patch: skip release |
| 6 | initialize reads dead fields | VEH returns 0 |
| 7 | Skia auto-abandoned | x64dbg confirmed |
| 8 | EGLDisplay singleton | ANGLE architecture |
| 9 | Renderer11 layout unknown | No PDB/RTTI |

## Approach 11: The breakthrough

Instead of fixing ANGLE from below (9 barriers), attack from above: destroy the entire engine and create a new one. Approaches 6 failed because `.release()` skipped `eglTerminate`. But the VEH from approach 9 lets `.reset()` run safely — the destructor calls `eglTerminate`, which clears `Display::mInitialized`, which lets the new engine reinitialize ANGLE from scratch.

The VEH that failed for approach 9 (couldn't fix ANGLE internally) became the critical enabler for approach 11 (protects the destructor during engine recreation).

</details>

---

## What a proper Flutter engine fix would look like

This plugin is a workaround — it restarts the Dart VM. A proper engine fix (~200 lines) would preserve the VM:

1. `egl/surface.cc` — detect `EGL_CONTEXT_LOST` specifically, signal to engine
2. `egl/manager.cc` — add `Reinitialize()` with SEH-protected `eglTerminate`
3. `flutter_windows_engine.cc` — `HandleDeviceLost()`: suspend raster thread, `GrDirectContext::abandonContext()`, reinitialize EGL, recreate compositor
4. `flutter_window.cc` — handle `WM_POWERBROADCAST`

## References

- [flutter#124194](https://github.com/flutter/flutter/issues/124194) — App freezes when GPU disabled (P2, open, no fix)
- [flutter#88649](https://github.com/flutter/flutter/issues/88649) — Desktop app freezes after sleep (closed, no fix)
- [flutter#163732](https://github.com/flutter/flutter/issues/163732) — Black/white screen after sleep in Parallels
- [ANGLE commit (2016)](https://chromium.googlesource.com/angle/angle/+/2823) — "device-loss at the display level is non-recoverable"
- [ANGLE Renderer11.cpp](https://chromium.googlesource.com/angle/angle/+/refs/heads/main/src/libANGLE/renderer/d3d/d3d11/Renderer11.cpp)
- [Microsoft: Handle device removed scenarios](https://learn.microsoft.com/en-us/windows/uwp/gaming/handling-device-lost-scenarios)

## License

MIT
