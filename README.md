# STIFFIO - Sistema de Medición de Rigidez Arterial

## 📋 Descripción del Proyecto

**STIFFIO** es un sistema integrado de medición de la velocidad de onda de pulso (PWV - Pulse Wave Velocity) diseñado para evaluar la rigidez arterial. Este dispositivo médico combina hardware embebido, procesamiento de señales y una interfaz táctil intuitiva para proporcionar análisis no invasivo de la salud cardiovascular.

El proyecto fue desarrollado como trabajo final de la cátedra de **Instrumentación Biomédica II** del ITBA.

### 👥 Autores
- **Catalina Jonquieres**
- **Victoria Orsi**
- **Daniela Gluj**

---

## 🎯 Características Principales

### Hardware
- **Microcontrolador**: ESP32 con conectividad WiFi integrada
- **Sensores**: 2 sensores MAX30102 para detección de pulso (carótida y radial)
- **Pantalla**: Display TFT táctil 4" (320RGB x 480 píxeles) con controlador ST7796S
- **Interfaz Táctil**: Capacitiva resistiva integrada
- **Muestreo**: 50 Hz de frecuencia de muestreo
- **Filtrado**: Filtros pasa-banda personalizados (0.5 Hz - 5.0 Hz)

### Software
- **Microcontrolador** (`Microcontrolador.ino`): 
  - Comunicación WebSocket con la PC
  - Lectura sincronizada de sensores
  - Cálculo de frecuencia cardíaca (HR)
  - Visualización en tiempo real en pantalla TFT táctil 320x480
  - Interfaz táctil responsiva con botones y gráficos embebidos

- **Backend** (`BackEnd.py`):
  - Procesamiento de señales con filtros exponenciales (EMA)
  - Detección de picos de onda de pulso
  - Cálculo de PWV basado en correlación temporal
  - Validación de datos fisiológicos

- **Frontend** (`FrontEnd.py`):
  - Interfaz gráfica con PyQt6
  - Visualización de señales en tiempo real con PyQtGraph
  - Gestión de datos de pacientes
  - Historial de mediciones con búsqueda y filtrado
  - Exportación de reportes en PDF
  - Gráfico comparativo PWV vs Edad (referencia DOI:10.1155/2014/653239)

---

## 🔧 Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────┐
│            ESP32 Microcontrolador                   │
│  • MAX30102 Sensor 1 (Proximal - Carótida)        │
│  • MAX30102 Sensor 2 (Distal - Radial)            │
│  • TFT ST7796S 4" 320x480 (Visualización táctil)  │
│  • WiFi WebSocket Server (Puerto 81)              │
└──────────────────┬──────────────────────────────────┘
                   │ WebSocket
                   ▼
┌─────────────────────────────────────────────────────┐
│        PC - Aplicación Python                       │
├─────────────────────────────────────────────────────┤
│ ComunicacionMax.py: Gestor de WebSocket            │
│ BackEnd.py: Procesamiento de señales               │
│ FrontEnd.py: Interfaz gráfica (PyQt6)             │
└─────────────────────────────────────────────────────┘
```

---

## 📊 Algoritmo de Cálculo de PWV

1. **Adquisición**: Lectura sincronizada de ambos sensores a 50 Hz
2. **Filtrado**: Aplicación de filtro pasa-banda (0.5-5 Hz) en el firmware
3. **Procesamiento**: Filtro exponencial adicional en backend
4. **Detección de Picos**: Identificación de máximos locales en ambas señales
5. **Correlación**: Cálculo del tiempo de propagación entre sensores
6. **Validación**: Verificación del rango fisiológico (2-15 m/s)
7. **Promediado**: Buffer de 100 muestras para estabilización

**Fórmula**: PWV = Distancia (0.436 × altura) / Δt

---

## 🚀 Instalación y Uso

### Requisitos
- Python 3.8+
- Librerías: PyQt6, PyQtGraph, NumPy, SciPy, websocket-client
- Arduino IDE (para cargar firmware en ESP32)
- ESP32 con drivers CH340 instalados
- Driver ST7796S para pantalla TFT

### Instalación del Software

```bash
# Clonar o descargar el proyecto
cd Stiffio\ 14-01

# Instalar dependencias Python
pip install PyQt6 pyqtgraph numpy scipy websocket-client

# Configurar IP del ESP32 en ComunicacionMax.py
# Modificar: esp_ip = "IP_DEL_ESP32"
```

### Cargar Firmware en ESP32

1. Abrir `Microcontrolador/Microcontrolador.ino` en Arduino IDE
2. Seleccionar Board: ESP32 Dev Module
3. Instalar librerías: TFT_eSPI (configurada para ST7796S), MAX30102
4. Configurar WiFi (SSID y contraseña en el código)
5. Cargar el sketch

### Ejecutar Aplicación

```bash
python FrontEnd.py
```

---

## 📁 Estructura del Proyecto

```
Stiffio 14-01/
├── Microcontrolador/
│   ├── Microcontrolador.ino          # Firmware principal
│   ├── TFT_Config.h                  # Configuración TFT ST7796S
│   ├── User_Setup.h                  # Setup de TFT_eSPI
│   ├── heartRate.h                   # Algoritmo de detección HR
│   └── sensores.h                    # Gestión de sensores MAX30102
├── BackEnd.py                        # Procesamiento de señales
├── FrontEnd.py                       # Interfaz gráfica
├── ComunicacionMax.py                # Comunicación WebSocket
├── mediciones_pwv.csv                # Base de datos de mediciones
└── README.md                         # Este archivo
```

---

## 📈 Pantallas de la Aplicación (TFT 4")

### 1. Pantalla de Bienvenida
- Logo y título STIFFIO
- Botón táctil "NUEVA MEDICIÓN"
- Botón táctil "HISTORIAL"
- Indicador de conexión WiFi
- Resolución: 320x480 píxeles

### 2. Ingreso de Datos del Paciente
- Campos táctiles para: Nombre, edad, altura, sexo
- Teclado virtual integrado
- Validaciones de rangos fisiológicos
- Interfaz táctil optimizada para pantalla 4"

### 3. Pantalla Principal de Medición
- Gráficos en tiempo real de ambos sensores (carótida y radial)
- Visualización de PWV y HR en grande
- Alertas visuales de sensores desconectados
- Botones táctiles de inicio/pausa/parada de medición
- Indicador de estabilidad de señal
- Gráfico comparativo con referencias poblacionales

### 4. Pantalla de Resultados
- Resumen de medición en pantalla TFT
- Botón para guardar medición
- Botón para nueva medición
- Botón para ver historial

### 5. Historial de Mediciones (en PC)
- Tabla con todas las mediciones registradas
- Búsqueda por nombre/paciente
- Filtrado por rango de fechas
- Impresión de reportes en PDF
- Eliminación de registros

---

## 🔬 Especificaciones Técnicas

| Parámetro | Valor |
|-----------|-------|
| Display | TFT ST7796S 4" táctil |
| Resolución | 320RGB x 480 píxeles |
| Colores | 262K (16 bits) |
| Frecuencia de Muestreo | 50 Hz |
| Resolución ADC | 18 bits |
| Rango de HR | 20-255 bpm |
| Rango de PWV | 2-15 m/s |
| Distancia carótida-radial | 0.436 × altura |
| Tiempo de estabilización HR | ~10 latidos |
| Buffer de PWV | 100 muestras (~30 s @ 60 bpm) |

---

## 📝 Almacenamiento de Datos

Las mediciones se guardan en `mediciones_pwv.csv` con el siguiente formato:

```
Fecha y Hora;Nombre;Edad;Altura (cm);Sexo;HR (bpm);PWV (m/s)
2025-11-18 18:54:18;Victoria Orsi;23;168;Femenino;80;3.0
```

---

## ⚠️ Consideraciones Importantes

- **Sincronización de Sensores**: Se limpian buffers automáticamente cuando un sensor se desconecta
- **Validación de Datos**: Solo se aceptan valores dentro de rangos fisiológicos
- **Estabilización**: El HR requiere ~10 latidos válidos antes de estabilizarse
- **Conexión WiFi**: Configurar SSID y contraseña en el firmware antes de cargar
- **Pantalla Táctil**: Requiere calibración inicial - ejecutar rutina de calibración en setup
- **SPI Display**: Configurar pines SPI en `User_Setup.h` según conexión al ESP32

---

## 🔄 Flujo de Datos

1. **Adquisición**: ESP32 lee sensores a 50 Hz
2. **Visualización Local**: Datos mostrados en tiempo real en pantalla TFT 4"
3. **Envío**: Datos se transmiten vía WebSocket en JSON a PC
4. **Recepción**: Python recibe en `ComunicacionMax.py`
5. **Procesamiento**: `BackEnd.py` aplica filtros y calcula PWV
6. **Visualización PC**: `FrontEnd.py` actualiza gráficos y métricas
7. **Almacenamiento**: Datos guardados en CSV al guardar medición

---

## 📚 Referencias

- DOI: 10.1155/2014/653239 - Tabla de referencia PWV vs Edad
- MAX30102 Datasheet
- ST7796S Controller/Driver Datasheet
- TFT_eSPI Library Documentation
- ESP32 Documentation
- PyQt6 Documentation

---

## 📄 Licencia

Proyecto académico - Instituto Tecnológico de Buenos Aires (ITBA)

---

## 📞 Soporte

Para consultas sobre el proyecto, contactar a los autores o revisar la documentación incluida en los comentarios del código fuente.

---

**Última actualización**: Enero 2026
