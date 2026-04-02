#ifndef GPU_RECOVERY_MESSAGE_H_
#define GPU_RECOVERY_MESSAGE_H_

#include <windows.h>

// Custom window message sent by the GPU recovery plugin to trigger
// a full FlutterViewController recreation. The host window (FlutterWindow)
// should handle this in its MessageHandler by destroying and recreating
// flutter_controller_.
//
// This is the only reliable recovery path for EGL_CONTEXT_LOST /
// DXGI_ERROR_DEVICE_REMOVED — the EGL context is permanently bound to the
// dead D3D device and cannot be reinitialized through any public Flutter API.
#define WM_GPU_RECOVERY (WM_USER + 0x4750)  // "GP" in hex

#endif  // GPU_RECOVERY_MESSAGE_H_
