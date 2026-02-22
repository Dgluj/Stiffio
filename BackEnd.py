"""
BACKEND.PY
Visual-only processing pipeline for signal display.

- Receives signals from ESP32 through ComunicacionMax.
- Uses fixed normalization: (ir / 262143.0) * 100.0
- Uses a fixed 6-second FIFO display window.
- Starts plotting only after the 6-second buffer is complete.
- Does NOT compute HR or PWV (those come from ESP32 JSON).
"""

from collections import deque
import math
import time

import ComunicacionMax


class SignalProcessor:
    def __init__(self, fs=50):
        self.fs = fs
        self.VIEW_SECONDS = 6.0
        self.MAX_POINTS = max(2, int(round(self.fs * self.VIEW_SECONDS)))
        self.MAX_ADC = 262143.0
        self.DISPLAY_GAIN = 1000.0

        self.proximal_filtered = deque(maxlen=self.MAX_POINTS)
        self.distal_filtered = deque(maxlen=self.MAX_POINTS)
        self.time_axis = deque(maxlen=self.MAX_POINTS)

        self.hr = None
        self.pwv = None

        self.connected = False
        self.c1 = False
        self.c2 = False
        self.s1 = False
        self.s2 = False

        self.session_active = False
        self.session_start_local = None
        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0

    def start_session(self):
        self.session_active = True
        self.session_start_local = time.monotonic()
        self.hr = None
        self.pwv = None
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0

    def stop_session(self):
        self.session_active = False

    def clear_buffers(self):
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self.hr = None
        self.pwv = None
        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0

    def _update_status(self, snapshot):
        self.connected = bool(snapshot.get("connected", False))
        self.c1 = bool(snapshot.get("c1", False))
        self.c2 = bool(snapshot.get("c2", False))
        self.s1 = bool(snapshot.get("s1", False))
        self.s2 = bool(snapshot.get("s2", False))
        self.hr = self._coerce_optional_int(snapshot.get("hr", None))
        self.pwv = self._coerce_optional_float(snapshot.get("pwv", None))

    def _coerce_optional_int(self, value):
        if value is None:
            return None
        try:
            v = int(float(value))
        except (TypeError, ValueError):
            return None
        return v if v > 0 else None

    def _coerce_optional_float(self, value):
        if value is None:
            return None
        if isinstance(value, str):
            value = value.strip().replace(",", ".")
        try:
            v = float(value)
        except (TypeError, ValueError):
            return None
        if (not math.isfinite(v)) or v <= 0.0:
            return None
        return v

    def _normalize_fixed(self, values):
        out = []
        for v in values:
            norm = (float(v) / self.MAX_ADC) * 100.0
            norm *= self.DISPLAY_GAIN
            if norm > 100.0:
                norm = 100.0
            elif norm < -100.0:
                norm = -100.0
            out.append(norm)
        return out

    def process_all(self):
        if not self.session_active:
            return

        snapshot = ComunicacionMax.get_snapshot()
        self._update_status(snapshot)

        raw_t = snapshot.get("t", [])
        raw_p = snapshot.get("p", [])
        raw_d = snapshot.get("d", [])
        seq = int(snapshot.get("seq", 0))
        self.data_seq = seq
        now = time.monotonic()

        if not raw_t or not raw_p or not raw_d:
            return

        self.last_data_time = now

        n = min(len(raw_t), len(raw_p), len(raw_d))
        if n <= 0:
            return
        raw_t = raw_t[-n:]
        raw_p = raw_p[-n:]
        raw_d = raw_d[-n:]

        if self.last_seq < 0:
            new_count = n
        else:
            delta = seq - self.last_seq
            if delta <= 0:
                return
            new_count = min(n, delta)

        if new_count <= 0:
            return

        new_p = raw_p[-new_count:]
        new_d = raw_d[-new_count:]
        p_norm = self._normalize_fixed(new_p)
        d_norm = self._normalize_fixed(new_d)

        for p_val, d_val in zip(p_norm, d_norm):
            self.time_axis.append(self._sample_index / self.fs)
            self.proximal_filtered.append(p_val)
            self.distal_filtered.append(d_val)
            self._sample_index += 1

        self.last_seq = seq

    def get_signals(self):
        if len(self.time_axis) < self.MAX_POINTS:
            return [], [], []
        return list(self.time_axis), list(self.proximal_filtered), list(self.distal_filtered)

    def get_metrics(self):
        buffer_progress = min(1.0, len(self.time_axis) / float(self.MAX_POINTS))
        buffer_ready = len(self.time_axis) >= self.MAX_POINTS
        return {
            "hr": self.hr,
            "pwv": self.pwv,
            "calibrating": self.session_active and (not buffer_ready),
            "calibration_progress": buffer_progress,
            "calibration_ready": buffer_ready,
            "buffer_progress": buffer_progress,
            "buffer_ready": buffer_ready,
            "calib_min_p": None,
            "calib_max_p": None,
            "calib_min_d": None,
            "calib_max_d": None,
            "y1_min": -100.0,
            "y1_max": 100.0,
            "y2_min": -100.0,
            "y2_max": 100.0,
            "data_seq": self.data_seq,
        }

    def get_sensor_status(self):
        return {
            "connected": self.connected,
            "c1": self.c1,
            "c2": self.c2,
            "s1": self.s1,
            "s2": self.s2,
        }


processor = SignalProcessor()
