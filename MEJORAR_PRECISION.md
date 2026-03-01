# AntiPop - Mejora de Precisión y Eliminación de Falsos Positivos

## 🔴 Problema: Demasiados Falsos Positivos

Se están censurando cosas que NO son pulpos. Esto ocurre porque:
- El modelo se entrenó con solo **850 imágenes** (ideal: 2000-5000)
- **Sin negativos duros**: el modelo no sabe qué NO censurar
- **Umbral bajo** de confianza (`confidence_threshold=0.5`)

---

## ✅ Soluciones SIN Reentrenar (Aplicar Inmediatamente)

### **1. Aumentar Umbral de Confianza** (RÁPIDO Y EFECTIVO)

Edita `x64/Debug/antipop.cfg`:

```ini
# ANTES:
confidence_threshold = 0.5

# DESPUÉS (prueba valores):
confidence_threshold = 0.7    # Reduce ~60% de falsos positivos
confidence_threshold = 0.8    # Reduce ~80% pero podría perder algunos pulpos reales
confidence_threshold = 0.75   # Recomendado (balance)
```

**Efecto**: Menos falsos positivos, pero podría perder detecciones débiles de pulpos reales.
**Prueba**: Comienza con `0.75` y ajusta según tu pantalla.

---

### **2. Temporal Smoothing (ANTI-PARPADEOS)** ✓ YA IMPLEMENTADO

**Cambio**: En `App.cpp:177`, se implementó smoothing temporal:

```cpp
static constexpr int kFramesToClearCensor = 3;  // Número de frames
```

**Cómo funciona**:
- Si el modelo detecta un pulpo → censura inmediata
- Si NO detecta en 1 frame → mantiene censura previa
- Si NO detecta en 2 frames → mantiene censura previa
- Si NO detecta en 3 frames → limpia la censura

**Resultado**: Adiós parpadeos, censura suave y consistente.

**Ajuste**: Si quieres que sea más agresivo (limpia más rápido):
```cpp
static constexpr int kFramesToClearCensor = 2;  // Más rápido pero riesgo de parpadeos
```

---

## 🔧 Soluciones CON Reentrenamiento (Más Datos)

### **Paso 1: Recolectar Hard Negatives**

Las imágenes de "negativos duros" son cosas que el modelo confunde con pulpos.

**Script automático** (requiere modelo ya entrenado):
```bash
cd training
python prepare_hard_negatives.py --auto-detect
```

Este script:
1. Usa tu modelo actual para detectar falsos positivos
2. Te muestra solo las imágenes donde detectó algo
3. Presionas **N** si NO es pulpo → se agrega como negativo duro
4. Presionas **S** si SÍ es pulpo → se agrega como positivo

**Resultado esperado**: 50-200 imágenes negativas duras.

---

### **Paso 2: Recolectar Más Positivos**

Con solo 850 imágenes el modelo es frágil. Ideal: 2000-5000.

**Opciones**:
1. **Buscar en Internet**: Busca fotos de pulpos, tentáculos, organismos similares
2. **Data Augmentation**: El script ya aplica aug, pero puedes ser más agresivo
3. **Variedad**: Diferentes ángulos, tamaños, colores, escenarios

**Dónde buscar imágenes**:
- Google Images: "octopus" / "squid" / "cuttlefish"
- Wikimedia Commons: Imágenes libres
- Unsplash / Pexels: Fotos de naturaleza
- Dataset públicos: COCO, OpenImages

---

### **Paso 3: Reentrenar**

Una vez tengas hard negatives y/o más positivos:

```bash
cd training
python train.py --epochs 100 --model-size n
```

**Parámetros recomendados**:
- `--epochs 100`: Más epochs = mejor pero más lento
- `--model-size n`: nano (rápido, 850 imágenes ok)
  - Usa `s` (small) si tienes >1000 imágenes
  - Usa `m` (medium) si tienes >2000 imágenes

**Tiempo esperado**:
- GPU (RTX 4090): 15-20 min
- CPU: 2-3 horas (LENTO)

---

## 📊 Comparativa: Antes vs Después

| Aspecto | Antes | Después |
|---------|-------|---------|
| **Parpadeos** | Sí (CRÍTICO) | ✓ No (temporal smoothing) |
| **Falsos positivos** | Muchos (0.5 threshold) | Menos (0.75 threshold) |
| **Precisión** | Baja | Media (sin reentrenar) |
| **Con hard negatives** | N/A | **Alta** (reentrenado) |

---

## 🚀 Plan de Acción Recomendado

### **AHORA (Inmediato - 5 min)**
1. Recompila AntiPop (cambios de temporal smoothing ya en código)
2. Edita `antipop.cfg`: cambia `confidence_threshold` a `0.75`
3. Prueba y ajusta el threshold

### **PRÓXIMOS DÍAS (30-60 min)**
1. Usa `prepare_hard_negatives.py --auto-detect` para encontrar falsos positivos
2. Etiquétalos como negativos (presiona N en la interfaz)
3. Ejecuta `python train.py` para reentrenar

### **SEMANAS (Opcional - Máxima Precisión)**
1. Recolecta 500-1000 imágenes de pulpos más variadas
2. Recolecta 500-1000 negativos duros (cosas que se parecen pero NO son)
3. Reentrana con `python train.py --epochs 150 --model-size s`

---

## 🔬 Ajustes Técnicos Avanzados

### En `OnnxDetector.h` (si necesitas más control):

```cpp
// Umbral de IoU para NMS (reducir = menos duplicados, menos falsos)
// Valor actual: 0.45
m_nmsThreshold = 0.4f;  // Más agresivo, elimina más duplicados
```

### En `antipop.cfg`:

```ini
# Expandir area de censura (no afecta detección, solo visual)
censor_expansion = 0.15   # Actual: 15% más grande
censor_expansion = 0.05   # Menos expansión visual
```

---

## ✅ Checklist de Mejora

- [ ] Recompilé AntiPop con temporal smoothing
- [ ] Cambié `confidence_threshold` a 0.75 en antipop.cfg
- [ ] Probé y funcionan mejor los parpadeos
- [ ] Ejecuté `prepare_hard_negatives.py --auto-detect`
- [ ] Etiqueté 20+ falsos positivos
- [ ] Ejecuté `python train.py` para reentrenar
- [ ] Probé el nuevo modelo y mejoró la precisión

---

## 📞 Preguntas Frecuentes

**P: ¿Por qué sigue censurando cosas raras?**
R: Porque el modelo fue entrenado con pocas imágenes sin negativos duros. Agrega hard negatives y reentrana.

**P: ¿Qué es un "hard negative"?**
R: Una imagen que el modelo confunde con pulpo pero NO es pulpo (ej: tentáculos de planta marina, medusas, etc).

**P: ¿Cuántas imágenes necesito?**
R: Mínimo 2000 (850 positivos + 150 negativos). Mejor: 3000+ variadas.

**P: ¿Puedo entrenar en CPU?**
R: Sí, pero tarda 2-3h. Con GPU tarda 20 min.

**P: ¿Cómo sé si mejoró?**
R: Prueba la app visualmente. Si ve menos falsos positivos, funcionó.

---

**Última actualización**: 2026-03-01
**Autor**: AntiPop Improvement Guide
