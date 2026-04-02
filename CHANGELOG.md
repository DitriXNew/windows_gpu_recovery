## 0.1.0

* Initial release.
* Sentinel D3D11 device detects GPU device loss (`DXGI_ERROR_DEVICE_REMOVED`) with 2-second polling interval.
* Vectored Exception Handler (VEH) protects ANGLE cleanup crashes during engine destruction, allowing `eglTerminate` to clear the per-process `EGLDisplay` singleton.
* Posts `WM_GPU_RECOVERY` to the host window for host-side engine recreation.
* No Dart-side initialization required — plugin activates automatically on registration.
* Requires a one-time modification to `windows/runner/flutter_window.cpp` to handle `WM_GPU_RECOVERY` and recreate the `FlutterViewController`.
