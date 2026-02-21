"""
BACKEND.PY
Visual-only processing pipeline for signal display.

- Receives signals from ESP32 through ComunicacionMax.
- Uses fixed normalization: (ir / 262143.0) * 100.0
- Does NOT compute HR or PWV (those come from ESP32 JSON).
"""

from bisect import bisect_left
from collections import deque
import math
import time

import ComunicacionMax


class SignalProcessor:
    def __init__(self, fs=50):
        self.fs = fs
        self.MAX_POINTS = 300
        self.VIEW_SECONDS = 6.0
        self.NO_DATA_HOLD_SECONDS = 1.0
        self.MAX_ADC = 262143.0

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
            if self.last_data_time is None or (now - self.last_data_time) > self.NO_DATA_HOLD_SECONDS:
                self.proximal_filtered.clear()
                self.distal_filtered.clear()
                self.time_axis.clear()
            return

        self.last_data_time = now

        if seq == self.last_seq and len(self.time_axis) > 1:
            return

        n = min(len(raw_t), len(raw_p), len(raw_d))
        raw_t = raw_t[-n:]
        raw_p = raw_p[-n:]
        raw_d = raw_d[-n:]

        if self.session_start_local is None:
            self.session_start_local = float(raw_t[0])

        t_rel = [float(t) - self.session_start_local for t in raw_t]
        p_norm = self._normalize_fixed(raw_p)
        d_norm = self._normalize_fixed(raw_d)

        t_end = t_rel[-1]
        t_start = max(0.0, t_end - self.VIEW_SECONDS)
        idx = bisect_left(t_rel, t_start)

        self.time_axis = deque(t_rel[idx:], maxlen=self.MAX_POINTS)
        self.proximal_filtered = deque(p_norm[idx:], maxlen=self.MAX_POINTS)
        self.distal_filtered = deque(d_norm[idx:], maxlen=self.MAX_POINTS)
        self.last_seq = seq

    def get_signals(self):
        return list(self.time_axis), list(self.proximal_filtered), list(self.distal_filtered)

    def get_metrics(self):
        return {
            "hr": self.hr,
            "pwv": self.pwv,
            "calibrating": False,
            "calibration_progress": 1.0,
            "calibration_ready": True,
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
