// AutoStart.h : Gestion del inicio automatico con Windows.
// Usa el registro de Windows (HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run)
// para registrar/desregistrar la aplicacion en el arranque.

#pragma once

#include "../../framework.h"

namespace antipop::config {

class AutoStart {
public:
    // Nombre de la clave en el registro
    static constexpr wchar_t kRegistryValueName[] = L"AntiPop";

    // Registra la aplicacion para iniciar con Windows.
    [[nodiscard]] static bool Enable();

    // Desregistra la aplicacion del inicio automatico.
    [[nodiscard]] static bool Disable();

    // Comprueba si la aplicacion esta registrada para iniciar con Windows.
    [[nodiscard]] static bool IsEnabled();

private:
    static constexpr wchar_t kRunKeyPath[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
};

} // namespace antipop::config
