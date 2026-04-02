#include "include/windows_gpu_recovery/windows_gpu_recovery_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "windows_gpu_recovery_plugin.h"

void WindowsGpuRecoveryPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  windows_gpu_recovery::WindowsGpuRecoveryPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
