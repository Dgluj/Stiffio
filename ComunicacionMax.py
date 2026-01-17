"""COMUNICACIONMAX.PY - 2 sensores
Este código maneja la comunicación entre el ESP32 y la PC vía WebSocket"""


# =================================================================================================
# Librerías
# =================================================================================================
import websocket
import threading
from collections import deque
import json

# =================================================================================================
# Configuración WebSocket
# =================================================================================================
# Cambiar según el código del ESP (ver código de Arduino)
#esp_ip = "192.168.0.179"  # Vitu 
#esp_ip = "192.168.0.238" # Dani
esp_ip = "172.20.10.4" # iPhone Vitu
ws_url = f"ws://{esp_ip}:81/"

# Estado de conexión
connected = False


# =================================================================================================
# Chequeo de Sensores
# =================================================================================================
sensor1_ok = False
sensor2_ok = False


# =================================================================================================
# Parámetros y Variables
# =================================================================================================
MAX_POINTS = 300 # Buffer de la señal
proximal_data_raw = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)
distal_data_raw = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)
hr_avg = None  # BPM promedio

# =================================================================================================
# Callbacks
# =================================================================================================
def on_open(ws):
    global connected
    print("Conectado al ESP32")
    connected = True


def on_message(ws, message):
    global hr_avg, sensor1_ok, sensor2_ok
    
    # Imprimir el mensaje crudo para ver qué llega
    # print("Mensaje recibido:", message)

    try:
        data = json.loads(message)

        # Chequear estado de sensores (soporta formato antiguo y nuevo abreviado)
        if 'sensor1_ok' in data:
            sensor1_ok = bool(data['sensor1_ok'])
        elif 's1' in data:
            sensor1_ok = bool(data['s1'])
            
        if 'sensor2_ok' in data:
            sensor2_ok = bool(data['sensor2_ok'])
        elif 's2' in data:
            sensor2_ok = bool(data['s2'])

        # Actualizar estado de sensores (formato antiguo)
        sensor1_ok = data.get('sensor1', sensor1_ok)
        sensor2_ok = data.get('sensor2', sensor2_ok)

        # Leer datos de señal (soporta formato antiguo y nuevo abreviado)
        if 'proximal' in data:
            proximal_data_raw.append(data['proximal'])
        elif 'p' in data:
            proximal_data_raw.append(data['p'])
            
        if 'distal' in data:
            distal_data_raw.append(data['distal'])
        elif 'd' in data:
            distal_data_raw.append(data['d'])
            
        # Si ambos sensores están apoyados, actualizar HR; si no, anularlo
        if sensor1_ok and sensor2_ok:
            if 'avgBPM' in data:
                hr_avg = data['avgBPM']
            elif 'hr' in data:
                hr_avg = data['hr']
        else:
            hr_avg = None
            # LIMPIAR BUFFERS cuando algún sensor no está OK para mantener sincronización
            # Esto asegura que cuando se reconecte, no haya datos desfasados
            proximal_data_raw.clear()
            distal_data_raw.clear()

    except json.JSONDecodeError:
        print("Mensaje no válido recibido:", message)


def on_error(ws, error):
    global connected
    print("Error WebSocket:", error)
    connected = False


def on_close(ws, close_status_code, close_msg):
    global connected
    print("Conexión cerrada")
    connected = False


# =================================================================================================
# Hilo WebSocket
# =================================================================================================
def run_ws():
    ws = websocket.WebSocketApp(
        ws_url,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close
    )
    ws.run_forever()

# Función para arrancar comunicación
def start():
    threading.Thread(target=run_ws, daemon=True).start()
    print(f"Iniciando conexión a {ws_url}")