"""
BACKEND.PY
Visual-only processing pipeline for signal display.

- Receives signals from ESP32 through ComunicacionMax.
- Performs a 10-second startup calibration (per channel min/max).
- Starts plotting only after calibration is complete.
- Uses delayed playback (5 s) to smooth jitter.
- Scales both signals with fixed calibration ranges to [-100, 100].
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
        self.CALIB_SECONDS = 10.0
        self.PLAYBACK_DELAY_SECONDS = 5.0
        self.STARTUP_FILL_SECONDS = 10.0
        self.HOLDOVER_SECONDS = 0.0
        self.PLAYBACK_RATE_SLOW = 0.90
        self.PLAYBACK_RATE_FAST = 1.00
        self.QUEUE_LOW_SECONDS = 0.80
        self.QUEUE_HIGH_SECONDS = 2.20
        self.INPUT_BUFFER_SECONDS = 40.0

        self.MAX_POINTS = max(2, int(round(self.fs * self.VIEW_SECONDS)))
        self.CALIB_POINTS = max(2, int(round(self.fs * self.CALIB_SECONDS)))
        self.PLAYBACK_DELAY_POINTS = max(0, int(round(self.fs * self.PLAYBACK_DELAY_SECONDS)))
        self.STARTUP_FILL_POINTS = max(0, int(round(self.fs * self.STARTUP_FILL_SECONDS)))
        self.HOLDOVER_MAX_POINTS = max(0, int(round(self.fs * self.HOLDOVER_SECONDS)))
        self.QUEUE_LOW_POINTS = max(0, int(round(self.fs * self.QUEUE_LOW_SECONDS)))
        self.QUEUE_HIGH_POINTS = max(self.QUEUE_LOW_POINTS + 1, int(round(self.fs * self.QUEUE_HIGH_SECONDS)))
        self.INPUT_MAX_POINTS = max(
            self.MAX_POINTS + self.CALIB_POINTS + self.PLAYBACK_DELAY_POINTS + self.STARTUP_FILL_POINTS,
            int(round(self.fs * self.INPUT_BUFFER_SECONDS)),
        )

        # Output window (already scaled for plotting)
        self.proximal_filtered = deque(maxlen=self.MAX_POINTS)
        self.distal_filtered = deque(maxlen=self.MAX_POINTS)
        self.time_axis = deque(maxlen=self.MAX_POINTS)

        # Raw input queue used for delayed playback
        self._input_prox = deque(maxlen=self.INPUT_MAX_POINTS)
        self._input_dist = deque(maxlen=self.INPUT_MAX_POINTS)

        # Raw calibration buffers (first 10 s with both sensors OK)
        self._calib_prox = deque(maxlen=self.CALIB_POINTS)
        self._calib_dist = deque(maxlen=self.CALIB_POINTS)
        self._calibration_ready = False
        self.calib_min_p = None
        self.calib_max_p = None
        self.calib_min_d = None
        self.calib_max_d = None

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
        self._holdover_used_points = 0

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
        self._calib_prox.clear()
        self._calib_dist.clear()

        self._calibration_ready = False
        self.calib_min_p = None
        self.calib_max_p = None
        self.calib_min_d = None
        self.calib_max_d = None

        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0
        self._playback_started = False
        self._last_playback_time = None
        self._holdover_used_points = 0

    def stop_session(self):
        self.session_active = False

    def clear_buffers(self):
        self.proximal_filtered.clear()
        self.distal_filtered.clear()
        self.time_axis.clear()
        self._input_prox.clear()
        self._input_dist.clear()
        self._calib_prox.clear()
        self._calib_dist.clear()

        self.hr = None
        self.pwv = None
        self._calibration_ready = False
        self.calib_min_p = None
        self.calib_max_p = None
        self.calib_min_d = None
        self.calib_max_d = None

        self.last_data_time = None
        self.last_seq = -1
        self.data_seq = -1
        self._sample_index = 0
        self._playback_started = False
        self._last_playback_time = None
        self._holdover_used_points = 0

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

    def _coerce_sample_pair(self, p_val, d_val):
        try:
            p = float(p_val)
            d = float(d_val)
        except (TypeError, ValueError):
            return None
        if (not math.isfinite(p)) or (not math.isfinite(d)):
            return None
        return (p, d)

    def _try_finalize_calibration(self):
        if self._calibration_ready:
            return
        if len(self._calib_prox) < self.CALIB_POINTS or len(self._calib_dist) < self.CALIB_POINTS:
            return

        p_min = min(self._calib_prox)
        p_max = max(self._calib_prox)
        d_min = min(self._calib_dist)
        d_max = max(self._calib_dist)

        if (p_max - p_min) <= 1e-9 or (d_max - d_min) <= 1e-9:
            return

        self.calib_min_p = float(p_min)
        self.calib_max_p = float(p_max)
        self.calib_min_d = float(d_min)
        self.calib_max_d = float(d_max)
        self._calibration_ready = True

    def _scale_with_minmax(self, values, vmin, vmax):
        if not values:
            return []
        span = vmax - vmin
        if span <= 1e-9:
            return [0.0 for _ in values]

        out = []
        for v in values:
            scaled = ((float(v) - vmin) / span) * 200.0 - 100.0
            if scaled > 100.0:
                scaled = 100.0
            elif scaled < -100.0:
                scaled = -100.0
            out.append(scaled)
        return out

    def _select_playback_rate(self):
        reserve = self.PLAYBACK_DELAY_POINTS + self.STARTUP_FILL_POINTS
        queue_len = min(len(self._input_prox), len(self._input_dist))
        headroom = max(0, queue_len - reserve)

        if headroom <= self.QUEUE_LOW_POINTS:
            return self.PLAYBACK_RATE_SLOW
        if headroom >= self.QUEUE_HIGH_POINTS:
            return self.PLAYBACK_RATE_FAST
        return 1.0

    def _advance_playback(self, now):
        if not self._calibration_ready:
            return
        if (not self._input_prox) or (not self._input_dist):
            return

        if not self._playback_started:
            required = self.PLAYBACK_DELAY_POINTS + self.STARTUP_FILL_POINTS
            if min(len(self._input_prox), len(self._input_dist)) <= required:
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

        playback_rate = self._select_playback_rate()
        frames_due = int(elapsed * self.fs * playback_rate)
        if frames_due <= 0:
            return

        available = min(len(self._input_prox), len(self._input_dist)) - self.PLAYBACK_DELAY_POINTS
        if available <= 0:
            if (
                frames_due <= 0
                or self.HOLDOVER_MAX_POINTS <= 0
                or self._holdover_used_points >= self.HOLDOVER_MAX_POINTS
                or (not self.proximal_filtered)
                or (not self.distal_filtered)
            ):
                self._last_playback_time = now
                return

            hold_budget = self.HOLDOVER_MAX_POINTS - self._holdover_used_points
            emit_hold = min(frames_due, hold_budget, self.MAX_POINTS)
            if emit_hold <= 0:
                self._last_playback_time = now
                return

            p_hold = self.proximal_filtered[-1]
            d_hold = self.distal_filtered[-1]
            for _ in range(emit_hold):
                self.time_axis.append(self._sample_index / self.fs)
                self.proximal_filtered.append(p_hold)
                self.distal_filtered.append(d_hold)
                self._sample_index += 1

            self._holdover_used_points += emit_hold
            self._last_playback_time += emit_hold / self.fs
            if emit_hold < frames_due:
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
        self._holdover_used_points = 0

        self._last_playback_time += emit / self.fs
        if emit < frames_due:
            self._last_playback_time = now

    def process_all(self):
        if not self.session_active:
            return

        snapshot = ComunicacionMax.get_snapshot(include_stream=False)
        self._update_status(snapshot)

        pending = ComunicacionMax.consume_pending_samples(max_items=self.INPUT_MAX_POINTS)
        raw_p = pending.get("p", [])
        raw_d = pending.get("d", [])
        seq = int(pending.get("seq", snapshot.get("seq", 0)))
        self.data_seq = seq
        now = time.monotonic()

        if not raw_p or not raw_d:
            self._advance_playback(now)
            return

        self.last_data_time = now

        n = min(len(raw_p), len(raw_d))
        if n <= 0:
            self._advance_playback(now)
            return
        raw_p = raw_p[-n:]
        raw_d = raw_d[-n:]

        both_signals_ok = self.c1 and self.c2 and self.s1 and self.s2
        if (not self._calibration_ready) and (not both_signals_ok):
            # Exigimos 10 s continuos válidos para calibrar.
            self._calib_prox.clear()
            self._calib_dist.clear()
            self._input_prox.clear()
            self._input_dist.clear()

        new_p = raw_p
        new_d = raw_d
        ingest_enabled = self._calibration_ready or both_signals_ok

        if ingest_enabled:
            for p_val, d_val in zip(new_p, new_d):
                pair = self._coerce_sample_pair(p_val, d_val)
                if pair is None:
                    continue
                p, d = pair
                self._input_prox.append(p)
                self._input_dist.append(d)
                if not self._calibration_ready:
                    self._calib_prox.append(p)
                    self._calib_dist.append(d)

            if not self._calibration_ready:
                self._try_finalize_calibration()

        self.last_seq = seq
        self._advance_playback(now)

    def get_signals(self):
        if (not self._playback_started) or (len(self.time_axis) <= 1):
            return [], [], []

        t = list(self.time_axis)
        p = self._scale_with_minmax(list(self.proximal_filtered), self.calib_min_p, self.calib_max_p)
        d = self._scale_with_minmax(list(self.distal_filtered), self.calib_min_d, self.calib_max_d)
        return t, p, d

    def get_metrics(self):
        if self._playback_started:
            progress = 1.0
            buffer_ready = True
        else:
            progress = min(1.0, len(self._calib_prox) / float(self.CALIB_POINTS))
            buffer_ready = False

        return {
            "hr": self.hr,
            "pwv": self.pwv,
            "calibrating": self.session_active and (not buffer_ready),
            "calibration_progress": progress,
            "calibration_ready": self._calibration_ready,
            "buffer_progress": progress,
            "buffer_ready": buffer_ready,
            "calib_min_p": self.calib_min_p,
            "calib_max_p": self.calib_max_p,
            "calib_min_d": self.calib_min_d,
            "calib_max_d": self.calib_max_d,
            "y1_min": self.calib_min_p,
            "y1_max": self.calib_max_p,
            "y2_min": self.calib_min_d,
            "y2_max": self.calib_max_d,
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
