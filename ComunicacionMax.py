"""
COMUNICACIONMAX.PY
Maneja la conexión WebSocket con el ESP32.
Recibe: Señales (p, d), Ritmo (hr), Velocidad de Onda (pwv).
Envía: Datos de paciente (Altura, Edad) para el cálculo.
"""

import websocket
import threading
from collections import deque
import json
import time

# ==============================================================================
# CONFIGURACIÓN DE RED (¡CAMBIAR ESTO SEGÚN LA RED!)
# ==============================================================================
# Esta IP es la que muestra la pantalla del ESP32 al conectarse (letras verdes).
# Tus amigas deben cambiar esto por lo que vean en SU pantalla.
esp_ip = "192.168.137.86"  
ws_url = f"ws://{esp_ip}:81/"

# ==============================================================================
# VARIABLES GLOBALES Y BUFFERS
# ==============================================================================
# Estado de conexión
connected = False
ws_app = None  # Objeto del socket para poder enviar datos

# Estado de sensores (True si el dedo está puesto)
sensor1_ok = False
sensor2_ok = False

# Buffers de señales (Compartidos con BackEnd)
MAX_POINTS = 300 
proximal_data_raw = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)
distal_data_raw = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)

# Resultados que vienen calculados desde el ESP32 (Modo Estudio Completo)
remote_hr = 0
remote_pwv = 0.0

# ==============================================================================
# FUNCIONES DE ENVÍO (PC -> ESP32)
# ==============================================================================
def enviar_datos_paciente(altura_cm, edad):
    """
    Envía la altura y edad al ESP32 para que él haga el cálculo interno.
    Se llama desde FrontEnd.py al dar 'Iniciar'.
    """
    global ws_app, connected
    
    if connected and ws_app:
        try:
            # Creamos el JSON: {"h": 170, "a": 25}
            mensaje = json.dumps({"h": int(altura_cm), "a": int(edad)})
            ws_app.send(mensaje)
            print(f"--> Datos enviados al ESP32: Altura={altura_cm}, Edad={edad}")
            return True
        except Exception as e:
            print(f"Error enviando datos: {e}")
            return False
    else:
        print("No se puede enviar: ESP32 no conectado.")
        return False

# ==============================================================================
# EVENTOS WEBSOCKET (ESP32 -> PC)
# ==============================================================================
def on_open(ws):
    global connected
    print(f"--- CONEXIÓN ESTABLECIDA CON ESP32 ({esp_ip}) ---")
    connected = True

def on_message(ws, message):
    global sensor1_ok, sensor2_ok, remote_hr, remote_pwv
    
    try:
        # El ESP32 envía JSON. Ej: {"s1":true, "s2":true, "p":1200, "d":1300, "hr":75, "pwv":6.5}
        data = json.loads(message)

        # 1. Actualizar estado de sensores
        if 's1' in data: sensor1_ok = data['s1']
        if 's2' in data: sensor2_ok = data['s2']

        # 2. Procesar datos (Solo si ambos sensores están OK)
        if sensor1_ok and sensor2_ok:
            # Señales filtradas (p=proximal, d=distal)
            if 'p' in data: proximal_data_raw.append(data['p'])
            if 'd' in data: distal_data_raw.append(data['d'])
            
            # Resultados calculados por el ESP32
            if 'hr' in data: remote_hr = data['hr']
            if 'pwv' in data: remote_pwv = data['pwv']
        else:
            # Si se levanta un dedo, limpiamos buffers para evitar desfasajes en el gráfico
            # y reseteamos valores para que la interfaz muestre "--" o 0
            if len(proximal_data_raw) > 0: proximal_data_raw.clear()
            if len(distal_data_raw) > 0: distal_data_raw.clear()
            remote_hr = 0
            remote_pwv = 0.0

    except json.JSONDecodeError:
        print("Mensaje basura recibido:", message)
    except Exception as e:
        print("Error procesando mensaje:", e)

def on_error(ws, error):
    global connected
    print("Error WebSocket:", error)
    connected = False

def on_close(ws, close_status_code, close_msg):
    global connected
    print("Conexión cerrada")
    connected = False

# ==============================================================================
# HILO DE EJECUCIÓN
# ==============================================================================
def run_ws():
    global ws_app
    # websocket.enableTrace(True) # Descomentar para ver logs de red en consola
    ws_app = websocket.WebSocketApp(
        ws_url,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close
    )
    ws_app.run_forever()

def start_connection():
    """Inicia el hilo de conexión en segundo plano"""
    t = threading.Thread(target=run_ws)
    t.daemon = True # El hilo muere si cierras la app principal
    t.start()