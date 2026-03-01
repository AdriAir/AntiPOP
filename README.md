# AntiPop

Aplicacion para Windows 11 que detecta y censura automaticamente contenido visual de pulpos en pantalla, en tiempo real, usando vision artificial.

Se ejecuta de forma invisible en segundo plano con un icono en la bandeja del sistema.

## Arquitectura

```text
anti-pop.cpp                  Punto de entrada (wWinMain, message loop, tray icon)
src/
  App.h/.cpp                  Orquestador: inicializa subsistemas, ejecuta pipeline
  capture/
    IScreenCapture.h          Interfaz de captura de pantalla
    DxgiCapture.h/.cpp        Implementacion via Desktop Duplication API (GPU)
  detector/
    Detection.h               Structs: BoundingBox, Detection, CensorCategory
    IContentDetector.h        Interfaz de deteccion de contenido
    OnnxDetector.h/.cpp       Inferencia YOLOv8 via ONNX Runtime
  overlay/
    IOverlay.h                Interfaz del overlay de censura
    OverlayWindow.h/.cpp      Ventana transparente topmost click-through
  config/
    Config.h/.cpp             Carga/guarda configuracion (antipop.cfg)
    AutoStart.h/.cpp          Registro de Windows para inicio automatico
  utils/
    Logger.h                  Logger thread-safe con macros LOG_*
training/                     Pipeline de entrenamiento del modelo de IA
models/                       Modelos ONNX (no incluidos en git)
```

### Pipeline de censura (hilo dedicado, ~10 FPS)

```text
Captura DXGI (GPU) --> Preproceso (resize 640x640, BGRA->RGB) --> YOLOv8 (ONNX Runtime) --> NMS --> Overlay
```

## Requisitos del proyecto (C++)

- **Windows 11** (Windows 10 compatible)
- **Visual Studio 2026 Community** con workload "Desktop development with C++"
- **NuGet**: `Microsoft.ML.OnnxRuntime 1.24.2` (se restaura automaticamente)

## Compilar

1. Abrir `anti-pop.slnx` en Visual Studio 2026
2. Restaurar paquetes NuGet: click derecho en la solucion > "Restore NuGet Packages"
3. Seleccionar configuracion **Release | x64**
4. Compilar con Ctrl+Shift+B

El ejecutable se genera en `x64/Release/anti-pop.exe`.

---

## Entrenar el modelo

AntiPop necesita un modelo YOLOv8 entrenado para detectar pulpos.
El modelo no se incluye en el repositorio por su tamano; hay que generarlo una vez.

### Requisitos de Python

- **Python 3.11** (requerido por PyTorch)
- **PyTorch** con el build correcto segun tu GPU (ver tabla abajo)

### Compatibilidad GPU / PyTorch

Cada GPU NVIDIA pertenece a una arquitectura con un "compute capability" (sm_XX).
PyTorch se compila para un conjunto concreto de arquitecturas; si tu GPU no esta incluida, falla.

| Arquitectura | GPUs ejemplo | Compute cap | Build PyTorch necesario |
| --- | --- | --- | --- |
| Maxwell | GTX 900 | sm_52 | cu124 estable |
| Pascal | GTX 1000 | sm_60/61 | cu124 estable |
| Volta | V100 | sm_70 | cu124 estable |
| Turing | RTX 2000 | sm_75 | cu124 estable |
| Ampere | RTX 3000 | sm_80/86 | cu124 estable |
| Ada Lovelace | RTX 4000 | sm_89 | cu124 estable |
| Hopper | H100 | sm_90 | cu124 estable |
| **Blackwell** | **RTX 5000** | **sm_120** | **nightly cu128** |

> **RTX 5060 Ti y serie RTX 5000 en general**: cu124 estable no soporta sm_120.
> Necesitas el **nightly cu128**, que es el unico build con soporte Blackwell.

### Paso 1: Instalar PyTorch (hacer una vez)

Ejecuta desde la carpeta `training/`:

**Opcion A — setup.bat (automatico, detecta tu GPU):**

```bat
cd training
setup.bat
```

`setup.bat` usa `nvidia-smi` para leer el compute capability, elige el build correcto e instala todo.

**Opcion B — manual (si ya tienes el venv creado y solo necesitas reinstalar PyTorch):**

```bat
cd training
venv\Scripts\activate

:: RTX 5000 (Blackwell, sm_120):
pip uninstall torch torchvision torchaudio -y
pip install --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cu128

:: Otras GPUs NVIDIA:
pip uninstall torch torchvision torchaudio -y
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124
```

Verificar que funciona:

```bat
python -c "import torch; print(torch.__version__, '| CUDA:', torch.cuda.is_available())"
```

### Paso 2: Recopilar imagenes

Coloca imagenes de pulpos (.jpg, .png) en `training/dataset/images/`.

Se recomiendan **al menos 200 imagenes** con variedad de:

- Fotos reales, ilustraciones, capturas de pantalla
- Diferentes tamanos, angulos, fondos
- Incluir tambien imagenes **sin** pulpos (negativos) para reducir falsos positivos

Fuentes de imagenes sugeridas:

- [Open Images Dataset](https://storage.googleapis.com/openimages/web/index.html) (tiene clase "Octopus")
- [Roboflow Universe](https://universe.roboflow.com/) (buscar "octopus", muchos datasets ya anotados)
- Google Images, Flickr, Unsplash

### Paso 3: Anotar las imagenes

```bat
venv\Scripts\activate
python annotate.py
```

Se abre una ventana donde puedes dibujar bounding boxes sobre cada pulpo:

- **Click + arrastrar**: dibujar rectangulo alrededor del pulpo
- **Z**: deshacer ultimo box
- **Enter / N**: guardar y pasar a la siguiente imagen
- **S**: saltar imagen sin guardar
- **Q / Escape**: salir

Las anotaciones se guardan en `training/dataset/labels/` en formato YOLO.

**Alternativa online**: [Roboflow](https://roboflow.com) (interfaz web, mas comodo para grandes volumenes). Exportar en formato "YOLOv8" y copiar los `.txt` a `dataset/labels/`.

### Paso 4: Entrenar

```bat
python train.py
```

Opciones:

```text
--epochs 100        Numero de epocas (default: 100)
--model-size n      Tamano: n=nano (recomendado), s=small, m=medium
--imgsz 640         Resolucion de entrada (default: 640)
--export-only       Solo exportar un modelo ya entrenado, sin re-entrenar
```

Al terminar, el modelo se copia automaticamente a `models/octopus_detector.onnx` y AntiPop lo carga en el siguiente arranque.

### Tiempos estimados (200 imagenes, 100 epochs, yolov8n)

| GPU | Tiempo aprox. |
| --- | --- |
| RTX 5060 Ti (Blackwell) | ~6 min |
| RTX 4070 | ~8 min |
| RTX 3060 | ~15 min |
| Solo CPU | ~2-4 horas |

---

## Configuracion

Al ejecutarse por primera vez, AntiPop crea `antipop.cfg` junto al ejecutable:

```ini
# Ruta al modelo ONNX (relativa al directorio del ejecutable)
model_path = models/octopus_detector.onnx

# Umbral de confianza para detecciones (0.0 - 1.0)
confidence_threshold = 0.5

# Intervalo entre capturas en ms (menor = mas fluido, mas CPU)
capture_interval_ms = 100

# Margen extra alrededor de las detecciones (0.15 = 15%)
censor_expansion = 0.15

# Color de censura RGB (0-255). Negro por defecto.
censor_color_r = 0
censor_color_g = 0
censor_color_b = 0

# Iniciar automaticamente con Windows
auto_start = true

# Mostrar icono en la bandeja del sistema
show_tray_icon = true
```

## Uso

1. Ejecutar `anti-pop.exe` (o dejar que inicie automaticamente con Windows)
2. Aparece un icono en la bandeja del sistema
3. **Click derecho** en el icono: menu con opciones de pausar/salir
4. **Doble click** en el icono: pausar/reanudar la censura

La aplicacion funciona de forma completamente invisible. Solo veras los bloques de censura cuando se detecte un pulpo en pantalla.

### Argumentos de linea de comandos

```text
anti-pop.exe              Inicio normal
anti-pop.exe --silent     Inicio silencioso (usado por el auto-start con Windows)
```

---

## Estructura modular

| Modulo | Responsabilidad | Interfaz publica |
| --- | --- | --- |
| **capture** | Captura de pantalla via GPU | `IScreenCapture` |
| **detector** | Deteccion de objetos con IA | `IContentDetector` |
| **overlay** | Renderizado de censura | `IOverlay` |
| **config** | Configuracion y auto-start | `Config`, `AutoStart` |
| **utils** | Logging thread-safe | `Logger` |

Las interfaces permiten cambiar implementaciones sin tocar el resto del codigo. Por ejemplo:

- Reemplazar DXGI por captura GDI para compatibilidad con versiones antiguas de Windows
- Usar TensorRT en vez de ONNX Runtime para mayor rendimiento en GPU NVIDIA
- Cambiar el overlay de GDI a Direct2D para rendering mas eficiente

## Ampliar a nuevas categorias de censura

1. Agregar las nuevas clases en `training/dataset/data.yaml`
2. Re-entrenar el modelo con `python train.py`
3. Actualizar `m_targetClassIds` y `m_classNames` en `src/detector/OnnxDetector.cpp`
4. Opcionalmente, declarar la nueva categoria en `CensorCategory` (`src/detector/Detection.h`)

## Licencia

Proyecto privado.
