// anti-pop.cpp : Punto de entrada de la aplicacion AntiPop.
//
// Esta aplicacion se ejecuta como un proceso en segundo plano con un icono
// en la bandeja del sistema. No muestra ventana principal visible.
// El pipeline de censura (captura -> IA -> overlay) corre en un hilo dedicado.

#include "framework.h"
#include "anti-pop.h"
#include "src/App.h"
#include "src/utils/Logger.h"
#include "src/config/Config.h"

// Instancia global de la aplicacion
static std::unique_ptr<antipop::App> g_app;

// Forward declarations
static LRESULT CALLBACK HiddenWndProc(HWND, UINT, WPARAM, LPARAM);
static bool             RegisterHiddenWindowClass(HINSTANCE hInstance);

// Nombre de la clase de la ventana oculta (solo para recibir mensajes del tray)
static constexpr wchar_t kHiddenWindowClass[] = L"AntiPopHiddenWindow";

// Mutex global para evitar multiples instancias
static constexpr wchar_t kMutexName[] = L"Global\\AntiPopSingleInstance";

// Mensaje personalizado del icono de la bandeja
static constexpr UINT WM_TRAYICON = WM_USER + 1;

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // ========================================================================
    // 1. Prevenir multiples instancias
    // ========================================================================
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // ========================================================================
    // 2. Inicializar el logger
    // ========================================================================
    auto logPath = antipop::config::Config::GetAppDirectory() / "antipop.log";
#ifdef _DEBUG
    antipop::utils::Logger::Instance().Initialize(logPath, antipop::utils::LogLevel::Debug);
#else
    antipop::utils::Logger::Instance().Initialize(logPath, antipop::utils::LogLevel::Info);
#endif
    LOG_INFO("=== AntiPop iniciando ===");

    // ========================================================================
    // 3. Detectar si se inicio en modo silencioso (auto-start con Windows)
    // ========================================================================
    bool silentMode = false;
    if (lpCmdLine) {
        std::wstring cmdLine(lpCmdLine);
        silentMode = (cmdLine.find(L"--silent") != std::wstring::npos);
    }

    // ========================================================================
    // 4. Crear la ventana oculta (necesaria para recibir mensajes del tray)
    // ========================================================================
    if (!RegisterHiddenWindowClass(hInstance)) {
        LOG_ERROR("No se pudo registrar la clase de ventana oculta");
        CloseHandle(hMutex);
        return 1;
    }

    HWND hwndHidden = CreateWindowExW(
        0, kHiddenWindowClass, L"", 0,
        0, 0, 0, 0,
        HWND_MESSAGE,   // Ventana solo-mensajes, invisible
        nullptr, hInstance, nullptr
    );

    if (!hwndHidden) {
        LOG_ERROR("No se pudo crear la ventana oculta");
        CloseHandle(hMutex);
        return 1;
    }

    // ========================================================================
    // 5. Inicializar la aplicacion
    // ========================================================================
    g_app = std::make_unique<antipop::App>();
    if (!g_app->Initialize(hInstance, silentMode)) {
        LOG_ERROR("Fallo la inicializacion de AntiPop");
        g_app.reset();
        CloseHandle(hMutex);
        return 1;
    }

    // ========================================================================
    // 6. Configurar el icono de la bandeja del sistema
    // ========================================================================
    if (g_app->GetConfig().showTrayIcon) {
        g_app->SetupTrayIcon(hwndHidden);
    }

    // ========================================================================
    // 7. Iniciar el pipeline de censura
    // ========================================================================
    g_app->Start();
    LOG_INFO("AntiPop ejecutandose");

    // ========================================================================
    // 8. Message loop principal
    // ========================================================================
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ========================================================================
    // 9. Limpieza
    // ========================================================================
    g_app->Stop();
    g_app->RemoveTrayIcon();
    g_app.reset();

    LOG_INFO("=== AntiPop finalizado ===");
    antipop::utils::Logger::Instance().Shutdown();

    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}

bool RegisterHiddenWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = HiddenWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kHiddenWindowClass;
    return RegisterClassExW(&wc) != 0;
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TRAYICON:
        if (g_app) {
            g_app->HandleTrayMessage(hwnd, wParam, lParam);
        }
        return 0;

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1001:  // ID_TRAY_EXIT
            if (g_app) {
                g_app->Stop();
                g_app->RemoveTrayIcon();
            }
            PostQuitMessage(0);
            return 0;

        case 1002:  // ID_TRAY_TOGGLE
            if (g_app) {
                if (g_app->IsRunning()) {
                    g_app->Stop();
                } else {
                    g_app->Start();
                }
            }
            return 0;

        case 1003:  // ID_TRAY_ABOUT
            MessageBoxW(hwnd,
                L"AntiPop v1.0\n\n"
                L"Proteccion automatica contra contenido de pulpos.\n"
                L"Usa vision artificial para detectar y censurar\n"
                L"imagenes de pulpos en tiempo real.\n\n"
                L"Copyright (c) 2026",
                L"Acerca de AntiPop",
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
