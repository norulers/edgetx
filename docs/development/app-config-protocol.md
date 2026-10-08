# App Config serial protocol

The **App Config** protocol lets an external application (phone/PC) read and
write radio and model settings over a plain serial link. Because the protocol
is transport agnostic it works over a wired UART adapter, a USB-to-UART bridge
or a transparent Bluetooth / WiFi-to-UART module.

Currently supported:

| Group | Operations |
| --- | --- |
| Mixer (`mixData`) | count, get, insert, modify, delete, move |
| Inputs (`expoData`) | count, get, insert, modify, delete, move, input name |
| Outputs (`limitData`) | get / set limits, subtrim, ppm center, direction, name |
| Curves | get / set type, smooth, point count, name and point values; clear, mirror |
| Logical switches | get / set |
| Special functions | get / set |
| Model | name, list models, select, create, duplicate, delete, rename |
| Timers | get / set |
| Flight modes | get / set name, switch, fade; trim values and modes |
| Global variables | definitions (via generic access) and runtime values |
| Radio settings | volume, beep/wav/vario/background volume, haptic, backlight |
| RF | module type + RF power for supported module protocols |
| Storage | dirty-mask polling so the host knows when settings are persisted |
| Events | optional push notification when the radio side changes settings |
| Generic settings | reflection based read/write of **every** model and radio setting (see below) |

Not supported (yet): telemetry *views*, global variable runtime values,
bluetooth pairing, SD card manager actions and model templates. Model and
radio settings themselves are all reachable through the generic access below.

## Enabling it on the radio

1. **Radio settings → Hardware → Serial port** (`AUX1` / `AUX2`) and select
   **App Config**.
2. Enable the port power toggle if your adapter/module is powered from the AUX
   connector.
3. Fixed link parameters: **115200 8N1, full duplex (TX + RX)**.

Notes:

* Only AUX ports are offered; the USB VCP port is intentionally excluded.
* The mode can only be assigned to one port at a time.
* The port is exclusive: it cannot be used for telemetry mirror / passthrough
  at the same time.
* The AUX port is a **3.3 V TTL** interface. Never connect a 5 V-level module
  directly to the radio's RX pin.

On an H750 based radio for example, `AUX1` is `UART5` with `PB6` = TX and
`PB5` = RX, and `PB7` controls the AUX port power.

## Frame format

All multi-byte fields are **little endian**.

```
+-------+-------+-------+-------+-------+---------------+---------+---------+
| SYNC0 | SYNC1 |  CMD  |  SEQ  |  LEN  |    PAYLOAD    | CRC16lo | CRC16hi |
| 0xA5  | 0x5A  |  1 B  |  1 B  |  1 B  |   LEN bytes   |   1 B   |   1 B   |
+-------+-------+-------+-------+-------+---------------+---------+---------+
```

* `LEN` is limited to 64 bytes, so a full frame is at most 71 bytes. This keeps
  the protocol usable over BLE links with a 20-byte ATT MTU: the parser is a
  stream state machine and reassembles arbitrarily fragmented frames.
* CRC is **CRC-16/CCITT-FALSE** (polynomial `0x1021`, init `0xFFFF`, no
  reflection, no final xor) computed over `CMD`, `SEQ`, `LEN` and `PAYLOAD`.

Reference implementation of the CRC:

```python
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc
```

### Responses

A response uses the **same `CMD` and `SEQ`** as the request. The first payload
byte is a status code, the remaining bytes are command specific:

```
PAYLOAD = [ STATUS | DATA... ]
```

The host must match a response by `(CMD, SEQ)` and ignore frames that do not
match the request it is waiting for.

| Status | Value | Meaning |
| --- | --- | --- |
| `OK` | 0x00 | success |
| `ERROR` | 0x01 | generic failure |
| `BAD_CMD` | 0x02 | unknown command |
| `BAD_PARAM` | 0x03 | malformed payload |
| `OUT_OF_RANGE` | 0x04 | index/value out of range |
| `UNSUPPORTED` | 0x05 | valid but not supported on this radio |
| `NOT_READY` | 0x06 | radio not ready (mixer not started yet) |

### Reliability

* The host increments `SEQ` for every request (it wraps at 255).
* If no response arrives within the host timeout, the frame is retransmitted
  with the **same** `SEQ`.
* For all *mutating* commands the firmware caches the last `(CMD, SEQ)` answer
  and resends it without re-executing the command, so a retransmission can
  never corrupt the model twice.
* Read commands are idempotent and are simply re-executed.

A command is considered mutating for the following opcodes: `0x12`–`0x15`,
`0x18`–`0x1B`, `0x1D`, `0x21`, `0x22`, `0x24`–`0x27`, `0x29`, `0x2C`, `0x31`,
`0x33`, `0x35`–`0x39`, `0x41`, `0x43`, `0x51`, `0x52`, `0x70`, `0x82` and
`0x87`.

Sequence number `0` is reserved for unsolicited events (see `0x00`), the host
must therefore use `1..255` for its requests.

### Persistence

Changes are marked dirty and written to the SD card by the normal storage
machinery (about 2 seconds after the last modification). Poll `GET_STATUS` and
wait for `dirty == 0` when the host needs to confirm persistence.

## Value encoding

| Type | Encoding |
| --- | --- |
| `u8` / `u16` / `u32` | unsigned little endian |
| `s8` / `s16` / `s32` | two's complement little endian |
| name | `u8` length followed by raw bytes (UTF-8, **not** NUL terminated) |
| mixer `source`, `switch`, `flight_modes` | the raw firmware enum values (`MIXSRC_*`, `SWSRC_*` bitmask) |
| mixer `weight`, `offset`, `curve_value` | Lua `model.getMix()` semantics: `-1024..1024` where `±1024` means "use a source instead of a constant" |
| output `min` / `max` | Lua `model.getOutput()` semantics: `min ∈ [-1000, 0]`, `max ∈ [0, 1000]`, unit is 0.1 % (`-1000` = `-100.0 %`) |
| output `offset` | subtrim in 0.1 % |
| output `ppm_center` | offset from 1500 µs |
| output `curve` | `0` = none, `N` = curve `N` (1-based) |

Names are truncated to the field size of the target radio (model name, channel
name, timer name and flight mode name lengths depend on the display size). The
last byte of the field is reserved for the terminating NUL, exactly like the
on-screen text editors.

## Commands

### 0x01 — GET_INFO

Request: *(empty)*

Response data:

```
[0]  u8  protocol version (= 1)
[1]  u8  firmware version major
[2]  u8  firmware version minor
[3]  u8  firmware version revision
[4]  u8  MAX_OUTPUT_CHANNELS
[5]  u8  MAX_MIXERS
[6]  u8  MAX_FLIGHT_MODES
[7]  u8  MAX_TIMERS
[8]  u8  number of module slots
[9]  u8  model name length
[10] ..  model name
```

### 0x10 — MIX_COUNT

Request: `[u8 channel]` (0-based)

Response data: `[u8 line count]`

### 0x11 — MIX_GET

Request: `[u8 channel][u8 line]`

Response data — mixer line blob:

```
[0]   u16 srcRaw
[2]   s16 weight
[4]   s16 offset
[6]   u16 swtch
[8]   u8  curve type
[9]   s16 curve value
[11]  u8  multiplex (0 = add, 1 = multiply, 2 = replace)
[12]  u16 flight modes bitmask
[14]  u8  flags: bit0 carryTrim, bits1-2 mixWarn,
              bit3 delayPrec, bit4 speedPrec
[15]  u8  delayUp
[16]  u8  delayDown
[17]  u8  speedUp
[18]  u8  speedDown
[19]  u8  name length
[20]  ..  name
```

The fixed part is 20 bytes. The same blob (without the `channel`/`line`
prefix) is used by `MIX_SET` and `MIX_INSERT`.

### 0x12 — MIX_INSERT

Request: `[u8 channel][u8 line][mixer line blob]`

`line` is the position within the channel, `0` inserts before the first line
and `line == MIX_COUNT(channel)` appends.

Response data: `[u8 new line index]` (absolute index inside `mixData`).

### 0x13 — MIX_SET

Request: `[u8 channel][u8 line][mixer line blob]`

Response data: *(empty)*

### 0x14 — MIX_DELETE

Request: `[u8 channel][u8 line]`

Response data: *(empty)*

### 0x15 — MIX_MOVE

Request: `[u8 channel][u8 line][u8 direction]` (`0` = up, `1` = down)

Response data: `[u8 new line index within the channel]`

### 0x16 — INPUT_COUNT

Request: `[u8 input]` (0-based)

Response data: `[u8 line count]`

### 0x17 — INPUT_GET

Request: `[u8 input][u8 line]`

Response data — input (expo) line blob:

```
[0]   u16 srcRaw
[2]   u16 scale
[4]   s16 weight
[6]   s16 offset
[8]   u16 swtch
[10]  u8  curve type
[11]  s16 curve value
[13]  s8  trim source
[14]  u8  side (0 = positive, 1 = negative, 3 = both)
[15]  u16 flight modes bitmask
[17]  u8  name length
[18]  ..  name
```

The fixed part is 18 bytes; the same blob is used by `INPUT_SET` and
`INPUT_INSERT` (prefixed with `input`, `line`).

### 0x18 — INPUT_INSERT

Request: `[u8 input][u8 line][input line blob]`

Response data: `[u8 new line index]` (absolute index inside `expoData`).

### 0x19 — INPUT_SET

Request: `[u8 input][u8 line][input line blob]`

Response data: *(empty)*

### 0x1A — INPUT_DELETE

Request: `[u8 input][u8 line]`

Response data: *(empty)*

### 0x1B — INPUT_MOVE

Request: `[u8 input][u8 line][u8 direction]` (`0` = up, `1` = down)

Response data: `[u8 new line index within the input]`

Moving the first/last line of an input re-assigns it to the neighbouring input,
exactly like the on-radio editor.

### 0x1C — INPUT_GET_NAME

Request: `[u8 input]`

Response data: `[u8 name length][name...]`

### 0x1D — INPUT_SET_NAME

Request: `[u8 input][u8 name length][name...]`

Response data: *(empty)*

### 0x20 — OUTPUT_GET

Request: `[u8 channel]`

Response data:

```
[0]  s16 min
[2]  s16 max
[4]  s16 offset
[6]  s16 ppm center
[8]  u8  symetrical
[9]  u8  revert
[10] s8  curve (0 = none)
[11] u8  name length
[12] ..  name
```

### 0x21 — OUTPUT_SET

Request:

```
[0] u8  channel
[1] s16 min
[3] s16 max
[5] s16 offset
[7] s16 ppm center
[9] u8  symetrical
[10]u8  revert
```

Response data: *(empty)*

### 0x22 — OUTPUT_SET_NAME

Request: `[u8 channel][u8 name length][name...]`

Response data: *(empty)*

### 0x23 — CURVE_GET

Request: `[u8 curve index]`

Response data:

```
[0]  u8 type (0 = standard, 1 = custom)
[1] u8 smooth
[2] s8 points parameter (number of points - 5)
[3] u8 number of points
[4] u8 flags: bit0 = curve is used
[5] u8 name length
[6] .. name
[..] s8 point values (number of points entries)
```

The point values are the curve's Y values. For custom curves the X positions
are generated by the firmware and are not transmitted.

### 0x24 — CURVE_SET

Request:

```
[0] u8  curve index
[1] u8  type (0 = standard, 1 = custom)
[2] u8  smooth
[3] u8  number of points (2..17)
[4] u8  name length
[5] ..  name
[..]    optional: s8 point values (number of points entries)
```

Response data: *(empty)*

Changing the type or the number of points re-samples the existing curve shape
onto the new resolution and shifts the point pool, exactly like the on-radio
curve editor. When no point values follow, the re-sampled shape is kept.

### 0x25 — CURVE_SET_POINT

Request: `[u8 curve index][u8 point index][s16 value]` (value is clipped to
`-100..100`)

Response data: *(empty)*

### 0x26 — CURVE_CLEAR

Request: `[u8 curve index]`

Response data: *(empty)*

Resets the curve to a flat 5-point standard curve.

### 0x27 — CURVE_MIRROR

Request: `[u8 curve index]`

Response data: *(empty)*

### 0x28 — LS_GET

Request: `[u8 logical switch index]`

Response data:

```
[0]  u8  func
[1]  s16 v1
[3]  s16 v2
[5]  s16 v3
[7]  s16 and switch
[9]  u8  delay
[10] u8  duration
[11] u8  flags: bit0 = persistent
[12] u8  parameter family (LS_FAMILY_*, read-only helper)
```

`v1`/`v2`/`v3` are the internal raw values, identical to what the firmware
stores for the given function family. There is no `LS_FUNC_NONE`: a logical
switch with `func == 0` is still a valid "AND" style function, use a zeroed
setting to disable it.

### 0x29 — LS_SET

Request:

```
[0]  u8  index
[1]  u8  func
[2]  s16 v1
[4]  s16 v2
[6]  s16 v3
[8]  s16 and switch
[10] u8  delay
[11] u8  duration
[12] u8  flags: bit0 = persistent
```

Response data: *(empty)*

### 0x2B — CF_GET

Request: `[u8 special function index]`

Response data:

```
[0]  u16 swtch
[2]  u8  func
[3]  u8  active
[4]  s8  repetition
[5]  u8  data kind: 0 = slot empty, 1 = name payload, 2 = numeric payload
[6]  s16 value
[8]  u8  mode
[9]  u8  param
[10] s32 value2
[14] u8  name length
[15] .. name
```

The name and the numeric payload share the same union in the firmware, so only
one of them is meaningful: `data kind` tells the host which one. `data kind == 0`
means the switch field is 0, i.e. the function is not assigned.

### 0x2C — CF_SET

Request: `[u8 index]` followed by the same 15-byte blob as `CF_GET`, plus the
optional name bytes when `data kind == 1`.

Response data: *(empty)*

### 0x30 — MODEL_GET_NAME

Request: *(empty)*

Response data: `[u8 name length][name...]`

### 0x31 — MODEL_SET_NAME

Request: `[u8 name length][name...]`

Response data: *(empty)*

### 0x32 — TIMER_GET

Request: `[u8 timer index]`

Response data:

```
[0]  u8  mode
[1]  u32 start (seconds)
[5]  s32 value (initial value)
[9]  u8  countdown start
[10] u8  countdown beep
[11] u8  minute beep
[12] u8  persistent
[13] u8  show elapsed
[14] u16 switch
[16] u8  name length
[17] ..  name
```

### 0x33 — TIMER_SET

Request: `[u8 index][u8 mode][u32 start][s32 value][u8 countdownStart]`
`[u8 countdownBeep][u8 minuteBeep][u8 persistent][u8 showElapsed]`
`[u16 switch][u8 name length (optional)][name...]`

Response data: *(empty)*

### 0x34 — MODEL_LIST

Request: `[u8 start index]`

Response data:

```
[0] u8 total number of models
[1] u8 number of entries in this response
then, for each entry:
    u8 flags: bit0 = this is the active model
    u8 name length
    .. name
```

Entries are returned in pages (as many as fit into one frame), so the host
should request again with `start += returned` until `start >= total`.

### 0x35 — MODEL_SELECT

Request: `[u8 model index]` (index in the same order as `MODEL_LIST`)

Response data: *(empty)*

**Selecting a model reboots the radio.** The settings are marked dirty, the
response is sent immediately, and after ~3 seconds the radio restarts and comes
up with the newly selected model. During that window further commands are
ignored and the serial link drops, so the host should close the port after the
acknowledgement.

The radio does *not* switch models in place: doing so would tear down the
running UI (screens and widgets) which is not safe to trigger from a serial
command.

### 0x36 — MODEL_CREATE

Request: `[u8 name length][name...]` (the name may be empty)

Response data: `[u8 index of the new model in the list]`

Creates a new model file on the SD card with default content. The firmware
copies `/MODELS/model.yml` (the empty model template shipped with the official
SD card contents) to the next free `modelNN.yml` name, so the currently
selected model is left untouched — the radio's own `createModel()` would
switch to the new model and would not be safe to call from here. Fails with
`ERROR` when the template file is missing.

The name is stored in `labels.yml`; the `header.name` inside the model file is
only updated once the model is opened on the radio. Names longer than the
radio allows are clipped (15 characters on colour screen radios, 12 or 10 on
the smaller ones) so the host does not need to know the limit.

### 0x37 — MODEL_DUPLICATE

Request: `[u8 index]`

Response data: *(empty)*

Copies the model file (and, on colour screen radios, its custom screens) and
adds it to the list.

### 0x38 — MODEL_DELETE

Request: `[u8 index]`

Response data: *(empty)*

The **active** model cannot be deleted — select another model first, otherwise
the command fails with `ERROR`. The model file is moved to `/MODELS/DELETED/`
rather than unlinked, exactly like on the radio.

### 0x39 — MODEL_RENAME

Request: `[u8 index][u8 name length][name...]`

Response data: *(empty)*

Renaming the active model also updates `g_model.header.name` so the change is
immediately visible on the radio. For every model the new name is written to
`labels.yml`; the name stored inside the model file is refreshed the next time
that model is opened on the radio. Names are clipped as described for
`MODEL_CREATE`.

### 0x40 — GENERAL_GET

Request: `[u8 setting id]`

Response data: `[s32 value]`

| Id | Name | Range | Notes |
| --- | --- | --- | --- |
| 1 | `volume` | 0..`VOLUME_LEVEL_MAX` (23) | main volume |
| 2 | `beep_volume` | 0..4 | |
| 3 | `wav_volume` | 0..4 | |
| 4 | `vario_volume` | 0..4 | |
| 5 | `background_volume` | 0..4 | |
| 6 | `haptic_strength` | 0..4 | |
| 7 | `haptic_length` | 0..4 | |
| 8 | `beep_length` | 0..4 | |
| 9 | `backlight` | 1..`BACKLIGHT_LEVEL_MAX` | |

### 0x41 — GENERAL_SET

Request: `[u8 setting id][s32 value]`

Response data: *(empty)*

### 0x42 — RF_POWER_GET

Request: `[u8 module slot]` (`0` = internal, `1` = external)

Response data: `[u8 module type][u8 power]`

### 0x43 — RF_POWER_SET

Request: `[u8 module slot][u8 power]`

Response data: *(empty)*

`power` meaning depends on the module type returned by `RF_POWER_GET`:

| Module type | Power encoding |
| --- | --- |
| `MODULE_TYPE_MULTIMODULE` | `0` = full power, `1` = low power mode |
| `MODULE_TYPE_XJT_PXX1`, `MODULE_TYPE_ISRM_PXX2`, `MODULE_TYPE_R9M_*` | `0` = 10 mW, `1` = 100 mW, `2` = 500 mW, `3` = 1 W |
| `MODULE_TYPE_FLYSKY_AFHDS3` | protocol specific power index |

Other module types (PPM, DSM2, Crossfire/ELRS, Ghost, …) return
`UNSUPPORTED`, as their power is either not configurable or handled by the
module itself.

### 0x50 — FM_GET

Request: `[u8 flight mode index]`

Response data:

```
[0]  u16 switch
[2]  u8  fade in
[3]  u8  fade out
[4]  u8  name length
[5]  ..  name
[..] u8  trim count (0 for flight mode 0)
[..] s16 trim values (trim count entries)
[..] u8  trim modes (trim count entries)
```

### 0x51 — FM_SET

Request: `[u8 index][u16 switch][u8 fadeIn][u8 fadeOut]`
`[u8 name length (optional)][name...]`

Response data: *(empty)*

Flight mode 0 is always active and therefore rejects a non-zero switch.

### 0x52 — FM_SET_TRIM

Request: `[u8 index][u8 trim][s16 value][u8 mode]`

Response data: *(empty)*

Flight mode 0 has no trim offsets and is rejected.

### 0x60 — GET_STATUS

Request: *(empty)*

Response data: `[u8 storage dirty mask][u8 mixer running]`

`dirty` is non-zero while a settings write is still pending.

### 0x70 — SUBSCRIBE

Request: `[u8 event mask]` (`bit0` = settings changed notifications)

Response data: `[u8 accepted mask]`

The subscription is not persistent: it is cleared when the serial mode is
re-entered and when the radio reboots.

### 0x83 — PARAM_EXPORT

Request: `[u8 root][u16 offset]`

Response data: `[u16 total length][u8 more][text chunk]`

Returns the **whole settings document** in the storage YAML representation, one
44 byte chunk at a time. Use `offset += <bytes received>` until `more` is zero.
This is the one-call way to fetch everything the radio stores, and the result
can be compared directly against the files under `RADIO/` and `MODELS/` on the
SD card.

Because the generator always walks the document from the start, exporting a
full model takes one pass per chunk; for a typical model this is a few hundred
chunks. When only a few values are needed, `PARAM_GET` is cheaper.

There is deliberately no corresponding bulk *import*: uploading a whole
document would silently overwrite settings the user may have changed on the
radio. Use `PARAM_SET` per field, or `PARAM_EXPORT` + diff.

### 0x86 — GVAR_GET

Request: `[u8 gvar index][u8 flight mode]`

Response data: `[s16 value]`

Reads the **runtime** value of a global variable (the stored per-flight-mode
value, not the GVar definition which is reachable through `PARAM_GET` at
`flightModeData/<fm>/gvars/<gv>`).

### 0x87 — GVAR_SET

Request: `[u8 gvar index][u8 flight mode][s16 value]`

Response data: *(empty)*

The value is in the GVar's own units. The write goes directly into the flight
mode's GVar table so that no on-screen popup can be triggered from the serial
context.

### 0x00 — EVENT (radio → host)

Sent unsolicited by the radio with **sequence number 0**. Request direction use
is rejected with `BAD_CMD`.

```
[0] u8 event type
[1] .. event data
```

| Event type | Data | Meaning |
| --- | --- | --- |
| 1 | `u8 dirty mask` | settings changed somewhere on the radio (`bit0` general, `bit1` model) |

Events are coalesced and rate limited to at most one frame every 200 ms, so the
host must treat them as a hint to re-read the data it cares about rather than as
a complete change log. They are only sent while the host is subscribed.

Because the host matches responses by `(cmd, seq)` and events always use
`seq == 0`, events never collide with a pending request.

## Generic settings access (0x80 – 0x82)

The commands above cover the settings that need dedicated logic. Everything
else — the full model and radio configuration — is reachable through a
**reflection based** interface, so the host can reproduce every on-radio screen
without a bespoke command per field.

The firmware already describes each stored field in the YAML node tables
(name, type, bit width, array bounds and the value names of every enum) and can
read and write them with exactly the same code that loads and saves the SD card
files. These three commands expose that machinery.

### Paths

A path is a `'/'`-separated list of field names. An array element is addressed
by putting its index directly after the field name:

```
timers/0/name
mixData/3/weight
moduleData/1/multi/rfProtocol
flightModeData/2/fadeIn
```

The first request byte selects the root:

| Root | Value | Contents |
| --- | --- | --- |
| Radio | 0 | `g_eeGeneral` (`RadioData`) — all radio settings |
| Model | 1 | `g_model` (`ModelData`) — all model settings |

An empty path addresses the root itself, which is the way to discover the
top-level sections.

### Values

Values use the **exact textual representation of the storage YAML files**:

* numbers are decimal (`100`, `-30`)
* enums are written by name (`SA`, `ADD`, `TELEMETRY_MIRROR`, `OFF`)
* switch and source fields use their YAML notation (`"SA"`, `"!SB"`, `Rud`)
* strings are the raw text without quotes

Because enums are named, the host can present exactly the same choices as the
radio screen, and a read-modify-write cycle is always safe: read the current
value, change it, write it back with the same notation.

### 0x80 — PARAM_LIST

Request: `[u8 root][u8 path length][path][u8 start index]`

Response data:

```
[0] u8 total number of children
[1] u8 number of entries in this response
then, for each entry:
    u8  type (YamlDataType: 1 idx, 2 signed, 3 unsigned, 4 string,
              5 array, 6 enum, 7 union, 8 padding, 9 custom)
    u16 size in bits (size of one element for arrays)
    u16 element count (1 for structs and unions, > 1 for arrays)
    u8  tag length
    ..  tag
```

Entries are paged; request again with `start += returned` until
`start >= total`. Descending into an entry of type `union`/`array` with
`elements == 1` lists its fields; an entry with `elements > 1` is a real array
and each element is addressed with an index in the path.

### 0x81 — PARAM_GET

Request: `[u8 root][u8 path length][path][u16 offset]`

Response data: `[u16 total length][u8 more][text chunk]`

The value is returned in chunks of 44 bytes. While `more` is non-zero, request
the next chunk with `offset += <bytes received>`.

A path must end with a field name; listing or dumping a whole container is done
by walking `PARAM_LIST` recursively.

### 0x82 — PARAM_SET

Request: `[u8 root][u8 path length][path][value text]`

Response data: *(empty)*

The value is validated against the field type before it is applied:

* `signed` / `unsigned` values must fit into the field's bit width
  (`OUT_OF_RANGE` otherwise)
* `string` values must fit into the field (`BAD_PARAM` otherwise)
* `enum` and `custom` fields are parsed by the same code that parses a model
  file

Structs, arrays and unions must be written field by field and are rejected with
`UNSUPPORTED`.

Writing to the model root requires the mixer to have been started
(`NOT_READY` before that), and the write is protected by the mixer lock.

### Worked example

Reading the model name and moving a mix line's weight:

```console
$ python3 tools/app_config_client.py --port COM7 get-param model/header/name
TEST

$ python3 tools/app_config_client.py --port COM7 params model/mixData/0
  destCh                   unsigned  5b
  srcRaw                   custom
  carryTrim                unsigned  1b
  mixWarn                  unsigned  2b
  mltpx                    enum      2b
  delayPrec                unsigned  1b
  speedPrec                unsigned  1b
  flightModes              custom
  weight                   signed    11b
  offset                   signed    11b
  swtch                    custom
  curve                    union
  delayUp                  unsigned  8b
  ...

$ python3 tools/app_config_client.py --port COM7 set-param model/mixData/0/weight 75
ok
```

A full dump is useful to mirror the whole configuration:

```console
$ python3 tools/app_config_client.py --port COM7 dump-params radio > radio.yml
$ python3 tools/app_config_client.py --port COM7 dump-params model/telemetrySensors > sensors.yml
```

## Threading / safety model

The protocol is serviced from a 10 ms FreeRTOS software timer, and only once
the mixer has been started (a few hundred milliseconds after power-on). Frames
arriving before that are ignored; the host should therefore retry until the
first `GET_INFO` succeeds. As a second safety net every mutating command checks
`mixerTaskStarted()` itself and answers `NOT_READY` when the radio is not ready
for model edits yet.

* `MIX_INSERT`, `MIX_DELETE`, `MIX_MOVE`, `INPUT_INSERT`, `INPUT_DELETE` and
  `INPUT_MOVE` use the same helpers as the on-radio editors, which stop and
  restart the mixer internally.
* In-place updates (mixer and input line fields, outputs, timers, flight modes,
  logical switches, special functions, curve points) are wrapped in
  `mixerTaskLock()` / `mixerTaskUnlock()` so the mixer can never read a
  half-written structure.
* `CURVE_SET` may resize a curve, which shifts the shared point pool. The
  firmware performs the same `moveCurve()` + re-sampling sequence as the curve
  editor and refuses the change if the pool would overflow (`OUT_OF_RANGE`).
* `MODEL_SELECT` only records the new selection and schedules a reboot; it never
  tears down the running UI.
* Do not issue commands concurrently from the host; the protocol is strictly
  request/response, and each request must be acknowledged before the next one
  is sent.

## Python client

`tools/app_config_client.py` implements the protocol and doubles as a CLI:

```console
$ python3 tools/app_config_client.py --port /dev/ttyUSB0 info
$ python3 tools/app_config_client.py --port COM7 demo
$ python3 tools/app_config_client.py --port COM7 mixes --channel 2
$ python3 tools/app_config_client.py --port COM7 add-mix --channel 2 --line 0 \
      --field source=1 --field weight=100 --name ELE
$ python3 tools/app_config_client.py --port COM7 inputs --input 0
$ python3 tools/app_config_client.py --port COM7 add-input --input 0 --line 0 \
      --field source=1 --field weight=80 --name AIL
$ python3 tools/app_config_client.py --port COM7 get-curve --index 0
$ python3 tools/app_config_client.py --port COM7 set-curve --index 0 \
      --count 5 --points -100,-50,0,50,100
$ python3 tools/app_config_client.py --port COM7 set-ls --index 0 --func 1 --v1 5 --v2 6
$ python3 tools/app_config_client.py --port COM7 set-cf --index 0 --switch 5 \
      --func 12 --value 3
$ python3 tools/app_config_client.py --port COM7 models
$ python3 tools/app_config_client.py --port COM7 set-output --channel 0 --min -800 --max 800
$ python3 tools/app_config_client.py --port COM7 set-general volume 18
$ python3 tools/app_config_client.py --port COM7 events
$ python3 tools/app_config_client.py --port COM7 save
```

It is also importable:

```python
from app_config_client import AppConfigClient

with AppConfigClient('/dev/ttyUSB0') as rc:
    rc.set_model_name('TEST')
    rc.insert_mix(0, 0, {'source': 1, 'weight': 80}, name='AIL')
    for model in rc.list_all_models():
        print(model['name'], 'current' if model['current'] else '')
    rc.wait_saved()
```

For a Bluetooth transparent module, connect the PC to the module first (it
appears as a virtual COM port for classic SPP modules) and use that port name.
