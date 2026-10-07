#!/usr/bin/env python3
#
# EdgeTX "App Config" host client.
#
# Talks the binary App Config protocol to a radio whose AUX serial port is set
# to the "App Config" mode (115200 8N1 by default). Works over a plain USB-UART
# adapter as well as over a transparent Bluetooth / WiFi-UART bridge.
#
# Protocol reference: docs/development/app-config-protocol.md
#
# Requires: pyserial (pip install pyserial)
#
# Usage:
#   python3 app_config_client.py --port COM7 info
#   python3 app_config_client.py --port /dev/ttyUSB0 mixes --channel 2
#   python3 app_config_client.py --port COM7 demo
#
# This file is also usable as a library:
#
#   from app_config_client import AppConfigClient
#   with AppConfigClient('/dev/ttyUSB0') as rc:
#       print(rc.get_info())
#       rc.set_mix(0, 0, {'source': 1, 'weight': 100})

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

SYNC0 = 0xA5
SYNC1 = 0x5A

MAX_PAYLOAD = 64

PROTO_VERSION = 1

# Status codes
ST_OK = 0
ST_ERROR = 1
ST_BAD_CMD = 2
ST_BAD_PARAM = 3
ST_RANGE = 4
ST_UNSUPPORTED = 5
ST_NOT_READY = 6

STATUS_NAMES = {
    ST_OK: "OK",
    ST_ERROR: "ERROR",
    ST_BAD_CMD: "BAD_CMD",
    ST_BAD_PARAM: "BAD_PARAM",
    ST_RANGE: "OUT_OF_RANGE",
    ST_UNSUPPORTED: "UNSUPPORTED",
    ST_NOT_READY: "NOT_READY",
}

# Commands
CMD_GET_INFO = 0x01

CMD_MIX_COUNT = 0x10
CMD_MIX_GET = 0x11
CMD_MIX_INSERT = 0x12
CMD_MIX_SET = 0x13
CMD_MIX_DELETE = 0x14
CMD_MIX_MOVE = 0x15

CMD_INPUT_COUNT = 0x16
CMD_INPUT_GET = 0x17
CMD_INPUT_INSERT = 0x18
CMD_INPUT_SET = 0x19
CMD_INPUT_DELETE = 0x1A
CMD_INPUT_MOVE = 0x1B
CMD_INPUT_GET_NAME = 0x1C
CMD_INPUT_SET_NAME = 0x1D

CMD_OUTPUT_GET = 0x20
CMD_OUTPUT_SET = 0x21
CMD_OUTPUT_SET_NAME = 0x22

CMD_CURVE_GET = 0x23
CMD_CURVE_SET = 0x24
CMD_CURVE_SET_POINT = 0x25
CMD_CURVE_CLEAR = 0x26
CMD_CURVE_MIRROR = 0x27

CMD_LS_GET = 0x28
CMD_LS_SET = 0x29

CMD_CF_GET = 0x2B
CMD_CF_SET = 0x2C

CMD_MODEL_GET_NAME = 0x30
CMD_MODEL_SET_NAME = 0x31
CMD_TIMER_GET = 0x32
CMD_TIMER_SET = 0x33
CMD_MODEL_LIST = 0x34
CMD_MODEL_SELECT = 0x35
CMD_MODEL_CREATE = 0x36
CMD_MODEL_DUPLICATE = 0x37
CMD_MODEL_DELETE = 0x38
CMD_MODEL_RENAME = 0x39

CMD_GENERAL_GET = 0x40
CMD_GENERAL_SET = 0x41
CMD_RF_POWER_GET = 0x42
CMD_RF_POWER_SET = 0x43

CMD_FM_GET = 0x50
CMD_FM_SET = 0x51
CMD_FM_SET_TRIM = 0x52

CMD_GET_STATUS = 0x60

CMD_SUBSCRIBE = 0x70

CMD_PARAM_LIST = 0x80
CMD_PARAM_GET = 0x81
CMD_PARAM_SET = 0x82
CMD_PARAM_EXPORT = 0x83

CMD_GVAR_GET = 0x86
CMD_GVAR_SET = 0x87

# Generic settings tree roots
ROOT_RADIO = 0
ROOT_MODEL = 1

# YamlDataType values reported by PARAM_LIST
YAML_TYPES = {
    0: "none",
    1: "idx",
    2: "signed",
    3: "unsigned",
    4: "string",
    5: "array",
    6: "enum",
    7: "union",
    8: "padding",
    9: "custom",
}

# Sent by the radio, sequence number 0
CMD_EVENT = 0x00
EVENT_SETTINGS_CHANGED = 1

SUBSCRIBE_SETTINGS_CHANGED = 0x01

EXPO_BLOB_FIXED = 18
CF_BLOB_FIXED = 15

MUTATING_COMMANDS = {
    CMD_MIX_INSERT,
    CMD_MIX_SET,
    CMD_MIX_DELETE,
    CMD_MIX_MOVE,
    CMD_INPUT_INSERT,
    CMD_INPUT_SET,
    CMD_INPUT_DELETE,
    CMD_INPUT_MOVE,
    CMD_INPUT_SET_NAME,
    CMD_OUTPUT_SET,
    CMD_OUTPUT_SET_NAME,
    CMD_CURVE_SET,
    CMD_CURVE_SET_POINT,
    CMD_CURVE_CLEAR,
    CMD_CURVE_MIRROR,
    CMD_LS_SET,
    CMD_CF_SET,
    CMD_MODEL_SET_NAME,
    CMD_MODEL_SELECT,
    CMD_MODEL_CREATE,
    CMD_MODEL_DUPLICATE,
    CMD_MODEL_DELETE,
    CMD_MODEL_RENAME,
    CMD_TIMER_SET,
    CMD_GENERAL_SET,
    CMD_RF_POWER_SET,
    CMD_FM_SET,
    CMD_FM_SET_TRIM,
    CMD_SUBSCRIBE,
    CMD_PARAM_SET,
    CMD_GVAR_SET,
}

# General setting ids
GEN_VOLUME = 1
GEN_BEEP_VOLUME = 2
GEN_WAV_VOLUME = 3
GEN_VARIO_VOLUME = 4
GEN_BACKGROUND_VOLUME = 5
GEN_HAPTIC_STRENGTH = 6
GEN_HAPTIC_LENGTH = 7
GEN_BEEP_LENGTH = 8
GEN_BACKLIGHT = 9

GENERAL_NAMES = {
    GEN_VOLUME: "volume",
    GEN_BEEP_VOLUME: "beep_volume",
    GEN_WAV_VOLUME: "wav_volume",
    GEN_VARIO_VOLUME: "vario_volume",
    GEN_BACKGROUND_VOLUME: "background_volume",
    GEN_HAPTIC_STRENGTH: "haptic_strength",
    GEN_HAPTIC_LENGTH: "haptic_length",
    GEN_BEEP_LENGTH: "beep_length",
    GEN_BACKLIGHT: "backlight",
}

GENERAL_IDS = {v: k for k, v in GENERAL_NAMES.items()}

MIX_BLOB_FIXED = 20


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    body = struct.pack("<BBB", cmd, seq, len(payload)) + payload
    crc = crc16_ccitt(body)
    return bytes([SYNC0, SYNC1]) + body + struct.pack("<H", crc)


class AppConfigError(Exception):
    def __init__(self, status, message=None):
        self.status = status
        self.name = STATUS_NAMES.get(status, "UNKNOWN(%d)" % status)
        super().__init__(message or self.name)


class AppConfigClient:
    """Blocking App Config client with sequence numbers, ACKs and retries."""

    def __init__(self, port, baudrate=115200, timeout=0.3, retries=3, verbose=False):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        self.timeout = timeout
        self.retries = retries
        self.verbose = verbose
        self.seq = 0

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    # -- low level ---------------------------------------------------------

    def _next_seq(self) -> int:
        # 1..255: sequence 0 is reserved for unsolicited events
        self.seq = (self.seq % 255) + 1
        return self.seq

    def _read_frame(self, timeout):
        deadline = time.monotonic() + timeout
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(1)
            if not chunk:
                continue
            buf += chunk
            if len(buf) < 5:
                continue
            if buf[0] != SYNC0 or buf[1] != SYNC1:
                del buf[0]
                continue
            total = 5 + buf[4] + 2
            while len(buf) < total and time.monotonic() < deadline:
                need = total - len(buf)
                more = self.ser.read(need)
                if more:
                    buf += more
            if len(buf) < total:
                return None
            frame = bytes(buf[:total])
            if crc16_ccitt(frame[2:5 + frame[4]]) != struct.unpack("<H", frame[5 + frame[4]:7 + frame[4]])[0]:
                return None
            return frame
        return None

    def request(self, cmd: int, payload: bytes = b"", expect_len=None):
        """Send a command and wait for its ACK. Raises AppConfigError on failure."""
        mutating = cmd in MUTATING_COMMANDS
        seq = self._next_seq()

        for attempt in range(self.retries):
            frame = build_frame(cmd, seq, payload)
            if self.verbose:
                print(
                    "-> cmd=0x%02X seq=%d len=%d %s" % (cmd, seq, len(payload), payload.hex()),
                    file=sys.stderr,
                )
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()

            deadline = time.monotonic() + self.timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                resp = self._read_frame(remaining)
                if resp is None:
                    break
                if self.verbose:
                    print("<- %s" % resp.hex(), file=sys.stderr)
                r_cmd, r_seq, r_len = resp[2], resp[3], resp[4]
                if r_cmd != cmd or r_seq != seq:
                    continue  # stale / unrelated frame
                status = resp[5]
                data = resp[6:5 + r_len]
                if status != ST_OK:
                    raise AppConfigError(status)
                if expect_len is not None and len(data) != expect_len:
                    raise AppConfigError(ST_ERROR, "unexpected payload length %d" % len(data))
                return data
            # no answer -> retransmit (mutating commands are de-duplicated by seq)
            if not mutating:
                break
        raise AppConfigError(ST_ERROR, "no response (timeout)")

    # -- info --------------------------------------------------------------

    def get_info(self):
        data = self.request(CMD_GET_INFO)
        proto, vmaj, vmin, vrev, max_ch, max_mix, max_fm, max_timers, num_modules, name_len = \
            struct.unpack_from("<BBBBBBBBBB", data, 0)
        name = data[10:10 + name_len].decode("utf-8", "replace")
        return {
            "proto": proto,
            "version": "%d.%d.%d" % (vmaj, vmin, vrev),
            "max_channels": max_ch,
            "max_mixes": max_mix,
            "max_flight_modes": max_fm,
            "max_timers": max_timers,
            "num_modules": num_modules,
            "model_name": name,
        }

    def get_status(self):
        dirty, running = struct.unpack("<BB", self.request(CMD_GET_STATUS))
        return {"dirty": dirty, "mixer_running": bool(running)}

    def wait_saved(self, timeout=5.0):
        """Poll until pending settings have been written to the SD card."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.get_status()["dirty"] == 0:
                return True
            time.sleep(0.2)
        return False

    # -- mixer -------------------------------------------------------------

    def mix_count(self, channel: int) -> int:
        return self.request(CMD_MIX_COUNT, struct.pack("<B", channel))[0]

    def get_mix(self, channel: int, line: int) -> dict:
        data = self.request(CMD_MIX_GET, struct.pack("<BB", channel, line))
        (src, weight, offset, swtch, curve_type, curve_value, mltpx,
         flight_modes, flags, delay_up, delay_down, speed_up, speed_down, name_len) = \
            struct.unpack_from("<HhhHBhBHBBBBBB", data, 0)
        name = data[MIX_BLOB_FIXED:MIX_BLOB_FIXED + name_len].decode("utf-8", "replace")
        return {
            "source": src,
            "weight": weight,
            "offset": offset,
            "switch": swtch,
            "curve_type": curve_type,
            "curve_value": curve_value,
            "multiplex": mltpx,
            "flight_modes": flight_modes,
            "carry_trim": bool(flags & 0x01),
            "mix_warn": (flags >> 1) & 0x03,
            "delay_prec": bool(flags & 0x08),
            "speed_prec": bool(flags & 0x10),
            "delay_up": delay_up,
            "delay_down": delay_down,
            "speed_up": speed_up,
            "speed_down": speed_down,
            "name": name,
        }

    def _mix_blob(self, mix: dict, name: str = "") -> bytes:
        name_bytes = name.encode("utf-8")[:5]  # LEN_EXPOMIX_NAME - 1
        flags = 0
        flags |= 0x01 if mix.get("carry_trim", False) else 0
        flags |= (int(mix.get("mix_warn", 0)) & 0x03) << 1
        flags |= 0x08 if mix.get("delay_prec", False) else 0
        flags |= 0x10 if mix.get("speed_prec", False) else 0
        return struct.pack(
            "<HhhHBhBHBBBBBB",
            int(mix.get("source", 0)),
            int(mix.get("weight", 100)),
            int(mix.get("offset", 0)),
            int(mix.get("switch", 0)),
            int(mix.get("curve_type", 0)),
            int(mix.get("curve_value", 0)),
            int(mix.get("multiplex", 0)),
            int(mix.get("flight_modes", 0)),
            flags,
            int(mix.get("delay_up", 0)),
            int(mix.get("delay_down", 0)),
            int(mix.get("speed_up", 0)),
            int(mix.get("speed_down", 0)),
            len(name_bytes),
        ) + name_bytes

    def insert_mix(self, channel: int, line: int, mix: dict, name: str = "") -> int:
        payload = struct.pack("<BB", channel, line) + self._mix_blob(mix, name)
        return self.request(CMD_MIX_INSERT, payload)[0]

    def set_mix(self, channel: int, line: int, mix: dict, name: str = "") -> None:
        payload = struct.pack("<BB", channel, line) + self._mix_blob(mix, name)
        self.request(CMD_MIX_SET, payload)

    def delete_mix(self, channel: int, line: int) -> None:
        self.request(CMD_MIX_DELETE, struct.pack("<BB", channel, line))

    def move_mix(self, channel: int, line: int, up: bool) -> int:
        payload = struct.pack("<BBB", channel, line, 0 if up else 1)
        return self.request(CMD_MIX_MOVE, payload)[0]

    # -- outputs -----------------------------------------------------------

    def get_output(self, channel: int) -> dict:
        data = self.request(CMD_OUTPUT_GET, struct.pack("<B", channel))
        min_pct, max_pct, offset, ppm_center, symetrical, revert, curve, name_len = \
            struct.unpack_from("<hhhhBBbB", data, 0)
        return {
            "min": min_pct,
            "max": max_pct,
            "offset": offset,
            "ppm_center": ppm_center,
            "symetrical": bool(symetrical),
            "revert": bool(revert),
            "curve": curve,
            "name": data[12:12 + name_len].decode("utf-8", "replace"),
        }

    def set_output(self, channel: int, output: dict) -> None:
        payload = struct.pack(
            "<BhhhhBB",
            channel,
            int(output.get("min", -1000)),
            int(output.get("max", 1000)),
            int(output.get("offset", 0)),
            int(output.get("ppm_center", 0)),
            1 if output.get("symetrical", False) else 0,
            1 if output.get("revert", False) else 0,
        )
        self.request(CMD_OUTPUT_SET, payload)

    def set_output_name(self, channel: int, name: str) -> None:
        name_bytes = name.encode("utf-8")[:5]  # LEN_CHANNEL_NAME - 1
        self.request(CMD_OUTPUT_SET_NAME, struct.pack("<BB", channel, len(name_bytes)) + name_bytes)

    # -- model -------------------------------------------------------------

    def get_model_name(self) -> str:
        data = self.request(CMD_MODEL_GET_NAME)
        return data[1:1 + data[0]].decode("utf-8", "replace")

    def set_model_name(self, name: str) -> None:
        name_bytes = name.encode("utf-8")[:14]  # LEN_MODEL_NAME - 1
        self.request(CMD_MODEL_SET_NAME, struct.pack("<B", len(name_bytes)) + name_bytes)

    # -- timers ------------------------------------------------------------

    def get_timer(self, index: int) -> dict:
        data = self.request(CMD_TIMER_GET, struct.pack("<B", index))
        (mode, start, value, countdown_start, countdown_beep, minute_beep,
         persistent, show_elapsed, swtch, name_len) = struct.unpack_from("<BIiBBBBBHB", data, 0)
        return {
            "mode": mode,
            "start": start,
            "value": value,
            "countdown_start": countdown_start,
            "countdown_beep": countdown_beep,
            "minute_beep": bool(minute_beep),
            "persistent": persistent,
            "show_elapsed": bool(show_elapsed),
            "switch": swtch,
            "name": data[17:17 + name_len].decode("utf-8", "replace"),
        }

    def set_timer(self, index: int, timer: dict) -> None:
        name_bytes = str(timer.get("name", "")).encode("utf-8")[:7]  # LEN_TIMER_NAME - 1
        payload = struct.pack(
            "<BBIiBBBBBH",
            index,
            int(timer.get("mode", 0)),
            int(timer.get("start", 0)),
            int(timer.get("value", 0)),
            int(timer.get("countdown_start", 0)),
            int(timer.get("countdown_beep", 0)),
            1 if timer.get("minute_beep", False) else 0,
            int(timer.get("persistent", 0)),
            1 if timer.get("show_elapsed", False) else 0,
            int(timer.get("switch", 0)),
        ) + struct.pack("<B", len(name_bytes)) + name_bytes
        self.request(CMD_TIMER_SET, payload)

    # -- general settings --------------------------------------------------

    def get_general(self, setting) -> int:
        sid = GENERAL_IDS[setting] if isinstance(setting, str) else setting
        data = self.request(CMD_GENERAL_GET, struct.pack("<B", sid))
        return struct.unpack("<i", data)[0]

    def set_general(self, setting, value: int) -> None:
        sid = GENERAL_IDS[setting] if isinstance(setting, str) else setting
        self.request(CMD_GENERAL_SET, struct.pack("<Bi", sid, int(value)))

    # -- RF power ----------------------------------------------------------

    def get_rf_power(self, module: int) -> dict:
        mod_type, power = struct.unpack("<BB", self.request(CMD_RF_POWER_GET, struct.pack("<B", module)))
        return {"module_type": mod_type, "power": power}

    def set_rf_power(self, module: int, power: int) -> None:
        self.request(CMD_RF_POWER_SET, struct.pack("<BB", module, power))

    # -- flight modes ------------------------------------------------------

    def get_flight_mode(self, index: int) -> dict:
        data = self.request(CMD_FM_GET, struct.pack("<B", index))
        swtch, fade_in, fade_out, name_len = struct.unpack_from("<HBB B", data, 0)
        pos = 5  # 2 + 1 + 1 + 1 (name length byte)
        name = data[pos:pos + name_len].decode("utf-8", "replace")
        pos += name_len
        trim_count = data[pos]
        pos += 1
        values = list(struct.unpack_from("<%dh" % trim_count, data, pos)) if trim_count else []
        pos += 2 * trim_count
        modes = list(data[pos:pos + trim_count])
        return {
            "switch": swtch,
            "fade_in": fade_in,
            "fade_out": fade_out,
            "name": name,
            "trim_values": values,
            "trim_modes": modes,
        }

    def set_flight_mode(self, index: int, fm: dict) -> None:
        name_bytes = str(fm.get("name", "")).encode("utf-8")[:9]  # LEN_FLIGHT_MODE_NAME - 1
        payload = struct.pack(
            "<BHBB B",
            index,
            int(fm.get("switch", 0)),
            int(fm.get("fade_in", 0)),
            int(fm.get("fade_out", 0)),
            len(name_bytes),
        ) + name_bytes
        self.request(CMD_FM_SET, payload)

    def set_flight_mode_trim(self, index: int, trim: int, value: int, mode: int) -> None:
        self.request(CMD_FM_SET_TRIM, struct.pack("<BBhB", index, trim, value, mode))

    # -- inputs (expo) -----------------------------------------------------

    def input_count(self, input: int) -> int:
        return self.request(CMD_INPUT_COUNT, struct.pack("<B", input))[0]

    def get_input(self, input: int, line: int) -> dict:
        data = self.request(CMD_INPUT_GET, struct.pack("<BB", input, line))
        (src, scale, weight, offset, swtch, curve_type, curve_value,
         trim_source, mode, flight_modes, name_len) = \
            struct.unpack_from("<HHhhHhBbBHB", data, 0)
        name = data[EXPO_BLOB_FIXED:EXPO_BLOB_FIXED + name_len].decode("utf-8", "replace")
        return {
            "source": src,
            "scale": scale,
            "weight": weight,
            "offset": offset,
            "switch": swtch,
            "curve_type": curve_type,
            "curve_value": curve_value,
            "trim_source": trim_source,
            "side": mode,
            "flight_modes": flight_modes,
            "name": name,
        }

    def _expo_blob(self, expo: dict, name: str = "") -> bytes:
        name_bytes = name.encode("utf-8")[:5]  # LEN_EXPOMIX_NAME - 1
        return struct.pack(
            "<HHhhHhBbBHB",
            int(expo.get("source", 0)),
            int(expo.get("scale", 0)),
            int(expo.get("weight", 100)),
            int(expo.get("offset", 0)),
            int(expo.get("switch", 0)),
            int(expo.get("curve_type", 0)),
            int(expo.get("curve_value", 0)),
            int(expo.get("trim_source", 0)),
            int(expo.get("side", 3)),
            int(expo.get("flight_modes", 0)),
            len(name_bytes),
        ) + name_bytes

    def insert_input(self, input: int, line: int, expo: dict, name: str = "") -> int:
        payload = struct.pack("<BB", input, line) + self._expo_blob(expo, name)
        return self.request(CMD_INPUT_INSERT, payload)[0]

    def set_input(self, input: int, line: int, expo: dict, name: str = "") -> None:
        payload = struct.pack("<BB", input, line) + self._expo_blob(expo, name)
        self.request(CMD_INPUT_SET, payload)

    def delete_input(self, input: int, line: int) -> None:
        self.request(CMD_INPUT_DELETE, struct.pack("<BB", input, line))

    def move_input(self, input: int, line: int, up: bool) -> int:
        payload = struct.pack("<BBB", input, line, 0 if up else 1)
        return self.request(CMD_INPUT_MOVE, payload)[0]

    def get_input_name(self, input: int) -> str:
        data = self.request(CMD_INPUT_GET_NAME, struct.pack("<B", input))
        return data[1:1 + data[0]].decode("utf-8", "replace")

    def set_input_name(self, input: int, name: str) -> None:
        name_bytes = name.encode("utf-8")[:3]  # LEN_INPUT_NAME - 1
        self.request(CMD_INPUT_SET_NAME, struct.pack("<BB", input, len(name_bytes)) + name_bytes)

    # -- curves ------------------------------------------------------------

    def get_curve(self, index: int) -> dict:
        data = self.request(CMD_CURVE_GET, struct.pack("<B", index))
        ctype, smooth, points, count, flags, name_len = \
            struct.unpack_from("<BBbBBB", data, 0)
        pos = 6
        name = data[pos:pos + name_len].decode("utf-8", "replace")
        pos += name_len
        values = list(struct.unpack_from("<%db" % count, data, pos)) if count else []
        return {
            "type": ctype,
            "smooth": bool(smooth),
            "points": points,
            "point_count": count,
            "used": bool(flags & 0x01),
            "name": name,
            "values": values,
        }

    def set_curve(self, index: int, curve: dict, name: str = "", values=None) -> None:
        name_bytes = name.encode("utf-8")[:7]  # device truncates to its field size
        payload = struct.pack(
            "<BBBBB",
            index,
            int(curve.get("type", 0)),
            1 if curve.get("smooth", False) else 0,
            int(curve.get("point_count", 5)),
            len(name_bytes),
        ) + name_bytes
        if values:
            payload += struct.pack("<%db" % len(values), *values)
        self.request(CMD_CURVE_SET, payload)

    def set_curve_point(self, index: int, point: int, value: int) -> None:
        self.request(CMD_CURVE_SET_POINT, struct.pack("<BBh", index, point, value))

    def clear_curve(self, index: int) -> None:
        self.request(CMD_CURVE_CLEAR, struct.pack("<B", index))

    def mirror_curve(self, index: int) -> None:
        self.request(CMD_CURVE_MIRROR, struct.pack("<B", index))

    # -- logical switches --------------------------------------------------

    def get_logical_switch(self, index: int) -> dict:
        data = self.request(CMD_LS_GET, struct.pack("<B", index))
        func, v1, v2, v3, andsw, delay, duration, persist, family = \
            struct.unpack_from("<BhhhHBBBB", data, 0)
        return {
            "func": func,
            "v1": v1,
            "v2": v2,
            "v3": v3,
            "and_switch": andsw,
            "delay": delay,
            "duration": duration,
            "persist": bool(persist),
            "family": family,
        }

    def set_logical_switch(self, index: int, ls: dict) -> None:
        payload = struct.pack(
            "<BBhhhHBBB",
            index,
            int(ls.get("func", 0)),
            int(ls.get("v1", 0)),
            int(ls.get("v2", 0)),
            int(ls.get("v3", 0)),
            int(ls.get("and_switch", 0)),
            int(ls.get("delay", 0)),
            int(ls.get("duration", 0)),
            1 if ls.get("persist", False) else 0,
        )
        self.request(CMD_LS_SET, payload)

    # -- special functions -------------------------------------------------

    def get_custom_function(self, index: int) -> dict:
        data = self.request(CMD_CF_GET, struct.pack("<B", index))
        (swtch, func, active, repeat, kind, val, mode, param, val2, name_len) = \
            struct.unpack_from("<HBBbBhBBiB", data, 0)
        name = data[CF_BLOB_FIXED:CF_BLOB_FIXED + name_len].decode("utf-8", "replace")
        return {
            "switch": swtch,
            "func": func,
            "active": bool(active),
            "repetition": repeat,
            "data_kind": kind,
            "value": val,
            "mode": mode,
            "param": param,
            "value2": val2,
            "name": name,
        }

    def set_custom_function(self, index: int, cf: dict, name: str = "") -> None:
        use_name = cf.get("data_kind", 0) == 1
        name_bytes = name.encode("utf-8")[:9] if use_name else b""
        blob = struct.pack(
            "<HBBbBhBBiB",
            int(cf.get("switch", 0)),
            int(cf.get("func", 0)),
            1 if cf.get("active", False) else 0,
            int(cf.get("repetition", 0)),
            int(cf.get("data_kind", 2)),
            int(cf.get("value", 0)),
            int(cf.get("mode", 0)),
            int(cf.get("param", 0)),
            int(cf.get("value2", 0)),
            len(name_bytes),
        ) + name_bytes
        self.request(CMD_CF_SET, struct.pack("<B", index) + blob)

    # -- models ------------------------------------------------------------

    def list_models(self, start: int = 0) -> dict:
        data = self.request(CMD_MODEL_LIST, struct.pack("<B", start))
        total, count = data[0], data[1]
        pos = 2
        models = []
        for _ in range(count):
            flags = data[pos]
            name_len = data[pos + 1]
            pos += 2
            name = data[pos:pos + name_len].decode("utf-8", "replace")
            pos += name_len
            models.append({"name": name, "current": bool(flags & 0x01)})
        return {"total": total, "models": models}

    def list_all_models(self) -> list:
        out = []
        start = 0
        while True:
            page = self.list_models(start)
            out.extend(page["models"])
            if not page["models"]:
                break
            start += len(page["models"])
            if start >= page["total"]:
                break
        return out

    def select_model(self, index: int) -> None:
        """Select a model. The radio stores the settings and reboots (~3 s)."""
        self.request(CMD_MODEL_SELECT, struct.pack("<B", index))

    def create_model(self, name: str = "") -> int:
        """Create a new model on the SD card; returns its list index."""
        name_bytes = name.encode("utf-8")[:15]
        data = self.request(CMD_MODEL_CREATE,
                            struct.pack("<B", len(name_bytes)) + name_bytes)
        return data[0] if data else 0

    def duplicate_model(self, index: int) -> None:
        self.request(CMD_MODEL_DUPLICATE, struct.pack("<B", index))

    def delete_model(self, index: int) -> None:
        """Delete a model; the active model cannot be deleted."""
        self.request(CMD_MODEL_DELETE, struct.pack("<B", index))

    def rename_model(self, index: int, name: str) -> None:
        name_bytes = name.encode("utf-8")[:15]
        self.request(CMD_MODEL_RENAME,
                     struct.pack("<BB", index, len(name_bytes)) + name_bytes)

    # -- global variable runtime values ------------------------------------

    def get_gvar(self, gvar: int, flight_mode: int = 0) -> int:
        data = self.request(CMD_GVAR_GET, struct.pack("<BB", gvar, flight_mode))
        return struct.unpack("<h", data)[0]

    def set_gvar(self, gvar: int, value: int, flight_mode: int = 0) -> None:
        self.request(CMD_GVAR_SET,
                     struct.pack("<BBh", gvar, flight_mode, value))

    # -- events ------------------------------------------------------------

    def subscribe(self, settings_changed: bool = True) -> int:
        mask = SUBSCRIBE_SETTINGS_CHANGED if settings_changed else 0
        return self.request(CMD_SUBSCRIBE, struct.pack("<B", mask))[0]

    def wait_event(self, timeout: float = 1.0):
        """Return the next event as a dict, or None on timeout."""
        frame = self._read_frame(timeout)
        if frame is None:
            return None
        cmd, seq, length = frame[2], frame[3], frame[4]
        if cmd != CMD_EVENT or seq != 0:
            return None
        data = frame[6:5 + length]
        if not data:
            return None
        return {"type": data[0], "data": list(data[1:])}

    # -- generic settings tree ---------------------------------------------

    def param_list(self, root: int, path: str = "", start: int = 0) -> dict:
        """List the children of a settings node.

        `path` is a '/'-separated list of field names, an array element is
        addressed by putting its index right after the name
        (e.g. 'mixData/3/weight', 'timers/0'). Empty means the root.
        """
        path_bytes = path.encode("utf-8")
        if len(path_bytes) > 36:
            raise ValueError("path too long")
        payload = struct.pack("<BB", root, len(path_bytes)) + path_bytes + struct.pack("<B", start)
        data = self.request(CMD_PARAM_LIST, payload)

        total, count = data[0], data[1]
        pos = 2
        entries = []
        for _ in range(count):
            etype = data[pos]
            size = struct.unpack_from("<H", data, pos + 1)[0]
            elmts = struct.unpack_from("<H", data, pos + 3)[0]
            tag_len = data[pos + 5]
            pos += 6
            tag = data[pos:pos + tag_len].decode("utf-8", "replace")
            pos += tag_len
            entries.append({
                "tag": tag,
                "type": etype,
                "type_name": YAML_TYPES.get(etype, "?"),
                "size": size,
                "elements": elmts,
            })
        return {"total": total, "entries": entries}

    def param_list_all(self, root: int, path: str = "") -> list:
        out = []
        start = 0
        while True:
            page = self.param_list(root, path, start)
            out.extend(page["entries"])
            if not page["entries"]:
                break
            start += len(page["entries"])
            if start >= page["total"]:
                break
        return out

    def param_get(self, root: int, path: str) -> str:
        """Read a settings value; long values are fetched in chunks."""
        path_bytes = path.encode("utf-8")
        if len(path_bytes) > 36:
            raise ValueError("path too long")

        out = bytearray()
        offset = 0
        while True:
            payload = struct.pack("<BB", root, len(path_bytes)) + path_bytes \
                + struct.pack("<H", offset)
            data = self.request(CMD_PARAM_GET, payload)
            total, more = struct.unpack_from("<HB", data, 0)
            out += data[3:]
            if not more:
                break
            offset = len(out)
            if offset >= total:
                break
        return out.decode("utf-8", "replace")

    def param_set(self, root: int, path: str, value) -> None:
        """Write a settings value using its YAML representation."""
        path_bytes = path.encode("utf-8")
        if len(path_bytes) > 36:
            raise ValueError("path too long")
        value_bytes = str(value).encode("utf-8")
        payload = struct.pack("<BB", root, len(path_bytes)) + path_bytes + value_bytes
        self.request(CMD_PARAM_SET, payload)

    def param_export(self, root: int) -> str:
        """Dump the whole settings document (storage YAML representation)."""
        out = bytearray()
        offset = 0
        while True:
            payload = struct.pack("<BH", root, offset)
            data = self.request(CMD_PARAM_EXPORT, payload)
            total, more = struct.unpack_from("<HB", data, 0)
            out += data[3:]
            if not more:
                break
            offset = len(out)
            if offset >= total:
                break
        return out.decode("utf-8", "replace")

    def param_dump(self, root: int, path: str = "", prefix: str = "") -> list:
        """Recursively dump the settings tree as a list of (path, value)."""
        out = []
        for entry in self.param_list_all(root, path):
            if not entry["tag"]:
                continue
            child = (path + "/" + entry["tag"]) if path else entry["tag"]
            if entry["type"] in (5, 7) and entry["elements"] <= 1:
                out.extend(self.param_dump(root, child, prefix))
            elif entry["type"] == 5 and entry["elements"] > 1:
                for i in range(entry["elements"]):
                    out.extend(self.param_dump(root, child + "/" + str(i), prefix))
            else:
                try:
                    out.append((child, self.param_get(root, child)))
                except AppConfigError as exc:
                    out.append((child, "<%s>" % exc.name))
        return out

    @staticmethod
    def split_root(path: str):
        """'model/timers/0/name' -> (ROOT_MODEL, 'timers/0/name')."""
        if path.startswith("model/"):
            return ROOT_MODEL, path[6:]
        if path.startswith("radio/"):
            return ROOT_RADIO, path[6:]
        if path in ("model", "radio"):
            return (ROOT_MODEL if path == "model" else ROOT_RADIO), ""
        raise ValueError("path must start with 'model/' or 'radio/'")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

MIX_HELP = """mix fields:
  source        mixer source id (MIXSRC_*), e.g. 1 = stick 1
  weight        -1024..1024 (100 = 100%)
  offset        -1024..1024
  switch        switch source id (SWSRC_*), 0 = always
  curve_type    0 = none, 1 = expo, 2 = curve
  curve_value   curve number or expo value
  multiplex     0 = add, 1 = multiply, 2 = replace
  flight_modes  bitmask of flight modes
  delay_up / delay_down / speed_up / speed_down
  name          mix line name
"""

EXPO_HELP = """input fields:
  source        input source id (MIXSRC_*), e.g. stick/axis
  scale         input scaling, used for telemetry sources
  weight        -1024..1024 (100 = 100%)
  offset        -1024..1024
  switch        switch source id (SWSRC_*), 0 = always
  curve_type    0 = none, 1 = expo, 2 = curve
  curve_value   curve number or expo value
  trim_source   trim source id (0 = none)
  side          0 = positive, 1 = negative, 3 = both
  flight_modes  bitmask of flight modes
  name          input line name
"""


def parse_kv(pairs):
    out = {}
    for item in pairs or []:
        if "=" not in item:
            raise SystemExit("expected key=value, got '%s'" % item)
        key, value = item.split("=", 1)
        if value.lstrip("-").isdigit():
            out[key] = int(value)
        else:
            out[key] = value
    return out


def main():
    ap = argparse.ArgumentParser(description="EdgeTX App Config client")
    ap.add_argument("--port", required=True, help="serial port (COM7, /dev/ttyUSB0, ...)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=0.3, help="per-request timeout in seconds")
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("-v", "--verbose", action="store_true", help="dump raw frames")

    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="show protocol / model information")
    sub.add_parser("status", help="show storage dirty mask")
    sub.add_parser("save", help="wait until pending settings are stored")

    p = sub.add_parser("mixes", help="list all mixer lines of a channel")
    p.add_argument("--channel", type=int, required=True)

    p = sub.add_parser("get-mix", help="read one mixer line")
    p.add_argument("--channel", type=int, required=True)
    p.add_argument("--line", type=int, required=True)

    p = sub.add_parser("set-mix", help="modify an existing mixer line",
                       epilog=MIX_HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--channel", type=int, required=True)
    p.add_argument("--line", type=int, required=True)
    p.add_argument("--field", action="append", metavar="KEY=VALUE")
    p.add_argument("--name", default="")

    p = sub.add_parser("add-mix", help="insert a new mixer line",
                       epilog=MIX_HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--channel", type=int, required=True)
    p.add_argument("--line", type=int, required=True)
    p.add_argument("--field", action="append", metavar="KEY=VALUE")
    p.add_argument("--name", default="")

    p = sub.add_parser("del-mix", help="delete a mixer line")
    p.add_argument("--channel", type=int, required=True)
    p.add_argument("--line", type=int, required=True)

    p = sub.add_parser("get-output", help="read servo output settings")
    p.add_argument("--channel", type=int, required=True)

    p = sub.add_parser("set-output", help="write servo output settings")
    p.add_argument("--channel", type=int, required=True)
    p.add_argument("--min", type=int)
    p.add_argument("--max", type=int)
    p.add_argument("--offset", type=int)
    p.add_argument("--ppm-center", type=int)
    p.add_argument("--symetrical", type=int)
    p.add_argument("--revert", type=int)

    p = sub.add_parser("get-model-name", help="read the model name")
    p = sub.add_parser("set-model-name", help="write the model name")
    p.add_argument("name")
    p = sub.add_parser("get-timer", help="read a model timer")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("get-general", help="read a general setting")
    p.add_argument("setting", choices=sorted(GENERAL_IDS))

    p = sub.add_parser("set-general", help="write a general setting")
    p.add_argument("setting", choices=sorted(GENERAL_IDS))
    p.add_argument("value", type=int)

    p = sub.add_parser("get-rf", help="read RF power of a module slot")
    p.add_argument("--module", type=int, default=0)

    p = sub.add_parser("set-rf", help="write RF power of a module slot")
    p.add_argument("--module", type=int, default=0)
    p.add_argument("power", type=int)

    p = sub.add_parser("get-fm", help="read a flight mode")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("inputs", help="list all input lines of an input")
    p.add_argument("--input", type=int, required=True)

    p = sub.add_parser("get-input", help="read one input line")
    p.add_argument("--input", type=int, required=True)
    p.add_argument("--line", type=int, required=True)

    for name, help_text in (("set-input", "modify an existing input line"),
                            ("add-input", "insert a new input line")):
        p = sub.add_parser(name, help=help_text, epilog=EXPO_HELP,
                           formatter_class=argparse.RawDescriptionHelpFormatter)
        p.add_argument("--input", type=int, required=True)
        p.add_argument("--line", type=int, required=True)
        p.add_argument("--field", action="append", metavar="KEY=VALUE")
        p.add_argument("--name", default="")

    p = sub.add_parser("del-input", help="delete an input line")
    p.add_argument("--input", type=int, required=True)
    p.add_argument("--line", type=int, required=True)

    p = sub.add_parser("get-input-name", help="read an input name")
    p.add_argument("--input", type=int, required=True)

    p = sub.add_parser("set-input-name", help="write an input name")
    p.add_argument("--input", type=int, required=True)
    p.add_argument("name")

    p = sub.add_parser("get-curve", help="read a curve")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("set-curve", help="write a curve (type/points/name/values)")
    p.add_argument("--index", type=int, required=True)
    p.add_argument("--type", type=int, default=None, help="0 = standard, 1 = custom")
    p.add_argument("--smooth", type=int, default=None, help="0 = off, 1 = on")
    p.add_argument("--count", type=int, default=None, help="number of points (5..17)")
    p.add_argument("--name", default="")
    p.add_argument("--points", default=None, help="comma separated point values (-100..100)")

    p = sub.add_parser("set-curve-point", help="write one curve point")
    p.add_argument("--index", type=int, required=True)
    p.add_argument("--point", type=int, required=True)
    p.add_argument("value", type=int)

    p = sub.add_parser("clear-curve", help="reset a curve to 5 flat points")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("mirror-curve", help="mirror a curve")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("get-ls", help="read a logical switch")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("set-ls", help="write a logical switch")
    p.add_argument("--index", type=int, required=True)
    p.add_argument("--func", type=int, required=True)
    p.add_argument("--v1", type=int, default=0)
    p.add_argument("--v2", type=int, default=0)
    p.add_argument("--v3", type=int, default=0)
    p.add_argument("--and", dest="and_switch", type=int, default=0)
    p.add_argument("--delay", type=int, default=0)
    p.add_argument("--duration", type=int, default=0)
    p.add_argument("--persist", type=int, default=0)

    p = sub.add_parser("get-cf", help="read a special function")
    p.add_argument("--index", type=int, required=True)

    p = sub.add_parser("set-cf", help="write a special function")
    p.add_argument("--index", type=int, required=True)
    p.add_argument("--switch", type=int, required=True)
    p.add_argument("--func", type=int, required=True)
    p.add_argument("--active", type=int, default=1)
    p.add_argument("--repeat", type=int, default=0)
    p.add_argument("--kind", type=int, default=2, help="1 = name payload, 2 = numeric")
    p.add_argument("--value", type=int, default=0)
    p.add_argument("--mode", type=int, default=0)
    p.add_argument("--param", type=int, default=0)
    p.add_argument("--value2", type=int, default=0)
    p.add_argument("--name", default="")

    sub.add_parser("models", help="list models stored on the SD card")

    p = sub.add_parser("select-model", help="select a model (the radio reboots)")
    p.add_argument("index", type=int)

    p = sub.add_parser("events", help="subscribe and print change notifications")
    p.add_argument("--seconds", type=float, default=None)

    p = sub.add_parser("params", help="list settings below a path (model/... or radio/...)")
    p.add_argument("path", nargs="?", default="model")

    p = sub.add_parser("get-param", help="read one setting")
    p.add_argument("path")

    p = sub.add_parser("set-param", help="write one setting")
    p.add_argument("path")
    p.add_argument("value")

    p = sub.add_parser("dump-params", help="recursively dump settings")
    p.add_argument("path", nargs="?", default="model")

    p = sub.add_parser("export-params", help="dump the whole settings document")
    p.add_argument("path", nargs="?", default="model")

    p = sub.add_parser("create-model", help="create a new model on the SD card")
    p.add_argument("name", nargs="?", default="")

    p = sub.add_parser("dup-model", help="duplicate a model")
    p.add_argument("index", type=int)

    p = sub.add_parser("del-model", help="delete a model")
    p.add_argument("index", type=int)

    p = sub.add_parser("rename-model", help="rename a model")
    p.add_argument("index", type=int)
    p.add_argument("name")

    p = sub.add_parser("get-gvar", help="read a global variable value")
    p.add_argument("index", type=int)
    p.add_argument("--fm", type=int, default=0)

    p = sub.add_parser("set-gvar", help="write a global variable value")
    p.add_argument("index", type=int)
    p.add_argument("value", type=int)
    p.add_argument("--fm", type=int, default=0)

    sub.add_parser("demo", help="read info, model name and the first mixes")

    args = ap.parse_args()

    with AppConfigClient(args.port, args.baud, args.timeout, args.retries, args.verbose) as rc:
        if args.cmd == "info":
            for k, v in rc.get_info().items():
                print("%-16s %s" % (k, v))
        elif args.cmd == "status":
            print(rc.get_status())
        elif args.cmd == "save":
            print("saved" if rc.wait_saved() else "still pending after timeout")
        elif args.cmd == "mixes":
            for line in range(rc.mix_count(args.channel)):
                print("CH%d:%d %s" % (args.channel + 1, line, rc.get_mix(args.channel, line)))
        elif args.cmd == "get-mix":
            print(rc.get_mix(args.channel, args.line))
        elif args.cmd == "set-mix":
            rc.set_mix(args.channel, args.line, parse_kv(args.field), args.name)
            print("ok")
        elif args.cmd == "add-mix":
            print("inserted at line", rc.insert_mix(args.channel, args.line, parse_kv(args.field), args.name))
        elif args.cmd == "del-mix":
            rc.delete_mix(args.channel, args.line)
            print("ok")
        elif args.cmd == "get-output":
            print(rc.get_output(args.channel))
        elif args.cmd == "set-output":
            out = {}
            for key in ("min", "max", "offset", "ppm_center", "symetrical", "revert"):
                value = getattr(args, key)
                if value is not None:
                    out[key] = value
            rc.set_output(args.channel, out)
            print("ok")
        elif args.cmd == "get-model-name":
            print(rc.get_model_name())
        elif args.cmd == "set-model-name":
            rc.set_model_name(args.name)
            print("ok")
        elif args.cmd == "get-timer":
            print(rc.get_timer(args.index))
        elif args.cmd == "get-general":
            print(rc.get_general(args.setting))
        elif args.cmd == "set-general":
            rc.set_general(args.setting, args.value)
            print("ok")
        elif args.cmd == "get-rf":
            print(rc.get_rf_power(args.module))
        elif args.cmd == "set-rf":
            rc.set_rf_power(args.module, args.power)
            print("ok")
        elif args.cmd == "get-fm":
            print(rc.get_flight_mode(args.index))
        elif args.cmd == "inputs":
            for line in range(rc.input_count(args.input)):
                print("IN%d:%d %s" % (args.input + 1, line,
                                      rc.get_input(args.input, line)))
        elif args.cmd == "get-input":
            print(rc.get_input(args.input, args.line))
        elif args.cmd == "set-input":
            rc.set_input(args.input, args.line, parse_kv(args.field), args.name)
            print("ok")
        elif args.cmd == "add-input":
            print("inserted at line",
                  rc.insert_input(args.input, args.line, parse_kv(args.field), args.name))
        elif args.cmd == "del-input":
            rc.delete_input(args.input, args.line)
            print("ok")
        elif args.cmd == "get-input-name":
            print(rc.get_input_name(args.input))
        elif args.cmd == "set-input-name":
            rc.set_input_name(args.input, args.name)
            print("ok")
        elif args.cmd == "get-curve":
            print(rc.get_curve(args.index))
        elif args.cmd == "set-curve":
            curve = {}
            if args.type is not None:
                curve["type"] = args.type
            if args.smooth is not None:
                curve["smooth"] = bool(args.smooth)
            if args.count is not None:
                curve["point_count"] = args.count
            values = None
            if args.points:
                values = [int(v) for v in args.points.split(",") if v != ""]
            rc.set_curve(args.index, curve, args.name, values)
            print("ok")
        elif args.cmd == "set-curve-point":
            rc.set_curve_point(args.index, args.point, args.value)
            print("ok")
        elif args.cmd == "clear-curve":
            rc.clear_curve(args.index)
            print("ok")
        elif args.cmd == "mirror-curve":
            rc.mirror_curve(args.index)
            print("ok")
        elif args.cmd == "get-ls":
            print(rc.get_logical_switch(args.index))
        elif args.cmd == "set-ls":
            rc.set_logical_switch(args.index, {
                "func": args.func,
                "v1": args.v1,
                "v2": args.v2,
                "v3": args.v3,
                "and_switch": args.and_switch,
                "delay": args.delay,
                "duration": args.duration,
                "persist": bool(args.persist),
            })
            print("ok")
        elif args.cmd == "get-cf":
            print(rc.get_custom_function(args.index))
        elif args.cmd == "set-cf":
            rc.set_custom_function(args.index, {
                "switch": args.switch,
                "func": args.func,
                "active": bool(args.active),
                "repetition": args.repeat,
                "data_kind": args.kind,
                "value": args.value,
                "mode": args.mode,
                "param": args.param,
                "value2": args.value2,
            }, args.name)
            print("ok")
        elif args.cmd == "models":
            for i, model in enumerate(rc.list_all_models()):
                print("%3d %s %s" % (i, "*" if model["current"] else " ", model["name"]))
        elif args.cmd == "select-model":
            rc.select_model(args.index)
            print("model change queued, the radio will reboot in a few seconds")
        elif args.cmd == "events":
            rc.subscribe(True)
            print("subscribed; waiting for change notifications (Ctrl+C to stop)")
            deadline = None if args.seconds is None else time.monotonic() + args.seconds
            try:
                while deadline is None or time.monotonic() < deadline:
                    ev = rc.wait_event(0.5)
                    if ev is None:
                        continue
                    print("event type=%d data=%s" % (ev["type"], ev["data"]))
            except KeyboardInterrupt:
                print()
        elif args.cmd == "params":
            root, path = AppConfigClient.split_root(args.path)
            for entry in rc.param_list_all(root, path):
                size = ""
                if entry["type"] in (2, 3, 6, 9):
                    size = " %s%s" % (entry["size"], "b")
                elif entry["type"] == 4:
                    size = " %d chars" % (entry["size"] // 8)
                elif entry["type"] == 5 and entry["elements"] > 1:
                    size = " [%d]" % entry["elements"]
                print("  %-24s %-9s%s" % (entry["tag"], entry["type_name"], size))
        elif args.cmd == "get-param":
            root, path = AppConfigClient.split_root(args.path)
            print(rc.param_get(root, path))
        elif args.cmd == "set-param":
            root, path = AppConfigClient.split_root(args.path)
            rc.param_set(root, path, args.value)
            print("ok")
        elif args.cmd == "dump-params":
            root, path = AppConfigClient.split_root(args.path)
            prefix = args.path.rstrip("/")
            for name, value in rc.param_dump(root, path):
                print("%s/%s = %s" % (prefix, name, value))
        elif args.cmd == "export-params":
            root, _ = AppConfigClient.split_root(args.path)
            sys.stdout.write(rc.param_export(root))
        elif args.cmd == "create-model":
            print("new model index:", rc.create_model(args.name))
        elif args.cmd == "dup-model":
            rc.duplicate_model(args.index)
            print("ok")
        elif args.cmd == "del-model":
            rc.delete_model(args.index)
            print("ok")
        elif args.cmd == "rename-model":
            rc.rename_model(args.index, args.name)
            print("ok")
        elif args.cmd == "get-gvar":
            print(rc.get_gvar(args.index, args.fm))
        elif args.cmd == "set-gvar":
            rc.set_gvar(args.index, args.value, args.fm)
            print("ok")
        elif args.cmd == "demo":
            info = rc.get_info()
            print("EdgeTX %s  model=%r  channels=%d mixes=%d" % (
                info["version"], info["model_name"], info["max_channels"], info["max_mixes"]))
            for ch in range(min(info["max_channels"], 8)):
                for line in range(rc.mix_count(ch)):
                    print("  CH%d:%d %s" % (ch + 1, line, rc.get_mix(ch, line)))


if __name__ == "__main__":
    main()
