// framework.h : Archivo de inclusion para includes estandar del sistema,
// o includes especificos del proyecto que se usan frecuentemente pero
// se modifican raramente.

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN

// Windows
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

// DirectX / DXGI para Desktop Duplication API
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// C++ Standard Library
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <array>
#include <cstdint>
