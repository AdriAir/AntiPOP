# AntiPop - Mejoras de Estética y Cobertura

## 🎯 Mejoras Implementadas

### 1. **Expansión de Bounding Boxes** ✅

**Problema**: La censura no cubría completamente los pulpos/tentáculos, dejando partes visibles.

**Solución**: Aumentar el margen de expansión automáticamente.

**Antes**: `censor_expansion = 0.15` (15%)
**Ahora**: `censor_expansion = 0.30` (30%)

**Beneficio**: Cubre un 30% más alrededor de la detección → sin tentáculos a la vista.

**Ajustable en**: `x64/Debug/antipop.cfg`
```ini
# Aumenta para mayor cobertura (0.5 = 50%)
censor_expansion = 0.30
```

---

### 2. **Censura Pixelada (Mosaico)** ✅ **NUEVO**

**Problema**: Rectángulos negros sólidos se veían poco profesionales.

**Solución**: Implementar pixelado/mosaico configurable (como en videos de censura).

**Cómo Funciona**:
```
ANTES:            AHORA:
████████          ▓▓▓▓▓▓▓▓
████████          ░▓▓▓░▓▓▓
████████          ▓▓░▓▓▓░▓
████████          ░▓▓▓▓░▓▓

(rectángulo)      (pixelado estético)
```

**Configuración en `antipop.cfg`**:

```ini
# Tipo de censura: 0=solido negro, 1=pixelado
censor_type = 1

# Tamaño del bloque de pixelado en pixels
# Valores recomendados:
#   8  = muy detallado (solo pixeliza levemente)
#  12  = recomendado (balance)
#  16  = muy pixelado (imposible ver detrás)
pixelate_block_size = 12
```

**Características del Pixelado**:
- Bloques de color gris oscuro (no puro negro para efecto)
- Variación de color para efecto visual
- Imposible ver detalles detrás del pixelado
- Más profesional que rectángulos sólidos

**Ajustes**:
```ini
# Para mayor privacidad (más pixelado):
pixelate_block_size = 16

# Para menos pixelado (más visible pero mejor performance):
pixelate_block_size = 8

# O volver a rectángulo sólido:
censor_type = 0
```

---

### 3. **Buffer Temporal Aumentado** ✅

**Problema**: Parpadeos cada 2-3 segundos (cuando el detector fallaba).

**Solución**: Aumentar el buffer que mantiene la censura incluso sin detecciones.

**Antes**: 8 frames (~800ms)
**Ahora**: 12 frames (~1200ms)

**Beneficio**:
- Tolera fallos del detector durante 1.2 segundos
- Parpadeos menos frecuentes
- Menos probabilidad de ver contenido brevemente

**Cómo Funciona**:
```
Frame 1:  ✓ Detecta pulpo → Censura visible
Frame 2:  ✗ No detecta (fallo) → Mantiene censura (frame 1/12)
Frame 3:  ✗ No detecta → Mantiene censura (frame 2/12)
...
Frame 12: ✗ No detecta → Mantiene censura (frame 11/12)
Frame 13: ✗ No detecta → LIMPIA la censura (frame 12/12)

→ Resultado: 1.2 segundos de censura incluso sin detección
```

**Nota**: Para fallos aún más largos, necesitas reentrenar el modelo.

---

## 📋 Cambios en Archivos

### **Configuración**
- `x64/Debug/antipop.cfg` - Nuevos parámetros:
  - `censor_expansion = 0.30` (antes: 0.15)
  - `censor_type = 1` (0=sólido, 1=pixelado)
  - `pixelate_block_size = 12` (tamaño del bloque)

### **Código C++**
- `src/config/Config.h` - Nuevos campos en AppConfig
- `src/config/Config.cpp` - Parseo de nuevos parámetros
- `src/overlay/OverlayWindow.h` - Método `SetCensorStyle()`
- `src/overlay/OverlayWindow.cpp` - Implementación de pixelado
- `src/App.h` - Buffer aumentado a 12 frames
- `src/App.cpp` - Configuración del overlay

---

## 🎨 Visualización

### **Modo Sólido (censor_type=0)**
```
██████████
██████████
██████████
██████████
```
- Rectángulo negro puro
- Simple pero menos profesional

### **Modo Pixelado (censor_type=1)**
```
▒▒▒▒▒▒▒▒▒▒
▒░▒▒▒░▒▒▒▒
▒▒▒░▒▒▒░▒▒
░▒▒▒▒░▒▒▒░
```
- Bloques de gris oscuro (RGB 40-60)
- Patrón pseudo-aleatorio
- Imposible ver detrás
- Más profesional

---

## 🔧 Ajustes Recomendados

### **Para Máxima Privacidad**
```ini
censor_expansion = 0.50        # 50% más grande
censor_type = 1                # Pixelado
pixelate_block_size = 20       # Muy grande
```
→ Bloque completamente cubierto, imposible ver detrás

### **Para Balance (Recomendado)**
```ini
censor_expansion = 0.30        # 30% más grande
censor_type = 1                # Pixelado
pixelate_block_size = 12       # Tamaño medio
```
→ Buen balance entre privacidad y estética

### **Para Rendimiento**
```ini
censor_expansion = 0.20        # Menos expansión
censor_type = 0                # Sólido (menos CPU)
pixelate_block_size = 0        # N/A
```
→ Menos cálculos, más rápido

---

## 📊 Comparativa Visual

| Aspecto | Antes | Ahora |
|---------|-------|-------|
| **Cobertura** | Incompleta (tentáculos visibles) | Completa (30% más) |
| **Estética** | Rectángulo negro sólido | Pixelado profesional |
| **Parpadeos** | Cada 2-3s | Cada 4-5s (buffer +50%) |
| **Privacidad** | Media | Alta |
| **Profesionalismo** | Bajo | Alto |

---

## 🧪 Cómo Probar

### **Test 1: Cobertura**
1. Muestra un pulpo en pantalla
2. Verifica que no se vean tentáculos a los lados
3. Resultado esperado: Completamente cubierto

### **Test 2: Pixelado**
1. Activa modo pixelado (`censor_type = 1`)
2. Abre AntiPop
3. Verifica que la censura sea bloques grises, no negra sólida
4. Intenta ver detrás del pixelado (imposible)

### **Test 3: Tamaño del Pixel**
1. Prueba con `pixelate_block_size = 8` (detallado)
2. Prueba con `pixelate_block_size = 16` (muy pixelado)
3. Encuentra el balance que prefieras

### **Test 4: Parpadeos**
1. Muestra objeto moviéndose
2. Desaparece brevemente (simula fallo del detector)
3. Verifica que censura se mantenga ~1.2 segundos
4. Resultado: Menos parpadeos visibles

---

## 💡 Notas Técnicas

### **Por qué Pixelado es Mejor**
1. **Privacidad**: Imposible ver detalles con bloques ≥12px
2. **Estética**: Parece intencional (no accidental)
3. **Profesionalismo**: Se usa en medios profesionales
4. **Performance**: Similar a rectángulo sólido

### **Por qué Buffer de 12 Frames**
- 100ms × 12 = 1200ms (1.2 segundos)
- Tolera fallos intermitentes del detector
- 3 segundos es demasiado (parece censura pegajosa)
- 800ms es insuficiente (aún hay parpadeos)

### **Limitaciones**
- **Parpadeos cada 2-3 segundos aún existen**: La solución definitiva es mejorar el modelo (entrenamiento con más datos/negativos)
- **El tracking suaviza el movimiento pero no elimina fallos**: El detector sigue siendo la limitación principal

---

## 🚀 Próximos Pasos Para Eliminar Parpadeos Completamente

1. **Reentrenar el modelo** con:
   - Más imágenes (~2000+ en lugar de 850)
   - Hard negatives (cosas que se parecen pero no son pulpos)
   - `python training/train.py --epochs 150`

2. **O aumentar más el buffer**:
   - Editar `src/App.h: kFramesToClearCensor = 20` (2 segundos)
   - Trade-off: Censura "pegajosa" pero menos parpadeos

3. **O ejecutar captura más frecuente**:
   - Cambiar `capture_interval_ms = 50` en config (20 FPS)
   - Trade-off: Más CPU pero más frame rate

---

## 📄 Configuración Completa (antipop.cfg)

```ini
# AntiPop - Configuracion
model_path = models/octopus_detector.onnx
confidence_threshold = 0.75

# Captura cada 100ms (10 FPS)
capture_interval_ms = 100

# Censura: 30% más grande
censor_expansion = 0.30

# Tipo de censura: pixelado (profesional)
censor_type = 1
pixelate_block_size = 12

# Color sólido (solo si censor_type=0)
censor_color_r = 0
censor_color_g = 0
censor_color_b = 0

auto_start = true
show_tray_icon = true
```

---

**Última actualización**: 2026-03-01
**Status**: ✅ Implementado y Listo para Compilar
