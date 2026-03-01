"""
prepare_hard_negatives.py - Minería de negativos duros para mejorar el modelo.

PROPOSITO:
    El modelo actual tiene falsos positivos. Este script ayuda a recolectar
    imágenes de "cosas que se parecen a pulpos pero NO son pulpos"
    (hard negatives). Entrenar con estos datos mejora significativamente
    la precision.

USO:
    1. Coloca imágenes sospechosas en training/hard_negatives_raw/
    2. Ejecuta: python prepare_hard_negatives.py
    3. El script mostrara cada imagen y preguntará si es un pulpo o no
    4. Las que NO sean pulpos se copian a dataset/images/ automaticamente

RESULTADO:
    Las imágenes negativas se agregan al dataset como "negativos"
    (archivos .txt vacíos en dataset/labels/)
"""

import sys
import shutil
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np

# Directorios
PROJECT_ROOT = Path(__file__).resolve().parent.parent
DATASET_DIR = Path(__file__).resolve().parent / "dataset"
HARD_NEGATIVES_RAW = Path(__file__).resolve().parent / "hard_negatives_raw"
IMAGES_DIR = DATASET_DIR / "images"
LABELS_DIR = DATASET_DIR / "labels"


def ensure_dirs():
    """Crea los directorios necesarios."""
    HARD_NEGATIVES_RAW.mkdir(exist_ok=True)
    IMAGES_DIR.mkdir(exist_ok=True)
    LABELS_DIR.mkdir(exist_ok=True)
    print(f"✓ Directorios listos")


def load_hard_negative_images() -> List[Path]:
    """Carga imágenes de hard_negatives_raw/"""
    image_exts = {".jpg", ".jpeg", ".png", ".bmp"}
    images = []

    for ext in image_exts:
        images.extend(HARD_NEGATIVES_RAW.glob(f"*{ext}"))
        images.extend(HARD_NEGATIVES_RAW.glob(f"*{ext.upper()}"))

    return sorted(images)


def display_image_for_review(image_path: Path) -> bool:
    """
    Muestra una imagen y pregunta si es un pulpo.
    Retorna True si es pulpo, False si NO es pulpo.
    """
    img = cv2.imread(str(image_path))
    if img is None:
        print(f"ERROR: No se pudo leer {image_path}")
        return None

    # Redimensionar si es muy grande (para pantalla)
    height, width = img.shape[:2]
    if width > 1280 or height > 720:
        ratio = min(1280 / width, 720 / height)
        new_width = int(width * ratio)
        new_height = int(height * ratio)
        img = cv2.resize(img, (new_width, new_height), interpolation=cv2.INTER_AREA)

    window_name = f"¿ES PULPO? (presiona S/N) - {image_path.name}"
    cv2.imshow(window_name, img)
    cv2.resizeWindow(window_name, 800, 600)

    while True:
        key = cv2.waitKey(0) & 0xFF
        if key == ord("s") or key == ord("S"):
            cv2.destroyWindow(window_name)
            return True  # Es pulpo
        elif key == ord("n") or key == ord("N"):
            cv2.destroyWindow(window_name)
            return False  # NO es pulpo
        elif key == 27:  # ESC - saltar
            cv2.destroyWindow(window_name)
            return None


def process_hard_negatives():
    """Procesa imágenes de negativos duros."""
    ensure_dirs()

    images = load_hard_negative_images()
    if not images:
        print(f"ERROR: No hay imágenes en {HARD_NEGATIVES_RAW}/")
        print(f"Coloca imágenes en esa carpeta y ejecuta de nuevo.")
        return

    print(f"\nEncontradas {len(images)} imágenes para revisar.")
    print("Presiona:")
    print("  S - ES un pulpo (lo añade al dataset como positivo)")
    print("  N - NO es un pulpo (hard negative, mejora precision)")
    print("  ESC - Saltar esta imagen")
    print()

    octopus_count = 0
    negative_count = 0

    for i, image_path in enumerate(images, 1):
        print(f"\n[{i}/{len(images)}] Revisando {image_path.name}...")

        result = display_image_for_review(image_path)
        if result is None:
            print("  → Saltada")
            continue
        elif result:
            print("  → SÍ es pulpo (positivo)")
            # Copiar a dataset/images/ si no existe
            dest = IMAGES_DIR / image_path.name
            if not dest.exists():
                shutil.copy2(image_path, dest)
                print(f"    Copiada a {dest.name}")
                octopus_count += 1
            else:
                print(f"    Ya existe en dataset (no sobrescrita)")
        else:
            print("  → NO es pulpo (hard negative)")
            # Copiar a dataset/images/
            dest = IMAGES_DIR / image_path.name
            if not dest.exists():
                shutil.copy2(image_path, dest)
                print(f"    Copiada a {dest.name}")
            else:
                print(f"    Ya existe en dataset (actualizada)")

            # Crear etiqueta vacía para indicar "negativo"
            label_path = LABELS_DIR / image_path.stem
            label_path = label_path.with_suffix(".txt")
            label_path.write_text("")  # Archivo vacío = imagen negativa
            print(f"    Etiqueta creada (negativa)")
            negative_count += 1

    print(f"\n{'='*60}")
    print(f"RESUMEN:")
    print(f"  Pulpos positivos agregados: {octopus_count}")
    print(f"  Hard negatives agregados: {negative_count}")
    print(f"{'='*60}")

    if negative_count > 0:
        print(f"\n✓ {negative_count} hard negatives añadidos al dataset.")
        print(f"  Ejecuta 'python train.py' para reentrenar el modelo con estos negativos.")
        print(f"  Resultado esperado: menos falsos positivos, mejor precision.")
    else:
        print(f"\nNo se agregaron negativos duros.")


def quick_scan_with_model():
    """
    Opción avanzada: escanear imágenes detectando con el modelo actual
    y solo mostrar las que el modelo detecta (para etiquetar como negativos).
    """
    print("\n[MODO AVANZADO] Escaneo automático con modelo...")
    print("Esto requiere que tengas un modelo entrenado.")

    from pathlib import Path

    # Buscar el mejor modelo entrenado
    best_pt = Path(__file__).resolve().parent / "runs" / "octopus_train" / "weights" / "best.pt"
    if not best_pt.exists():
        print(f"ERROR: No se encontró modelo en {best_pt}")
        print(f"Entrena un modelo primero ejecutando: python train.py")
        return

    try:
        from ultralytics import YOLO
    except ImportError:
        print("ERROR: ultralytics no está instalada. Ejecuta:")
        print("  pip install ultralytics")
        return

    print(f"Cargando modelo: {best_pt}")
    model = YOLO(str(best_pt))

    images = load_hard_negative_images()
    if not images:
        print(f"No hay imágenes en {HARD_NEGATIVES_RAW}/")
        return

    detected_images = []

    for image_path in images:
        results = model.predict(str(image_path), verbose=False, conf=0.5)
        # Si el modelo detectó algo, es un falso positivo potencial
        if results[0].boxes.data.shape[0] > 0:
            detected_images.append(image_path)

    print(f"\nModelo detectó objetos en {len(detected_images)}/{len(images)} imágenes.")
    print("Mostrando solo estas para etiquetar como negativos...\n")

    # Procesar solo las detectadas
    negative_count = 0
    for i, image_path in enumerate(detected_images, 1):
        print(f"\n[{i}/{len(detected_images)}] {image_path.name}")
        result = display_image_for_review(image_path)

        if result is None:
            continue
        elif result:
            print("  → SÍ es pulpo (positivo)")
        else:
            print("  → NO es pulpo (hard negative)")
            dest = IMAGES_DIR / image_path.name
            shutil.copy2(image_path, dest)
            label_path = LABELS_DIR / image_path.stem
            label_path = label_path.with_suffix(".txt")
            label_path.write_text("")
            negative_count += 1

    print(f"\n✓ Agregados {negative_count} hard negatives del escaneo automático")


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Minería de negativos duros para mejorar precision")
    parser.add_argument("--auto-detect", action="store_true",
                        help="Usar el modelo actual para detectar falsos positivos (requiere modelo entrenado)")

    args = parser.parse_args()

    print("=" * 60)
    print("  AntiPop - Minería de Negativos Duros")
    print("=" * 60)

    if args.auto_detect:
        quick_scan_with_model()
    else:
        process_hard_negatives()

    print(f"\nSiguiente paso: ejecuta 'python train.py' para reentrenar.")


if __name__ == "__main__":
    main()
