# 📘 GUÍA COMPLETA: Cómo funciona Microcontrolador.ino

## 🎯 RESUMEN GENERAL

**Stiffio** es un dispositivo para medir rigidez arterial mediante **PWV (Pulse Wave Velocity)** y **HR (Heart Rate)**. Usa dos sensores MAX30102 de fotopletismografía (PPG) colocados en:
- **Sensor 1 (S1)**: Carótida (cuello)
- **Sensor 2 (S2)**: Muñeca (arteria radial)

El sistema calcula el **tiempo de tránsito del pulso (PTT)** entre ambos puntos y estima la velocidad de onda de pulso.

---

## 🏗️ ARQUITECTURA DEL SISTEMA

### **Hardware:**
- **ESP32** (dual-core)
- **Pantalla TFT 480x320** táctil (ILI9488 + XPT2046)
- **2× MAX30102** (sensores PPG por I²C)
- **WiFi** (para exportar datos)

### **Software:**
- **Core 0**: `TaskSensores()` - Captura continua de datos PPG a 400Hz
- **Core 1**: `loop()` - Interfaz UI, gráficos, touch

---

## 📊 FLUJO DE DATOS COMPLETO

```
┌─────────────────────────────────────────────────────────────┐
│ 1. CAPTURA (Core 0 - TaskSensores @ 400Hz)                 │
├─────────────────────────────────────────────────────────────┤
│ MAX30102 Carótida → IR1 (raw)                               │
│ MAX30102 Muñeca   → IR2 (raw)                               │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. FILTRADO (cascada 4 etapas)                              │
├─────────────────────────────────────────────────────────────┤
│ a) Low-Pass Filter (ALPHA_LP = 0.75)                        │
│    └─ Elimina ruido alta frecuencia (>15Hz)                │
│                                                              │
│ b) DC Removal (ALPHA_DC = 0.97)                             │
│    └─ Quita offset lento (drift DC)                        │
│                                                              │
│ c) High-Pass Filter (HPF diferenciado):                     │
│    ├─ S1 (carótida): ALPHA_HP = 0.97 (corte @ 0.3Hz)       │
│    └─ S2 (muñeca):   ALPHA_HP = 0.95 (corte @ 0.5Hz)       │
│    └─ Elimina respiración y deriva térmica                 │
│                                                              │
│ d) Media Móvil (MA_SIZE = 4)                                │
│    └─ Suavizado final sin perder picos                     │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. ALMACENAMIENTO (Buffer circular FIFO)                    │
├─────────────────────────────────────────────────────────────┤
│ buffer_s1[320]    → Señal filtrada S1 (0.8 seg @ 400Hz)    │
│ buffer_s2[320]    → Señal filtrada S2                       │
│ buffer_time[320]  → Timestamps reales (millis())            │
│                                                              │
│ writeHead avanza: 0→1→2→...→319→0 (circular)               │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. DETECCIÓN DE PICOS (algoritmo SparkFun adaptativo)       │
├─────────────────────────────────────────────────────────────┤
│ S1: checkForBeatS1(ir1) → Umbral dinámico sobre IR raw     │
│     └─ Detecta pico sistólico carótida                     │
│                                                              │
│ S2: checkForBeat(ir2)   → Algoritmo SparkFun oficial       │
│     └─ Detecta pico sistólico radial                       │
│                                                              │
│ Validación fisiológica:                                      │
│ - Período refractario: 250-300ms (evita doble detección)   │
│ - Rango HR válido: 40-200 BPM                               │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. CÁLCULO HR (solo sobre S2 - muñeca)                      │
├─────────────────────────────────────────────────────────────┤
│ HR = 60000 / (tiempo entre picos consecutivos S2)           │
│                                                              │
│ Promediado: Buffer de 10 latidos                            │
│ └─ Muestra valor tras 5 latidos (~4-5 seg)                 │
│ └─ Estabiliza tras 10 latidos (~8 seg)                     │
└─────────────────────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. CÁLCULO PWV                                               │
├─────────────────────────────────────────────────────────────┤
│ PTT = buffer_time[idxPeakS2] - buffer_time[idxPeakS1]      │
│       └─ Tiempo de tránsito del pulso (ms)                 │
│                                                              │
│ Distancia = (altura_paciente × 0.436) / 100  [metros]      │
│             └─ Factor calibrado carótida→muñeca            │
│                                                              │
│ PWV = distancia / (PTT / 1000)  [m/s]                       │
│                                                              │
│ Validación: 3.0 < PWV < 50.0 m/s                            │
│ Promediado: Buffer de 10 mediciones                         │
│ └─ Muestra valor tras 2 mediciones (~2-3 seg)              │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. VISUALIZACIÓN (Core 1 - actualizarMedicion)             │
├─────────────────────────────────────────────────────────────┤
│ - Gráfico dual S1/S2 en tiempo real (~100ms refresh)       │
│ - Eje X: Tiempo absoluto progresivo (0s→1s→2s...)          │
│ - Autoscale suave (recalcula cada 1.5s, blend 98/2)        │
│ - HR y PWV mostrados numéricamente                          │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔀 MODOS DE OPERACIÓN

### **A) TEST RÁPIDO** (30 segundos)

```
INICIO
  │
  ├─ Usuario ingresa ALTURA (teclado numérico)
  │
  ├─ Fase 1: ESTABILIZACIÓN (10 segundos)
  │   └─ Filtros se adaptan, elimina artefactos iniciales
  │   └─ Pantalla: "Estabilizando sensores..."
  │
  ├─ Fase 2: MEDICIÓN (30 segundos)
  │   ├─ Gráfico en vivo (S1 roja arriba, S2 rosa abajo)
  │   ├─ HR aparece tras 5 latidos (~4-5 seg)
  │   ├─ PWV aparece tras 2 mediciones válidas (~2-3 seg)
  │   └─ Promedios se estabilizan progresivamente
  │
  └─ FIN
      └─ Pantalla resultados: HR final, PWV final
      └─ Exportar por WiFi (opcional)
```

**Características:**
- **Rápido**: Solo ingresa altura
- **Sin registro**: No guarda en SD
- **Ideal para**: Pruebas rápidas, demos

---

### **B) ESTUDIO CLÍNICO** (60 segundos)

```
INICIO
  │
  ├─ Usuario ingresa DATOS COMPLETOS:
  │   ├─ Nombre
  │   ├─ Edad
  │   ├─ Altura
  │   ├─ Peso
  │   └─ Observaciones
  │
  ├─ Fase 1: ESTABILIZACIÓN (10 segundos)
  │
  ├─ Fase 2: MEDICIÓN (60 segundos)
  │   ├─ Mismo proceso que Test Rápido
  │   ├─ Más tiempo = mayor estabilidad en promedios
  │   └─ Exporta datos crudos completos por WiFi
  │
  └─ FIN
      ├─ Guarda en SD: timestamp, datos paciente, HR, PWV, waveforms
      └─ Pantalla resultados + ID de guardado
```

**Características:**
- **Completo**: Registro detallado
- **Exportable**: JSON con datos crudos para análisis en Python
- **Ideal para**: Estudios clínicos, validación

---

## 🔧 CAMBIOS RECIENTES IMPLEMENTADOS

### **1. TIMING PRECISO (Crítico para PWV)**

**Antes:**
```cpp
buffer_time[writeHead] = (writeHead * 1000) / 400;  // ❌ Asumía muestreo uniforme
```

**Ahora:**
```cpp
unsigned long sampleTimestamp = millis();  // ✓ Timestamp REAL cuando llega muestra
buffer_time[writeHead] = sampleTimestamp - baseTime;
```

**Por qué importa:**
- MAX30102 tiene jitter interno (±0.3-0.5ms/muestra)
- Para PWV, error de 2ms → error del 10% en resultado
- Timestamps reales garantizan PTT preciso

---

### **2. FILTRADO ANTI-DERIVA (Mejora visual)**

**Antes:**
- Solo LP filter + DC removal
- Deriva visible en pantalla (línea base ondulante)

**Ahora - Cascada de 4 filtros:**

| Filtro | Parámetro | Función |
|--------|-----------|---------|
| **Low-Pass** | ALPHA_LP = 0.75 | Suaviza ruido >15Hz |
| **DC Removal** | ALPHA_DC = 0.97 | Elimina offset lento |
| **High-Pass Diferenciado** | S1: 0.97, S2: 0.95 | Quita respiración (0.2-0.5Hz) |
| **Media Móvil** | MA_SIZE = 4 | Suavizado final |

**Resultado:**
- ✅ Línea base estable (sin deriva)
- ✅ Picos nítidos y definidos
- ✅ S2 (muñeca) perfecto, S1 (carótida) mejorado

---

### **3. DETECCIÓN HR MEJORADA (Algoritmo SparkFun)**

**Antes:**
```cpp
if (deltaSlope > THRESHOLD) { /* detectar pico */ }  // ❌ Umbral fijo
```

**Ahora:**
```cpp
if (checkForBeat(ir2)) { /* pico detectado */ }  // ✓ Algoritmo adaptativo
```

**Ventajas SparkFun:**
- Umbral dinámico (se adapta a amplitud de señal)
- Validación de flancos (evita falsos positivos)
- Período refractario inteligente
- HR aparece tras **5 latidos** (~4-5 seg), estabiliza en 10

---

### **4. CÁLCULO PWV OPTIMIZADO**

**Antes:**
- Esperaba 10 mediciones para mostrar (~10-12 seg)
- Detección S1 por slope simple (poco robusta)

**Ahora:**
- `checkForBeatS1()` con algoritmo adaptativo
- LED carótida aumentado (20→30) para mejor señal
- Muestra PWV tras **2 mediciones válidas** (~2-3 seg)
- Estabiliza progresivamente hasta 10 muestras

**Fórmula (sin cambios):**
```cpp
distancia = (altura × 0.436) / 100;  // metros (calibrado carótida→muñeca)
PWV = distancia / (PTT / 1000);      // m/s
```

---

## 📈 PARÁMETROS CLAVE ACTUALES

```cpp
// Buffers
#define BUFFER_SIZE 320        // 0.8 seg @ 400Hz (~1 latido visible)
#define SAMPLE_RATE 400        // Hz real del MAX30102
#define AVG_SIZE 10            // Promedio de HR/PWV sobre 10 latidos

// Tiempos
TIEMPO_ESTABILIZACION = 10000  // 10 segundos inicio
TIEMPO_MEDICION_RAPIDA = 30000 // 30 seg test rápido
TIEMPO_MEDICION_COMPLETA = 60000 // 60 seg estudio clínico

// Filtros
ALPHA_LP = 0.75                // Low-pass
ALPHA_DC = 0.97                // DC removal
ALPHA_HP_S1 = 0.97             // HPF carótida (0.3Hz)
ALPHA_HP_S2 = 0.95             // HPF muñeca (0.5Hz)
MA_SIZE = 4                    // Media móvil

// Detección
BEAT_THRESHOLD = 20.0          // Backup S1 (si falla algoritmo)
REFRACTORY_PERIOD = 350        // ms entre latidos

// LEDs
sensorProx (carótida): 30      // Aumentado de 20
sensorDist (muñeca): 30        // Sin cambios

// Validación
HR válido: 40-200 BPM
PWV válido: 3.0-50.0 m/s
PTT válido: 20-400 ms
```

---

## 🎓 CONCEPTOS CLAVE

### **¿Por qué 2 sensores?**
El **PWV** mide rigidez arterial calculando la velocidad de propagación del pulso cardíaco. Mayor rigidez → pulso viaja más rápido → mayor PWV → mayor riesgo cardiovascular.

### **¿Por qué carótida y muñeca?**
- **Distancia conocida** (~35-45cm según altura)
- **Arterias centrales** (representan stiffness aórtica)
- **No invasivo** (vs. cateterización)

### **¿Por qué tanto filtrado?**
PPG tiene múltiples fuentes de ruido:
- Movimiento corporal
- Respiración (0.2-0.3 Hz)
- Deriva térmica del LED
- Ruido eléctrico (50/60 Hz)

Sin filtrado → falsos picos → HR/PWV incorrectos.

---

## 🚀 PRÓXIMOS PASOS

1. ✅ Timing preciso - **HECHO**
2. ✅ Filtrado robusto - **HECHO**
3. ✅ HR con SparkFun - **HECHO**
4. ✅ PWV optimizado - **HECHO**
5. ⏳ Validación clínica (comparar con gold standard)
6. ⏳ Exportación Python para análisis avanzado

---

## 📚 REFERENCIAS

- **MAX30102**: Maxim Integrated. "MAX30102 High-Sensitivity Pulse Oximeter and Heart-Rate Sensor for Wearable Health"
- **PWV**: Laurent S, et al. (2006). "Expert consensus document on arterial stiffness: methodological issues and clinical applications"
- **SparkFun Algorithm**: Nathan Seidle. "Optical Heart Rate Detection (PBA Algorithm)" - SparkFun Electronics

---

**Generado:** Febrero 2026  
**Proyecto:** Stiffio - Instrumentación Biomédica II - ITBA  
**Autores:** Equipo Stiffio

---
