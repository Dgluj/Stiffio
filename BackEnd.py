"""
BACKEND.PY
Procesamiento de datos.
ACTUALIZACIÓN: Delega el cálculo matemático al ESP32.
Solo gestiona buffers para visualización.
"""

import numpy as np
from collections import deque
import ComunicacionMax

class SignalProcessor:
    def __init__(self, fs=50):
        self.fs = fs
        self.MAX_POINTS = 300
        
        # Buffers para gráficas (vienen del WebSocket)
        self.proximal_filtered = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)
        self.distal_filtered = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)
        
        # Variables de resultados (se actualizan desde el ESP32)
        self.pwv = 0.0
        self.hr = 0
        
        # Buffer simple para suavizar visualmente el PWV si varía muy rápido en pantalla
        self.pwv_buffer = deque(maxlen=20) 

    def clear_buffers(self):
        """Limpia todo si se pierde conexión o dedos"""
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.pwv = 0.0
        self.hr = 0
        self.pwv_buffer.clear()

    def update_data(self):
        """
        Sincroniza los buffers locales con los datos que llegan al WebSocket.
        """
        # Copiamos los datos que ComunicacionMax recibió del ESP32
        # Convertimos a lista para manejarlo fácil en pyqtgraph
        self.proximal_filtered = list(ComunicacionMax.proximal_data_raw)
        self.distal_filtered = list(ComunicacionMax.distal_data_raw)

    def process_all(self):
        """
        Ciclo principal de procesamiento.
        Ahora es pasivo: solo lee lo que calculó el ESP32.
        """
        # 1. Verificar si hay conexión y sensores OK
        if not (ComunicacionMax.sensor1_ok and ComunicacionMax.sensor2_ok):
            self.clear_buffers()
            return
        
        # 2. Traer señales nuevas para el gráfico
        self.update_data()
        
        # 3. LEER RESULTADOS DEL ESP32 (La Magia)
        # El ESP32 manda el HR y PWV ya calculados.
        
        if ComunicacionMax.remote_hr > 0:
            self.hr = ComunicacionMax.remote_hr
            
        if ComunicacionMax.remote_pwv > 0:
            # Filtro suave local para que el número en pantalla no salte decimales a lo loco
            # (Promedio móvil simple de lo que manda el ESP)
            self.pwv_buffer.append(ComunicacionMax.remote_pwv)
            self.pwv = sum(self.pwv_buffer) / len(self.pwv_buffer)

    def get_signals(self):
        """Retorna ejes X e Y para graficar en FrontEnd"""
        # Generar eje temporal relativo para el gráfico
        length = len(self.proximal_filtered)
        if length > 0:
            time_axis = np.linspace(0, length/self.fs, length)
            return time_axis, self.proximal_filtered, self.distal_filtered
        else:
            return [], [], []

# Instancia global que usará el FrontEnd
processor = SignalProcessor()