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
        self.INITIAL_BUFFER_SECONDS = 10.0
        self.PLAYBACK_DELAY_SECONDS = 5.0
        self.INPUT_BUFFER_SECONDS = 30.0
        self.MAX_POINTS = max(2, int(round(self.fs * self.VIEW_SECONDS)))
        self.INITIAL_BUFFER_POINTS = max(2, int(round(self.fs * self.INITIAL_BUFFER_SECONDS)))
        self.PLAYBACK_DELAY_POINTS = max(0, int(round(self.fs * self.PLAYBACK_DELAY_SECONDS)))
        self.INPUT_MAX_POINTS = max(
            self.MAX_POINTS + self.INITIAL_BUFFER_POINTS + self.PLAYBACK_DELAY_POINTS,
            int(round(self.fs * self.INPUT_BUFFER_SECONDS)),
        )
        self.MAX_ADC = 262143.0
        # Ganancias de visualizacion por canal para compensar sensibilidad distinta
        # entre proximal y distal sin tocar el firmware.
        self.PROX_DISPLAY_GAIN = 650.0
        self.DIST_DISPLAY_GAIN = 2500.0

        self.proximal_filtered = deque(maxlen=self.MAX_POINTS)
        self.distal_filtered = deque(maxlen=self.MAX_POINTS)
        self.time_axis = deque(maxlen=self.MAX_POINTS)
        self._input_prox = deque(maxlen=self.INPUT_MAX_POINTS)
        self._input_dist = deque(maxlen=self.INPUT_MAX_POINTS)

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
        self._playback_started = False
        self._last_playback_time = None

    def start_session(self):
        self.session_active = True
        self.session_start_local = time.monotonic()
        self.hr = None
        self.pwv = None
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self._input_prox.clear()
        self._input_dist.clear()
        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0
        self._playback_started = False
        self._last_playback_time = None

    def stop_session(self):
        self.session_active = False

    def clear_buffers(self):
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self._input_prox.clear()
        self._input_dist.clear()
        self.hr = None
        self.pwv = None
        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0
        self._playback_started = False
        self._last_playback_time = None

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

    def _normalize_fixed(self, values, gain):
        out = []
        for v in values:
            norm = (float(v) / self.MAX_ADC) * 100.0
            norm *= gain
            if norm > 100.0:
                norm = 100.0
            elif norm < -100.0:
                norm = -100.0
            out.append(norm)
        return out

    def _advance_playback(self, now):
        if (not self._input_prox) or (not self._input_dist):
            return

        if not self._playback_started:
            if len(self._input_prox) < self.INITIAL_BUFFER_POINTS:
                return
            self._playback_started = True
            self._last_playback_time = now
            return

        if self._last_playback_time is None:
            self._last_playback_time = now
            return

        elapsed = now - self._last_playback_time
        if elapsed <= 0.0:
            return

        frames_due = int(elapsed * self.fs)
        if frames_due <= 0:
            return

        available = min(len(self._input_prox), len(self._input_dist)) - self.PLAYBACK_DELAY_POINTS
        if available <= 0:
            # Sin margen de atraso, reseteamos deuda de tiempo para no producir saltos grandes.
            self._last_playback_time = now
            return

        emit = min(frames_due, available, self.MAX_POINTS)
        if emit <= 0:
            return

        for _ in range(emit):
            p_val = self._input_prox.popleft()
            d_val = self._input_dist.popleft()
            self.time_axis.append(self._sample_index / self.fs)
            self.proximal_filtered.append(p_val)
            self.distal_filtered.append(d_val)
            self._sample_index += 1

        self._last_playback_time += emit / self.fs
        if emit < frames_due:
            self._last_playback_time = now

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
            self._advance_playback(now)
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
                self._advance_playback(now)
                return
            new_count = min(n, delta)

        if new_count <= 0:
            self._advance_playback(now)
            return

        new_p = raw_p[-new_count:]
        new_d = raw_d[-new_count:]
        p_norm = self._normalize_fixed(new_p, self.PROX_DISPLAY_GAIN)
        d_norm = self._normalize_fixed(new_d, self.DIST_DISPLAY_GAIN)

        for p_val, d_val in zip(p_norm, d_norm):
            self._input_prox.append(p_val)
            self._input_dist.append(d_val)

        self.last_seq = seq
        self._advance_playback(now)

    def get_signals(self):
        if (not self._playback_started) or (len(self.time_axis) <= 1):
            return [], [], []
        return list(self.time_axis), list(self.proximal_filtered), list(self.distal_filtered)

    def get_metrics(self):
        if self._playback_started:
            buffer_progress = 1.0
            buffer_ready = True
        else:
            buffer_progress = min(1.0, len(self._input_prox) / float(self.INITIAL_BUFFER_POINTS))
            buffer_ready = False
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
