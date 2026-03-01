// AutoStart.cpp : Implementacion del registro de inicio automatico via registro de Windows.

#include "AutoStart.h"
#include "../utils/Logger.h"

namespace antipop::config {

bool AutoStart::Enable() {
    // Obtener la ruta completa del ejecutable actual
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        LOG_ERROR("No se pudo obtener la ruta del ejecutable");
        return false;
    }

    // Abrir la clave del registro
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey);

    if (result != ERROR_SUCCESS) {
        LOG_ERROR("No se pudo abrir la clave del registro: {}", result);
        return false;
    }

    // Escribir la ruta del ejecutable como valor
    // Se usa comillas para soportar rutas con espacios
    std::wstring command = std::wstring(L"\"") + exePath + L"\" --silent";
    result = RegSetValueExW(
        hKey,
        kRegistryValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        LOG_ERROR("No se pudo escribir en el registro: {}", result);
        return false;
    }

    LOG_INFO("Inicio automatico habilitado");
    return true;
}

bool AutoStart::Disable() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey);

    if (result != ERROR_SUCCESS) return false;

    result = RegDeleteValueW(hKey, kRegistryValueName);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        LOG_ERROR("No se pudo eliminar la clave del registro: {}", result);
        return false;
    }

    LOG_INFO("Inicio automatico deshabilitado");
    return true;
}

bool AutoStart::IsEnabled() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &hKey);

    if (result != ERROR_SUCCESS) return false;

    result = RegQueryValueExW(hKey, kRegistryValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}

} // namespace antipop::config
