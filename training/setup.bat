@echo off
echo ============================================
echo   AntiPop - Configuracion del entorno de entrenamiento
echo ============================================
echo.

:: ============================================================
:: 1. Buscar Python 3.11
:: ============================================================
set "PY311="

py -3.11 --version >nul 2>&1
if not errorlevel 1 (
    set "PY311=py -3.11"
    goto :found_python
)

for %%P in (
    "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
    "C:\Python311\python.exe"
    "%PROGRAMFILES%\Python311\python.exe"
) do (
    if exist %%P (
        set "PY311=%%~P"
        goto :found_python
    )
)

echo ERROR: No se encontro Python 3.11.
echo.
echo PyTorch con CUDA requiere Python 3.11.
echo Descarga Python 3.11 desde: https://www.python.org/downloads/release/python-3119/
echo Asegurate de marcar "Add to PATH" durante la instalacion.
pause
exit /b 1

:found_python
echo Python 3.11 encontrado:
%PY311% --version
echo.

:: ============================================================
:: 2. Crear entorno virtual
:: ============================================================
if exist venv (
    echo Eliminando entorno virtual anterior...
    rmdir /s /q venv
)

echo Creando entorno virtual...
%PY311% -m venv venv
if errorlevel 1 (
    echo ERROR: No se pudo crear el entorno virtual.
    pause
    exit /b 1
)

call venv\Scripts\activate.bat

:: ============================================================
:: 3. Detectar GPU y elegir build de PyTorch
::
:: Blackwell (RTX 5000, compute cap 12.x) -> nightly cu128
:: Otras NVIDIA                            -> estable cu124
:: Sin GPU / fallo                         -> CPU
:: ============================================================
echo Detectando GPU NVIDIA...

set "CUDA_ARCH="
nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>nul > "%TEMP%\antipop_gpu.txt"
if not errorlevel 1 (
    set /p CUDA_ARCH=<"%TEMP%\antipop_gpu.txt"
    del "%TEMP%\antipop_gpu.txt" >nul 2>&1
)

if "%CUDA_ARCH%"=="" (
    echo   No se detecto GPU NVIDIA con nvidia-smi.
) else (
    echo   Compute capability: %CUDA_ARCH%
)

echo.
set "TORCH_INSTALLED=0"

:: Blackwell: compute capability empieza por "12"
echo %CUDA_ARCH% | findstr /r "^12\." >nul 2>&1
if not errorlevel 1 (
    echo GPU Blackwell ^(sm_120+^) detectada.
    echo Instalando PyTorch nightly cu128 ^(unico build con soporte Blackwell^)...
    echo.
    pip install --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cu128
    if not errorlevel 1 (
        set "TORCH_INSTALLED=1"
        set "TORCH_BUILD=nightly/cu128"
    ) else (
        echo AVISO: Fallo la instalacion del nightly cu128.
    )
)

:: Otras GPU NVIDIA: cu124 estable
if "%TORCH_INSTALLED%"=="0" if not "%CUDA_ARCH%"=="" (
    echo GPU NVIDIA detectada.
    echo Instalando PyTorch estable cu124...
    echo.
    pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124
    if not errorlevel 1 (
        set "TORCH_INSTALLED=1"
        set "TORCH_BUILD=estable/cu124"
    ) else (
        echo AVISO: Fallo la instalacion de cu124.
    )
)

:: Fallback: CPU only
if "%TORCH_INSTALLED%"=="0" (
    echo No se encontro GPU compatible. Instalando PyTorch CPU...
    echo El entrenamiento sera significativamente mas lento.
    echo.
    pip install torch torchvision torchaudio
    if not errorlevel 1 (
        set "TORCH_INSTALLED=1"
        set "TORCH_BUILD=CPU"
    )
)

if "%TORCH_INSTALLED%"=="0" (
    echo ERROR: No se pudo instalar PyTorch por ningun metodo.
    pause
    exit /b 1
)

:: ============================================================
:: 4. Instalar el resto de dependencias
::    NOTA: requirements.txt NO incluye torch/torchvision para
::    no pisar el build especifico que acabamos de instalar.
:: ============================================================
echo.
echo Instalando dependencias restantes ^(ultralytics, opencv, pillow^)...
pip install -r requirements.txt

:: ============================================================
:: 5. Verificar instalacion
:: ============================================================
echo.
echo Verificando instalacion de PyTorch...
python -c "import torch; print(f'  PyTorch {torch.__version__} | CUDA disponible: {torch.cuda.is_available()}')"
if errorlevel 1 (
    echo ERROR: PyTorch no se importa correctamente.
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Entorno listo! ^(Build: %TORCH_BUILD%^)
echo.
echo   Pasos siguientes:
echo   1. Coloca imagenes de pulpos en dataset\images\
echo   2. Activa el entorno: venv\Scripts\activate.bat
echo   3. Anota las imagenes: python annotate.py
echo   4. Entrena el modelo: python train.py
echo ============================================
pause
