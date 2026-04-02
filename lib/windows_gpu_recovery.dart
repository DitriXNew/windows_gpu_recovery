/// Flutter Windows plugin for recovering from EGL_CONTEXT_LOST /
/// DXGI_ERROR_DEVICE_REMOVED after system sleep or GPU driver reset.
///
/// The plugin works entirely at the native level:
/// 1. Sentinel D3D11 device detects GPU loss (2 s polling)
/// 2. Vectored Exception Handler protects ANGLE cleanup crashes
/// 3. Posts WM_GPU_RECOVERY to the host window
/// 4. flutter_window.cpp destroys and recreates the FlutterViewController
///
/// No Dart-side initialization is needed — the plugin starts on registration.
library windows_gpu_recovery;
