"""
COMUNICACIONMAX.PY
WebSocket bridge between Python app and ESP32.

Incoming JSON (ESP32 -> Python):
{
  "c1": bool, "c2": bool,
  "s1": bool, "s2": bool,
  "p": float, "d": float,
  "hr": int|null, "pwv": float|null
}

Outgoing JSON (Python -> ESP32):
{"h": int, "a": int}
"""

import json
import threading
import time
from collections import deque

import websocket


# ==============================================================================
# NETWORK CONFIG
# ==============================================================================
# esp_ip = "172.20.10.4"
# esp_ip = "192.168.0.177"
esp_ip = "192.168.0.238"
ws_url = f"ws://{esp_ip}:81/"
WS_PING_INTERVAL_SEC = 12
WS_PING_TIMEOUT_SEC = 6
WS_RECONNECT_DELAY_SEC = 1.0

# Tiempo de muestreo esperado en el frontend (50 Hz visuales)
SAMPLE_DT_SEC = 1.0 / 50.0
SAMPLE_MIN_STEP_SEC = 0.010
SAMPLE_MAX_STEP_SEC = 0.060
SAMPLE_DISCONTINUITY_SEC = 0.300


# ==============================================================================
# SHARED STATE
# ==============================================================================
connected = False
ws_app = None

sensor1_connected = False
sensor2_connected = False
sensor1_ok = False
sensor2_ok = False

MAX_POINTS = 600
sample_time_raw = deque(maxlen=MAX_POINTS)
proximal_data_raw = deque(maxlen=MAX_POINTS)
distal_data_raw = deque(maxlen=MAX_POINTS)

remote_hr = None
remote_pwv = None

_state_lock = threading.Lock()
_send_lock = threading.Lock()
_connection_thread = None
_last_sample_time = None
_sample_seq = 0
_last_rx_monotonic = None


def _to_bool(value, default=False):
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        v = value.strip().lower()
        if v in ("true", "1", "yes", "y", "si"):
            return True
        if v in ("false", "0", "no", "n"):
            return False
    return default


def _to_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _nullable_hr(value):
    if value is None:
        return None
    try:
        v = int(value)
    except (TypeError, ValueError):
        return None
    return v if v > 0 else None


def _nullable_pwv(value):
    if value is None:
        return None
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if v > 0.0 else None


def reset_stream_buffers(reset_metrics=True):
    global remote_hr, remote_pwv, _last_sample_time, _sample_seq
    with _state_lock:
        sample_time_raw.clear()
        proximal_data_raw.clear()
        distal_data_raw.clear()
        _last_sample_time = None
        _sample_seq = 0
        if reset_metrics:
            remote_hr = None
            remote_pwv = None


def get_snapshot():
    with _state_lock:
        return {
            "connected": connected,
            "c1": sensor1_connected,
            "c2": sensor2_connected,
            "s1": sensor1_ok,
            "s2": sensor2_ok,
            "t": list(sample_time_raw),
            "p": list(proximal_data_raw),
            "d": list(distal_data_raw),
            "hr": remote_hr,
            "pwv": remote_pwv,
            "seq": _sample_seq,
            "last_rx": _last_rx_monotonic,
        }


# ==============================================================================
# SEND (Python -> ESP32)
# ==============================================================================
def enviar_datos_paciente(altura_cm, edad):
    global ws_app, connected

    if connected and ws_app:
        try:
            mensaje = json.dumps({"h": int(altura_cm), "a": int(edad)})
            with _send_lock:
                ws_app.send(mensaje)
            return True
        except Exception:
            return False
    return False


# ==============================================================================
# WEBSOCKET EVENTS
# ==============================================================================
def on_open(ws):
    global connected
    with _state_lock:
        connected = True


def on_message(ws, message):
    global sensor1_connected, sensor2_connected, sensor1_ok, sensor2_ok
    global remote_hr, remote_pwv, _last_sample_time, _sample_seq, _last_rx_monotonic

    try:
        data = json.loads(message)
    except json.JSONDecodeError:
        return
    except Exception:
        return

    now = time.monotonic()

    with _state_lock:
        _last_rx_monotonic = now

        # Physical connection flags
        if "c1" in data:
            sensor1_connected = _to_bool(data.get("c1"), sensor1_connected)
        elif "s1" in data:
            # Backward compatibility with old firmware packets
            sensor1_connected = True

        if "c2" in data:
            sensor2_connected = _to_bool(data.get("c2"), sensor2_connected)
        elif "s2" in data:
            sensor2_connected = True

        # On-skin / contact flags
        if "s1" in data:
            sensor1_ok = _to_bool(data.get("s1"), sensor1_ok)
        if "s2" in data:
            sensor2_ok = _to_bool(data.get("s2"), sensor2_ok)

        # Signal samples
        if "p" in data and "d" in data and sensor1_connected and sensor2_connected and sensor1_ok and sensor2_ok:
            p_val = _to_float(data.get("p"), 0.0)
            d_val = _to_float(data.get("d"), 0.0)

            if _last_sample_time is None:
                t_sample = now
            else:
                dt = now - _last_sample_time
                if dt > SAMPLE_DISCONTINUITY_SEC:
                    t_sample = now
                else:
                    dt = max(SAMPLE_MIN_STEP_SEC, min(SAMPLE_MAX_STEP_SEC, dt))
                    if abs(dt - SAMPLE_DT_SEC) <= 0.020:
                        dt = SAMPLE_DT_SEC
                    t_sample = _last_sample_time + dt

            _last_sample_time = t_sample
            _sample_seq += 1
            sample_time_raw.append(t_sample)
            proximal_data_raw.append(p_val)
            distal_data_raw.append(d_val)

        # Metrics already computed by ESP32
        if "hr" in data:
            remote_hr = _nullable_hr(data.get("hr"))
        if "pwv" in data:
            remote_pwv = _nullable_pwv(data.get("pwv"))


def on_error(ws, error):
    global connected, sensor1_connected, sensor2_connected, sensor1_ok, sensor2_ok, remote_hr, remote_pwv
    global ws_app, _last_sample_time, _last_rx_monotonic
    with _state_lock:
        connected = False
        ws_app = None
        sensor1_connected = False
        sensor2_connected = False
        sensor1_ok = False
        sensor2_ok = False
        remote_hr = None
        remote_pwv = None
        _last_sample_time = None
        _last_rx_monotonic = None
        sample_time_raw.clear()
        proximal_data_raw.clear()
        distal_data_raw.clear()


def on_close(ws, close_status_code, close_msg):
    global connected, sensor1_connected, sensor2_connected, sensor1_ok, sensor2_ok, remote_hr, remote_pwv
    global ws_app, _last_sample_time, _last_rx_monotonic
    with _state_lock:
        connected = False
        ws_app = None
        sensor1_connected = False
        sensor2_connected = False
        sensor1_ok = False
        sensor2_ok = False
        remote_hr = None
        remote_pwv = None
        _last_sample_time = None
        _last_rx_monotonic = None
        sample_time_raw.clear()
        proximal_data_raw.clear()
        distal_data_raw.clear()


# ==============================================================================
# THREAD / STARTUP
# ==============================================================================
def run_ws():
    global ws_app
    while True:
        ws_local = websocket.WebSocketApp(
            ws_url,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close,
        )
        with _state_lock:
            ws_app = ws_local
        try:
            ws_local.run_forever(
                ping_interval=WS_PING_INTERVAL_SEC,
                ping_timeout=WS_PING_TIMEOUT_SEC,
            )
        except Exception:
            with _state_lock:
                ws_app = None
        time.sleep(WS_RECONNECT_DELAY_SEC)


def start_connection():
    global _connection_thread
    if _connection_thread is not None and _connection_thread.is_alive():
        return
    _connection_thread = threading.Thread(target=run_ws, daemon=True)
    _connection_thread.start()


if __name__ == "__main__":
    start_connection()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
