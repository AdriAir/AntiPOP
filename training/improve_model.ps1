# improve_model.ps1 - Script de mejora automática del modelo AntiPop
#
# USO:
#   powershell -ExecutionPolicy Bypass -File improve_model.ps1
#
# Opciones:
#   -HardNegativesDir "ruta\a\negativos"   (carpeta con imágenes negativas)
#   -Mode "collect" | "train" | "full"     (colectar, entrenar, o todo)
#
# EJEMPLO:
#   powershell -File improve_model.ps1 -Mode full

param(
    [string]$HardNegativesDir = "hard_negatives_raw",
    [string]$Mode = "full",
    [int]$Epochs = 100,
    [string]$ModelSize = "n"
)

$ErrorActionPreference = "Stop"

# Colores para output
function Write-Header { Write-Host "`n$args" -ForegroundColor Cyan -BackgroundColor Black }
function Write-Success { Write-Host "✓ $args" -ForegroundColor Green }
function Write-Error { Write-Host "✗ ERROR: $args" -ForegroundColor Red }
function Write-Info { Write-Host "  $args" -ForegroundColor White }

Write-Header "╔════════════════════════════════════════════╗"
Write-Header "║  AntiPop - Mejora Automática del Modelo   ║"
Write-Header "╚════════════════════════════════════════════╝"

# Verificar que estamos en la carpeta training/
if (-not (Test-Path "train.py")) {
    Write-Error "Ejecuta este script desde la carpeta training/"
    exit 1
}

Write-Info "Modo: $Mode"
Write-Info "Directorio de negativos: $HardNegativesDir"
Write-Info "Epochs: $Epochs"
Write-Info "Tamaño modelo: yolov8$ModelSize"

# === PASO 1: RECOPILAR NEGATIVOS DUROS ===
if ($Mode -eq "collect" -or $Mode -eq "full") {
    Write-Header "`nPASO 1: Recopilando Hard Negatives..."

    # Crear directorio si no existe
    if (-not (Test-Path $HardNegativesDir)) {
        New-Item -ItemType Directory -Path $HardNegativesDir -Force | Out-Null
        Write-Info "Carpeta creada: $HardNegativesDir"
        Write-Error "Coloca imágenes en $HardNegativesDir y ejecuta de nuevo"
        exit 1
    }

    $images = @(Get-ChildItem $HardNegativesDir -Include *.jpg, *.jpeg, *.png, *.bmp -ErrorAction SilentlyContinue)
    if ($images.Count -eq 0) {
        Write-Error "No hay imágenes en $HardNegativesDir"
        Write-Info "Coloca imágenes sospechosas (falsos positivos) en esa carpeta."
        exit 1
    }

    Write-Info "Encontradas $($images.Count) imágenes"
    Write-Header "Ejecutando prepare_hard_negatives.py --auto-detect..."

    # Llamar al script Python para procesar negativos duros
    python prepare_hard_negatives.py --auto-detect
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Error al procesar hard negatives"
        exit 1
    }

    Write-Success "Hard negatives procesados"
}

# === PASO 2: VERIFICAR DATASET ===
Write-Header "`nPASO 2: Verificando dataset..."

$imagesCount = (Get-ChildItem "dataset/images" -Include *.jpg, *.png -ErrorAction SilentlyContinue).Count
$labelsCount = (Get-ChildItem "dataset/labels" -Include *.txt -ErrorAction SilentlyContinue).Count

Write-Info "Imágenes: $imagesCount"
Write-Info "Etiquetas: $labelsCount"

if ($imagesCount -lt 50) {
    Write-Error "Necesitas al menos 50 imágenes de entrenamiento"
    Write-Info "Recolecta más imágenes de pulpos en dataset/images/"
    exit 1
}

if ($labelsCount -eq 0) {
    Write-Error "No hay etiquetas en dataset/labels/"
    Write-Info "Usa annotate.py o CVAT/Roboflow para anotar las imágenes"
    exit 1
}

Write-Success "Dataset verificado ($imagesCount imágenes, $labelsCount etiquetas)"

# === PASO 3: ENTRENAR ===
if ($Mode -eq "train" -or $Mode -eq "full") {
    Write-Header "`nPASO 3: Entrenando modelo..."

    $args_train = @(
        "train.py",
        "--epochs", $Epochs,
        "--model-size", $ModelSize
    )

    Write-Info "Comando: python $($args_train -join ' ')"
    Write-Info "Esto puede tardar 15-30 minutos con GPU, o 2-3h con CPU"
    Write-Info ""
    Read-Host "Presiona ENTER para continuar (o Ctrl+C para cancelar)"

    python @args_train
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Error durante el entrenamiento"
        exit 1
    }

    Write-Success "Modelo entrenado exitosamente"
}

# === RESUMEN ===
Write-Header "`nRESUMEN"
Write-Success "Proceso completado"
Write-Info "Siguiente paso: ejecuta AntiPop para probar el nuevo modelo"
Write-Info ""
Write-Info "Para mejorar aún más:"
Write-Info "  1. Ejecuta prepare_hard_negatives.py periodicamente"
Write-Info "  2. Recolecta más imágenes (ideal: 2000-5000)"
Write-Info "  3. Reentrana con más epochs: python train.py --epochs 150"

Write-Header ""
