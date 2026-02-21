"""
BACKEND.PY
Visual-only processing pipeline for signal display.

- Receives already-filtered signals from ESP32 through ComunicacionMax.
- Runs a 10-second amplitude calibration (min/max) at session start.
- Normalizes for stable plotting.
- Does NOT compute HR or PWV (those come from ESP32 JSON).
"""

from collections import deque
import time

import numpy as np

import ComunicacionMax


class SignalProcessor:
    def __init__(self, fs=50):
        self.fs = fs
        self.MAX_POINTS = 300
        self.VIEW_SECONDS = 6.0
        self.CALIB_SECONDS = 10.0
        self.NO_DATA_HOLD_SECONDS = 1.0

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

        self.calib_min_p = None
        self.calib_max_p = None
        self.calib_min_d = None
        self.calib_max_d = None

        self.last_data_time = None
        self.last_seq = -1

    def start_session(self):
        self.session_active = True
        self.session_start_local = time.monotonic()
        self.calib_min_p = None
        self.calib_max_p = None
        self.calib_min_d = None
        self.calib_max_d = None
        self.hr = None
        self.pwv = None
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self.last_data_time = None
        self.last_seq = -1

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

    def _update_status(self, snapshot):
        self.connected = bool(snapshot.get("connected", False))
        self.c1 = bool(snapshot.get("c1", False))
        self.c2 = bool(snapshot.get("c2", False))
        self.s1 = bool(snapshot.get("s1", False))
        self.s2 = bool(snapshot.get("s2", False))
        self.hr = snapshot.get("hr", None)
        self.pwv = snapshot.get("pwv", None)

    def _calibration_progress(self):
        if not self.session_active or self.session_start_local is None:
            return 0.0
        elapsed = time.monotonic() - self.session_start_local
        return max(0.0, min(1.0, elapsed / self.CALIB_SECONDS))

    def _has_valid_calibration(self):
        if None in (self.calib_min_p, self.calib_max_p, self.calib_min_d, self.calib_max_d):
            return False
        return (self.calib_max_p - self.calib_min_p) > 1e-6 and (self.calib_max_d - self.calib_min_d) > 1e-6

    def _update_calibration(self, p_arr, d_arr):
        if p_arr.size == 0 or d_arr.size == 0:
            return

        p_min = float(np.min(p_arr))
        p_max = float(np.max(p_arr))
        d_min = float(np.min(d_arr))
        d_max = float(np.max(d_arr))

        self.calib_min_p = p_min if self.calib_min_p is None else min(self.calib_min_p, p_min)
        self.calib_max_p = p_max if self.calib_max_p is None else max(self.calib_max_p, p_max)
        self.calib_min_d = d_min if self.calib_min_d is None else min(self.calib_min_d, d_min)
        self.calib_max_d = d_max if self.calib_max_d is None else max(self.calib_max_d, d_max)

    def _normalize_signal(self, signal, vmin, vmax):
        baseline = (vmax + vmin) * 0.5
        half = (vmax - vmin) * 0.5
        if half < 1e-6:
            half = 1.0
        normalized = ((signal - baseline) / half) * 100.0
        return np.clip(normalized, -110.0, 110.0)

    def process_all(self):
        if not self.session_active:
            return

        snapshot = ComunicacionMax.get_snapshot()
        self._update_status(snapshot)

        raw_t = snapshot.get("t", [])
        raw_p = snapshot.get("p", [])
        raw_d = snapshot.get("d", [])
        seq = int(snapshot.get("seq", 0))
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
        raw_t = np.asarray(raw_t[-n:], dtype=float)
        raw_p = np.asarray(raw_p[-n:], dtype=float)
        raw_d = np.asarray(raw_d[-n:], dtype=float)

        # Calibrate using first 10 seconds when both sensors are physically connected and on skin.
        if self.c1 and self.c2 and self.s1 and self.s2 and self._calibration_progress() < 1.0:
            self._update_calibration(raw_p[-1:], raw_d[-1:])

        # Use fixed calibration when available; fallback to current window scale while calibrating.
        if self._has_valid_calibration():
            p_min, p_max = self.calib_min_p, self.calib_max_p
            d_min, d_max = self.calib_min_d, self.calib_max_d
        else:
            p_min, p_max = float(np.min(raw_p)), float(np.max(raw_p))
            d_min, d_max = float(np.min(raw_d)), float(np.max(raw_d))

        norm_p = self._normalize_signal(raw_p, p_min, p_max)
        norm_d = self._normalize_signal(raw_d, d_min, d_max)

        if self.session_start_local is None:
            self.session_start_local = raw_t[0]

        t_rel = raw_t - self.session_start_local
        t_end = float(t_rel[-1])
        t_start = max(0.0, t_end - self.VIEW_SECONDS)
        idx = int(np.searchsorted(t_rel, t_start, side="left"))

        self.time_axis = deque(t_rel[idx:].tolist(), maxlen=self.MAX_POINTS)
        self.proximal_filtered = deque(norm_p[idx:].tolist(), maxlen=self.MAX_POINTS)
        self.distal_filtered = deque(norm_d[idx:].tolist(), maxlen=self.MAX_POINTS)
        self.last_seq = seq

    def get_signals(self):
        return list(self.time_axis), list(self.proximal_filtered), list(self.distal_filtered)

    def get_metrics(self):
        return {
            "hr": self.hr,
            "pwv": self.pwv,
            "calibrating": self.session_active and self._calibration_progress() < 1.0,
            "calibration_progress": self._calibration_progress(),
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
