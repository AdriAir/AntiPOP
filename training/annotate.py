"""
annotate.py - Herramienta simple de anotacion de imagenes para AntiPop.

Abre cada imagen de training/dataset/images/ y permite dibujar bounding boxes
de pulpos con el raton. Guarda las anotaciones en formato YOLO (.txt) en
training/dataset/labels/.

CONTROLES:
    - Click izquierdo + arrastrar: dibujar bounding box
    - 'z': deshacer ultimo bounding box
    - 'n' o ENTER: siguiente imagen (guarda automaticamente)
    - 's': saltar imagen sin guardar
    - 'q' o ESC: salir

FORMATO DE SALIDA (YOLO):
    Cada linea del .txt: <class_id> <cx> <cy> <width> <height>
    Valores normalizados entre 0.0 y 1.0 respecto al tamano de la imagen.
"""

import cv2
import sys
from pathlib import Path

DATASET_DIR = Path(__file__).resolve().parent / "dataset"
IMAGES_DIR = DATASET_DIR / "images"
LABELS_DIR = DATASET_DIR / "labels"

# Clase unica: octopus = 0
CLASS_ID = 0
CLASS_NAME = "octopus"

# Estado global del dibujo
drawing = False
start_x, start_y = 0, 0
current_boxes = []
temp_box = None


def mouse_callback(event, x, y, flags, param):
    """Callback del raton para dibujar bounding boxes."""
    global drawing, start_x, start_y, temp_box

    if event == cv2.EVENT_LBUTTONDOWN:
        drawing = True
        start_x, start_y = x, y
        temp_box = None

    elif event == cv2.EVENT_MOUSEMOVE and drawing:
        temp_box = (start_x, start_y, x, y)

    elif event == cv2.EVENT_LBUTTONUP:
        drawing = False
        if abs(x - start_x) > 5 and abs(y - start_y) > 5:
            # Normalizar coordenadas: asegurar que (x1,y1) es top-left
            x1 = min(start_x, x)
            y1 = min(start_y, y)
            x2 = max(start_x, x)
            y2 = max(start_y, y)
            current_boxes.append((x1, y1, x2, y2))
        temp_box = None


def draw_ui(image, boxes, temp):
    """Dibuja los bounding boxes y la interfaz sobre la imagen."""
    display = image.copy()

    # Dibujar boxes confirmados en verde
    for i, (x1, y1, x2, y2) in enumerate(boxes):
        cv2.rectangle(display, (x1, y1), (x2, y2), (0, 255, 0), 2)
        cv2.putText(display, f"{CLASS_NAME} #{i+1}",
                    (x1, y1 - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

    # Dibujar box temporal en amarillo
    if temp:
        cv2.rectangle(display, (temp[0], temp[1]), (temp[2], temp[3]), (0, 255, 255), 2)

    # Barra de informacion
    h = display.shape[0]
    bar_y = h - 30
    cv2.rectangle(display, (0, bar_y), (display.shape[1], h), (40, 40, 40), -1)
    info = f"Boxes: {len(boxes)} | ENTER=guardar | Z=deshacer | S=saltar | Q=salir"
    cv2.putText(display, info, (10, h - 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (200, 200, 200), 1)

    return display


def save_annotations(image_path: Path, boxes: list, img_width: int, img_height: int):
    """Guarda las anotaciones en formato YOLO."""
    LABELS_DIR.mkdir(exist_ok=True)
    label_path = LABELS_DIR / f"{image_path.stem}.txt"

    with open(label_path, "w") as f:
        for (x1, y1, x2, y2) in boxes:
            # Convertir a formato YOLO: centro_x, centro_y, ancho, alto (normalizados)
            cx = ((x1 + x2) / 2.0) / img_width
            cy = ((y1 + y2) / 2.0) / img_height
            w = (x2 - x1) / img_width
            h = (y2 - y1) / img_height
            f.write(f"{CLASS_ID} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}\n")

    return label_path


def get_pending_images():
    """Devuelve las imagenes que aun no tienen anotacion."""
    if not IMAGES_DIR.exists():
        return []

    all_images = sorted(
        list(IMAGES_DIR.glob("*.jpg")) +
        list(IMAGES_DIR.glob("*.jpeg")) +
        list(IMAGES_DIR.glob("*.png"))
    )

    # Filtrar las que ya tienen anotacion
    annotated = {f.stem for f in LABELS_DIR.glob("*.txt")} if LABELS_DIR.exists() else set()
    pending = [img for img in all_images if img.stem not in annotated]

    return all_images, pending


def main():
    global current_boxes, temp_box

    all_images, pending = get_pending_images()

    if not all_images:
        print(f"No hay imagenes en {IMAGES_DIR}")
        print("Coloca imagenes de pulpos (.jpg, .png) en esa carpeta.")
        sys.exit(1)

    print(f"Imagenes totales: {len(all_images)}")
    print(f"Pendientes de anotar: {len(pending)}")
    print()
    print("Controles:")
    print("  Click + arrastrar = dibujar bounding box")
    print("  Z = deshacer ultimo box")
    print("  ENTER/N = guardar y siguiente")
    print("  S = saltar sin guardar")
    print("  Q/ESC = salir")
    print()

    if not pending:
        print("Todas las imagenes ya estan anotadas!")
        response = input("Quieres re-anotar desde el principio? [s/N]: ")
        if response.lower() == "s":
            pending = all_images
        else:
            return

    window_name = "AntiPop - Anotacion de pulpos"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    cv2.setMouseCallback(window_name, mouse_callback)

    for idx, image_path in enumerate(pending):
        current_boxes = []
        temp_box = None

        image = cv2.imread(str(image_path))
        if image is None:
            print(f"  No se pudo leer: {image_path.name}, saltando...")
            continue

        img_h, img_w = image.shape[:2]

        # Redimensionar ventana si la imagen es muy grande
        display_w = min(img_w, 1280)
        display_h = int(img_h * (display_w / img_w))
        cv2.resizeWindow(window_name, display_w, display_h)

        print(f"[{idx+1}/{len(pending)}] {image_path.name} ({img_w}x{img_h})")

        while True:
            display = draw_ui(image, current_boxes, temp_box)
            cv2.imshow(window_name, display)
            key = cv2.waitKey(30) & 0xFF

            if key == ord("q") or key == 27:  # Q o ESC
                print("Saliendo...")
                cv2.destroyAllWindows()
                return

            elif key == ord("z"):  # Deshacer
                if current_boxes:
                    removed = current_boxes.pop()
                    print(f"  Deshecho box #{len(current_boxes)+1}")

            elif key == 13 or key == ord("n"):  # ENTER o N = guardar y siguiente
                if current_boxes:
                    label_path = save_annotations(image_path, current_boxes, img_w, img_h)
                    print(f"  Guardado: {label_path.name} ({len(current_boxes)} boxes)")
                else:
                    # Guardar archivo vacio = imagen sin pulpos (negativo)
                    label_path = save_annotations(image_path, [], img_w, img_h)
                    print(f"  Guardado como negativo (sin pulpos)")
                break

            elif key == ord("s"):  # Saltar
                print(f"  Saltado")
                break

    cv2.destroyAllWindows()
    print()
    print("Anotacion completada!")
    print(f"Ahora ejecuta 'python train.py' para entrenar el modelo.")


if __name__ == "__main__":
    main()
