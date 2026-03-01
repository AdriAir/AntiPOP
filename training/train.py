"""
train.py - Script de entrenamiento del modelo de deteccion de pulpos para AntiPop.

REQUISITOS:
    - Python 3.11 (PyTorch estable; el nightly cu128 tambien soporta 3.11)
    - GPUs Blackwell (RTX 5000): nightly cu128
        pip install --pre torch torchvision --index-url https://download.pytorch.org/whl/nightly/cu128
    - Otras GPUs NVIDIA:
        pip install torch torchvision --index-url https://download.pytorch.org/whl/cu124
    - Ejecuta setup.bat para configurar el entorno automaticamente.

USO:
    1. Coloca tus imagenes (.jpg/.png) en training/dataset/images/
    2. Anota las imagenes con el script annotate.py o con Roboflow/CVAT
    3. Ejecuta: python train.py

El script entrena un modelo YOLOv8, lo exporta a ONNX y lo copia
automaticamente a la carpeta models/ del proyecto.
"""

import sys
import shutil
import time
from pathlib import Path

import torch

# Directorio raiz del proyecto (un nivel arriba de training/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent
DATASET_DIR = Path(__file__).resolve().parent / "dataset"
MODELS_DIR = PROJECT_ROOT / "models"
OUTPUT_NAME = "octopus_detector"


def check_gpu():
    """Verifica la disponibilidad de CUDA y compatibilidad de la GPU."""
    print("\n--- Configuracion de dispositivo ---")

    if not torch.cuda.is_available():
        print("  CUDA: NO disponible")
        print("  Dispositivo: CPU (el entrenamiento sera mucho mas lento)")
        print()
        print("  NOTA: Para GPUs NVIDIA, ejecuta setup.bat para instalar PyTorch con CUDA.")
        print()
        return "cpu"

    gpu_name = torch.cuda.get_device_name(0)
    props = torch.cuda.get_device_properties(0)
    vram_total = props.total_memory / (1024 ** 3)          # Corregido: total_memory
    major, minor = props.major, props.minor
    compute_cap = f"sm_{major}{minor}"

    print(f"  CUDA: v{torch.version.cuda}")
    print(f"  GPU:  {gpu_name}")
    print(f"  VRAM: {vram_total:.1f} GB")
    print(f"  Compute capability: {major}.{minor} ({compute_cap})")

    # Verificar que esta arquitectura esta soportada por este build de PyTorch.
    # PyTorch cu124 solo llega a sm_90 (Ada Lovelace).
    # Blackwell (sm_120, RTX 5000 series) requiere nightly cu128.
    supported = torch.cuda.get_arch_list()
    arch_key = f"sm_{major}{minor}"
    if arch_key not in supported:
        print()
        print(f"  AVISO: {gpu_name} ({compute_cap}) no esta soportada por este build de PyTorch.")
        print(f"  Arquitecturas soportadas: {', '.join(supported)}")
        print()
        print("  Para GPUs Blackwell (RTX 5000), instala el nightly cu128:")
        print("    pip install --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cu128")
        print("  O ejecuta setup.bat de nuevo (lo instala automaticamente).")
        print()
        response = input("  Continuar entrenando en CPU como fallback? [s/N]: ")
        if response.lower() != "s":
            sys.exit(0)
        print("  Usando CPU.")
        return "cpu"

    print(f"  Dispositivo: cuda:0")
    return "cuda:0"


def check_dataset():
    """Verifica que el dataset tenga imagenes y anotaciones."""
    images_dir = DATASET_DIR / "images"
    labels_dir = DATASET_DIR / "labels"

    if not images_dir.exists():
        print(f"ERROR: No se encuentra la carpeta de imagenes: {images_dir}")
        print("Crea la carpeta y coloca tus imagenes (.jpg, .png) dentro.")
        sys.exit(1)

    image_files = list(images_dir.glob("*.jpg")) + list(images_dir.glob("*.png"))
    if not image_files:
        print(f"ERROR: No hay imagenes en {images_dir}")
        print("Coloca imagenes de pulpos (.jpg o .png) en esa carpeta.")
        sys.exit(1)

    label_files = list(labels_dir.glob("*.txt")) if labels_dir.exists() else []

    print(f"Dataset encontrado:")
    print(f"  Imagenes: {len(image_files)}")
    print(f"  Anotaciones: {len(label_files)}")

    if not label_files:
        print()
        print("AVISO: No hay anotaciones (.txt) en dataset/labels/.")
        print("Ejecuta primero 'python annotate.py' para anotar las imagenes,")
        print("o usa Roboflow (https://roboflow.com) para anotarlas online.")
        print()
        response = input("Continuar sin anotaciones? El modelo no aprendera nada util. [s/N]: ")
        if response.lower() != "s":
            sys.exit(0)

    # Verificar que haya al menos una anotacion por cada imagen (warning)
    image_stems = {f.stem for f in image_files}
    label_stems = {f.stem for f in label_files}
    missing = image_stems - label_stems
    if missing:
        print(f"  AVISO: {len(missing)} imagenes sin anotacion (se trataran como negativos)")

    return len(image_files)


def train(epochs: int = 100, model_size: str = "n", imgsz: int = 640, device: str = "cuda:0"):
    """
    Entrena el modelo YOLOv8.

    Args:
        epochs: Numero de epocas de entrenamiento.
        model_size: Tamano del modelo base ('n'=nano, 's'=small, 'm'=medium).
        imgsz: Tamano de imagen de entrada del modelo.
        device: Dispositivo de entrenamiento ('cuda:0' o 'cpu').
    """
    from ultralytics import YOLO

    data_yaml = DATASET_DIR / "data.yaml"
    base_model = f"yolov8{model_size}.pt"

    print(f"\n--- Parametros de entrenamiento ---")
    print(f"  Modelo base: {base_model}")
    print(f"  Epochs: {epochs}")
    print(f"  Tamano imagen: {imgsz}x{imgsz}")
    print(f"  Dispositivo: {device}")
    print(f"  Dataset: {data_yaml}")

    # Cargar modelo base pre-entrenado en COCO
    print(f"\nCargando modelo base {base_model}...")
    model = YOLO(base_model)

    # Registrar callbacks para feedback por epoca
    def on_train_epoch_start(trainer):
        epoch = trainer.epoch + 1
        total = trainer.epochs
        print(f"\n>>> Epoca {epoch}/{total} iniciada...")

    def on_train_epoch_end(trainer):
        epoch = trainer.epoch + 1
        total = trainer.epochs
        metrics = trainer.metrics
        box_loss = metrics.get("train/box_loss", "N/A")
        cls_loss = metrics.get("train/cls_loss", "N/A")
        map50 = metrics.get("metrics/mAP50(B)", "N/A")

        parts = [f"Epoca {epoch}/{total}"]
        if isinstance(box_loss, float):
            parts.append(f"box_loss={box_loss:.4f}")
        if isinstance(cls_loss, float):
            parts.append(f"cls_loss={cls_loss:.4f}")
        if isinstance(map50, float):
            parts.append(f"mAP50={map50:.4f}")
        print(f"    {' | '.join(parts)}")

    def on_train_start(trainer):
        print(f"\nEntrenamiento iniciado. Esto puede tardar un rato...\n")

    model.add_callback("on_train_start", on_train_start)
    model.add_callback("on_train_epoch_start", on_train_epoch_start)
    model.add_callback("on_train_epoch_end", on_train_epoch_end)

    # Entrenar con el dataset de pulpos
    t_start = time.time()
    results = model.train(
        data=str(data_yaml),
        epochs=epochs,
        imgsz=imgsz,
        batch=-1,           # Auto-batch segun VRAM disponible
        device=device,
        patience=20,         # Early stopping si no mejora en 20 epochs
        save=True,
        project=str(Path(__file__).resolve().parent / "runs"),
        name="octopus_train",
        exist_ok=True,
    )
    elapsed = time.time() - t_start
    minutes, seconds = divmod(int(elapsed), 60)
    print(f"\nEntrenamiento finalizado en {minutes}m {seconds}s.")

    return results


def export_and_deploy(imgsz: int = 640):
    """Exporta el mejor modelo a ONNX y lo copia a models/."""
    from ultralytics import YOLO

    # Buscar el mejor checkpoint
    best_pt = Path(__file__).resolve().parent / "runs" / "octopus_train" / "weights" / "best.pt"
    if not best_pt.exists():
        print(f"ERROR: No se encontro el modelo entrenado en {best_pt}")
        sys.exit(1)

    print(f"\nExportando modelo a ONNX...")
    model = YOLO(str(best_pt))

    # Exportar a ONNX optimizado para inferencia
    model.export(
        format="onnx",
        imgsz=imgsz,
        simplify=True,      # Simplificar grafo ONNX
        opset=17,            # Opset compatible con ONNX Runtime 1.24
    )

    # El archivo exportado esta junto al .pt
    onnx_path = best_pt.with_suffix(".onnx")
    if not onnx_path.exists():
        print(f"ERROR: No se genero el archivo ONNX en {onnx_path}")
        sys.exit(1)

    # Copiar al directorio models/ del proyecto
    MODELS_DIR.mkdir(exist_ok=True)
    dest = MODELS_DIR / f"{OUTPUT_NAME}.onnx"
    shutil.copy2(onnx_path, dest)

    print(f"\nModelo exportado exitosamente:")
    print(f"  ONNX: {dest}")
    print(f"  Tamano: {dest.stat().st_size / (1024*1024):.1f} MB")
    print(f"\nAntiPop cargara este modelo automaticamente al ejecutarse.")


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Entrena el modelo de deteccion de pulpos para AntiPop."
    )
    parser.add_argument(
        "--epochs", type=int, default=100,
        help="Numero de epocas de entrenamiento (default: 100)"
    )
    parser.add_argument(
        "--model-size", choices=["n", "s", "m"], default="n",
        help="Tamano del modelo: n=nano (rapido), s=small, m=medium (default: n)"
    )
    parser.add_argument(
        "--imgsz", type=int, default=640,
        help="Tamano de imagen de entrada (default: 640)"
    )
    parser.add_argument(
        "--export-only", action="store_true",
        help="Solo exportar un modelo ya entrenado, sin volver a entrenar"
    )

    args = parser.parse_args()

    print("=" * 60)
    print("  AntiPop - Entrenamiento del detector de pulpos")
    print("=" * 60)

    device = check_gpu()

    if not args.export_only:
        num_images = check_dataset()

        # Ajustar epochs si hay pocas imagenes
        if num_images < 50:
            print(f"\nAVISO: Solo hay {num_images} imagenes. Se recomiendan al menos 200.")
            print("El modelo puede no funcionar bien con tan pocas imagenes.")

        train(epochs=args.epochs, model_size=args.model_size, imgsz=args.imgsz, device=device)

    export_and_deploy(imgsz=args.imgsz)

    print()
    print("=" * 60)
    print("  Entrenamiento completado!")
    print("  Compila y ejecuta AntiPop para probar el modelo.")
    print("=" * 60)


if __name__ == "__main__":
    main()
