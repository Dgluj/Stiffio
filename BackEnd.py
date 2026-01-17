"""BACKEND.PY
Procesamiento de las señales para el cálculo de PWV"""


# =================================================================================================
# Librerías
# =================================================================================================
import numpy as np
from collections import deque
from scipy.signal import find_peaks
import ComunicacionMax

class SignalProcessor:
    def __init__(self, fs=50):
        self.fs = fs
        self.MAX_POINTS = 300
        
        # Buffers de datos crudos (vienen FILTRADOS Pasa-Banda desde el .ino)
        self.proximal_raw = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)
        self.distal_raw = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)

        # Buffers para tu filtro exponencial (EMA)
        self.proximal_filtered = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)
        self.distal_filtered = deque([0]*self.MAX_POINTS, maxlen=self.MAX_POINTS)
        
        # Variables de cálculo
        self.pwv = None
        self.height_m = 1.75  # altura default si no se envia nada
        self.pwv_buffer = deque(maxlen=100)  # ~30 s con HR 60

    def set_height_from_frontend(self, h_m):
        self.height_m = float(h_m)
        print(f"[Backend] Altura actualizada a {self.height_m} m")

    def clear_buffers(self):
        """
        Limpia todos los buffers y resetea el cálculo de PWV.
        Se debe llamar cuando se detecta desconexión de sensores para mantener sincronización.
        """
        self.proximal_raw.clear()
        self.distal_raw.clear()
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.pwv_buffer.clear()
        self.pwv = None
        print("[Backend] Buffers limpiados por desconexión de sensores")

    def update_data(self):
        """Actualiza datos desde el módulo de comunicación"""
        if hasattr(ComunicacionMax, 'proximal_data_raw') and len(ComunicacionMax.proximal_data_raw) > 10:
            self.proximal_raw.clear()
            self.proximal_raw.extend(ComunicacionMax.proximal_data_raw)
            
        if hasattr(ComunicacionMax, 'distal_data_raw') and len(ComunicacionMax.distal_data_raw) > 10:
            self.distal_raw.clear()
            self.distal_raw.extend(ComunicacionMax.distal_data_raw)


    def filter_signals(self):
        """
        Tu filtro exponencial (EMA) con MÍNIMO RETARDO.
        Lee de _raw y escribe en _filtered.
        """
        alpha = 0.3
        
        # Filtrar señal proximal
        if len(self.proximal_raw) > 10:
            signal = np.array(self.proximal_raw)
            smoothed = np.zeros_like(signal)
            smoothed[0] = signal[0]
            
            for i in range(1, len(signal)):
                smoothed[i] = alpha * signal[i] + (1 - alpha) * smoothed[i-1]
            
            self.proximal_filtered.clear()
            self.proximal_filtered.extend(smoothed)
        
        # Filtrar señal distal
        if len(self.distal_raw) > 10:
            signal = np.array(self.distal_raw)
            smoothed = np.zeros_like(signal)
            smoothed[0] = signal[0]
            
            for i in range(1, len(signal)):
                smoothed[i] = alpha * signal[i] + (1 - alpha) * smoothed[i-1]
            
            self.distal_filtered.clear()
            self.distal_filtered.extend(smoothed)


    def detect_wave_peaks(self, signal):
        """
        Detección robusta de PICOS (máximos) de onda PPG reflejada.
        La señal del MAX30102 es REFLEJADA, por lo que los MÁXIMOS corresponden
        a los eventos sistólicos (no los mínimos como en señales de absorción).
        """
        signal = np.asarray(signal)
        if len(signal) < 3:
            return []
        
        # 1. Calcular rango de la señal
        sig_range = np.max(signal) - np.min(signal)
        if sig_range < 1e-6:
            return []
        
        # 2. Detección de Picos (MÁXIMOS) - prominencia: 25% del rango
        prom_threshold = np.std(signal) * 0.25
        dist_threshold = int(0.4 * self.fs)  # 150 bpm max (0.4 s entre latidos)

        # Buscar MÁXIMOS (peaks) directamente
        peaks, properties = find_peaks(signal, 
                                       distance=dist_threshold, 
                                       prominence=prom_threshold)
        
        if len(peaks) < 2:
            return []

        # 3. Filtrar picos demasiado juntos (<80 ms)
        min_dist = int(0.08 * self.fs)  # 80 ms
        peaks_filtered = [peaks[0]]  # siempre guardamos el primero

        for p in peaks[1:]:
            if p - peaks_filtered[-1] > min_dist:
                peaks_filtered.append(p)

        return sorted(peaks_filtered)
    

    def calculate_pwv(self):
        """
        Calcula la PWV usando la señal FILTRADA (EMA) y detección de MÁXIMOS.
        La señal es REFLEJADA, por lo que correlacionamos MÁXIMOS entre distal y proximal.
        """
        
        # Ahora leemos de los buffers FILTRADOS
        if len(self.proximal_filtered) < self.MAX_POINTS or len(self.distal_filtered) < self.MAX_POINTS:
            return None
        
        sig_prox = np.array(self.proximal_filtered)
        sig_dist = np.array(self.distal_filtered)
        
        # 2. DETECTAR MÁXIMOS (picos) en ambas señales (señal reflejada)
        peaks_prox = self.detect_wave_peaks(sig_prox)
        peaks_dist = self.detect_wave_peaks(sig_dist)
        
        if len(peaks_prox) < 2 or len(peaks_dist) < 2:
            return None
        
        # 3. Convertir índices a tiempos
        tiempos_prox = np.array(peaks_prox) / self.fs
        tiempos_dist = np.array(peaks_dist) / self.fs
        
        # 4. Correlacionar máximos: encontrar el máximo distal que corresponde a cada máximo proximal
        # El distal debe llegar DESPUÉS del proximal (PWTT positivo)
        deltas = []
        j = 0
        for t_p in tiempos_prox:
            # Buscar el primer máximo distal que ocurre DESPUÉS del máximo proximal
            while j < len(tiempos_dist) and tiempos_dist[j] <= t_p:
                j += 1
            if j < len(tiempos_dist):
                delta = tiempos_dist[j] - t_p  # Tiempo de tránsito (PWTT)
                if 0.01 <= delta <= 0.4:  # Rango fisiológico para carótida-radial (10ms a 400ms)
                    deltas.append(delta)

        if not deltas:
            return None
        
        # 5. Calcular PWV usando la distancia estimada
        distancia = 0.436 * self.height_m  # Distancia carótida-radial estimada
        
        for delta_t in deltas:
            pwv_value = distancia / delta_t
            if 2.0 <= pwv_value <= 15.0:  # Rango fisiológico de PWV (m/s)
                self.pwv_buffer.append(pwv_value)

        # 6. Promediar solo cuando el buffer esté lleno
        self.pwv = (
            float(np.mean(self.pwv_buffer))
            if len(self.pwv_buffer) == self.pwv_buffer.maxlen
            else None
        )
        return self.pwv


    def process_all(self):
        """
        Procesa datos crudos y calcula la PWV.
        Si algún sensor no está OK, limpia buffers y corta la medición.
        """
        # Verificar estado de sensores antes de procesar
        if not (ComunicacionMax.sensor1_ok and ComunicacionMax.sensor2_ok):
            # Si algún sensor no está OK, limpiar buffers y no procesar
            self.clear_buffers()
            return
        
        # 1. Trae los últimos datos del microcontrolador
        self.update_data()
        # --- AÑADIDO ---
        # 1. Actualiza los datos crudos
        # 2. Aplica tu filtro exponencial (EMA) 
        self.filter_signals() # (Esta función ya tiene su propio check "if > 10" adentro)
        # 3. Calcula PWV sobre las señales filtradas
        self.calculate_pwv() # (Esta función ya tiene su check "if < MAX_POINTS" adentro)
    

    def get_signals(self):
        """Retorna las señales FILTRADAS (EMA) para graficar"""
        time_axis = np.linspace(0, len(self.proximal_filtered)/self.fs, len(self.proximal_filtered))
        return (
            time_axis,
            # Devolvemos las filtradas para que la gráfica se vea suave
            list(self.proximal_filtered),
            list(self.distal_filtered)
        )
    

    def get_metrics(self):
        """Retorna las métricas calculadas"""
        return {'pwv': self.pwv}


# Instancia global
processor = SignalProcessor()