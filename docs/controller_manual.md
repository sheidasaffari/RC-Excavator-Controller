# RC Excavator Serial Controller

This document accompanies `RC_Excavator_Controller.ino`, the consolidated firmware for commanding an RC excavator from a computer or phone. It is intended for researchers who need to automate excavator motions without using the handheld radio transmitter.

---

## Hardware setup

| Component | Model / role |
|-----------|----------------|
| Microcontroller | **Arduino Mega 2560** |
| RC receiver (on excavator) | **RadioLink R12DS** (2.4 GHz, 12-channel) |
| Wireless serial link | **HC-05** Bluetooth module |
| Actuator interface | Mega PWM outputs emulate R12DS channel signals |

The Mega does not replace the R12DS in normal RC flight/drive mode. In automated mode, the firmware drives PWM outputs wired into the excavator’s control channels—the same signals the R12DS would produce when operated manually.

### Power and connections

- Excavator battery/power must be **ON** during motion tests.
- Mega powered via USB (programming/debug) or external 7–12 V supply.
- **Common ground** between Mega, HC-05, and excavator control board.

---

## Wiring reference

### PWM outputs (Mega → excavator control channels)

| Mega pin | Function |
|----------|----------|
| 2 | Bucket |
| 3 | Main arm (boom) |
| 4 | Middle arm (stick) |
| 5 | Body rotation (cab swing) |
| 7 | Hydraulic pump / shared enable |
| 8 | Left track |
| 9 | Right track |
| 32 | Digital auxiliary (from original project wiring) |
| A0 | Analog input (reserved for sensors) |

### HC-05 Bluetooth module (Serial2)

| Mega 2560 | HC-05 | Notes |
|-----------|-------|-------|
| Pin 16 (TX2) | RX | Use a **1 kΩ / 2 kΩ voltage divider** on the RX line if your HC-05 logic is 3.3 V |
| Pin 17 (RX2) | TX | Direct connection is usually fine (HC-05 TX is 3.3 V, Mega tolerates it) |
| GND | GND | Required |
| 5 V (or 3.3 V) | VCC | Match your module’s rating; do not exceed HC-05 supply limit |

### USB serial (Serial)

| Mega 2560 | Connection |
|-----------|------------|
| USB port | Computer (Arduino IDE Serial Monitor, Python, MATLAB) |

Both **USB Serial** and **HC-05 (Serial2)** accept the same command format at **9600 baud**.

> **HC-05 baud rate:** Factory default is often 9600. If your module was configured differently, either reconfigure it with AT commands or change `SERIAL_BAUD` in the sketch and re-upload.

---

## Uploading the firmware

1. Open `RC_Excavator_Controller.ino` in the Arduino IDE.
2. Select **Board:** Arduino Mega or Mega 2560.
3. Select the correct **Port** (USB).
4. Click **Upload**.
5. Open **Serial Monitor** → **9600 baud** → line ending **Newline**.

Expected startup message:

```
RC Excavator Controller ready (Arduino Mega 2560).
Command via USB Serial or HC-05 Bluetooth (Serial2).
Format: [PART][ACTION][DURATION_MS]  e.g. BUOP2000
Or send: Trial
```

---

## Sending commands

### Option A — USB (Serial Monitor)

1. Connect Mega via USB.
2. Open Serial Monitor at 9600 baud.
3. Type a command (e.g. `BUOP2000`) and press Enter.

### Option B — Bluetooth (HC-05)

1. Pair your phone or PC with the HC-05 (default PIN is often `1234` or `0000`).
2. Open a Bluetooth serial terminal app (e.g. “Serial Bluetooth Terminal” on Android, or a paired COM port on Windows).
3. Send the same commands as over USB, terminated with a newline.

Debug messages appear on **USB Serial only**. Commands work from either interface.

---

## Command protocol

### Format

```
[PART][ACTION][DURATION_MS]
```

| Field | Length | Description |
|-------|--------|-------------|
| `PART` | 2 chars | Subsystem code |
| `ACTION` | 2 chars | Motion code |
| `DURATION_MS` | 1+ digits | Hold time in milliseconds |

Commands are **case-sensitive**. No spaces.

Example: `BUOP2000` → open bucket for 2000 ms.

### Subsystem codes

#### Bucket (`BU`)

| Command | Action |
|---------|--------|
| `BUOP####` | Open bucket (dump) |
| `BUCL####` | Close bucket (curl) |

#### Middle arm / stick (`MI`)

| Command | Action |
|---------|--------|
| `MIOP####` | Raise middle arm |
| `MICL####` | Lower middle arm |

#### Main arm / boom (`MN`)

| Command | Action |
|---------|--------|
| `MNOP####` | Raise main arm |
| `MNCL####` | Lower main arm |

#### Body rotation (`BR`)

| Command | Action |
|---------|--------|
| `BRCW####` | Rotate cab clockwise |
| `BRCC####` | Rotate cab counter-clockwise |

#### Tracks (`MO`)

| Command | Action |
|---------|--------|
| `MOFW####` | Drive forward |
| `MOBW####` | Drive backward |
| `MOCW####` | Pivot turn right |
| `MOCC####` | Pivot turn left |

Replace `####` with duration in ms (e.g. `500`, `2000`).

### Special command

| Command | Action |
|---------|--------|
| `Trial` | Demo sequence: bucket → stick → boom → cab swing → tracks forward |

---

## Example commands

| Input | Result |
|-------|--------|
| `BUOP2000` | Open bucket, 2 s |
| `BUCL1500` | Close bucket, 1.5 s |
| `MIOP1000` | Raise stick, 1 s |
| `MNCL3000` | Lower boom, 3 s |
| `BRCW2000` | Swing cab clockwise, 2 s |
| `MOFW5000` | Drive forward, 5 s |
| `MOCW1500` | Pivot right, 1.5 s |
| `Trial` | Full subsystem verification |

---

## Automating from a computer

Commands are blocking: wait for each motion to finish before sending the next.

### Python (USB)

```python
import serial
import time

PORT = "/dev/tty.usbmodem14101"  # Mega USB port — change for your system
BAUD = 9600

def send_command(ser, command: str, extra_ms: int = 500):
    ser.write(f"{command}\n".encode())
    ser.flush()
    duration_ms = int(command[4:])
    time.sleep(duration_ms / 1000.0 + extra_ms / 1000.0)

with serial.Serial(PORT, BAUD, timeout=1) as ser:
    time.sleep(2)
    ser.reset_input_buffer()
    send_command(ser, "BUOP2000")
    send_command(ser, "MNOP2500")
    send_command(ser, "MOFW3000")
```

Install: `pip install pyserial`

### Python (Bluetooth)

Pair HC-05 first, then use the assigned serial port (e.g. `/dev/tty.HC-05-DevB` on macOS or `COM5` on Windows) with the same code and baud rate **9600**.

### MATLAB (USB)

```matlab
port = serialport("COM3", 9600);
pause(2);
writeline(port, "BUOP2000");
pause(2.5);
writeline(port, "MOFW3000");
pause(3.5);
clear port;
```

---

## PWM tuning

PWM constants at the top of `RC_Excavator_Controller.ino` emulate R12DS channel positions. They were tuned empirically on this excavator.

To adjust:

1. Edit the relevant `PWM_*` constant.
2. Re-upload the sketch.
3. Test with a short duration (500–1000 ms).

| Constant group | Controls |
|----------------|----------|
| `PWM_BUCKET_*` | Bucket open/close/neutral |
| `PWM_MIDDLE_ARM_*` | Stick up/down/neutral |
| `PWM_MAIN_ARM_*` | Boom up/down/neutral |
| `PWM_BODY_*` | Cab swing CW/CCW/neutral |
| `PWM_TRACK_*` | Track forward/back/neutral |
| `PWM_*_PUMP` | Hydraulic pump enable |

---

## Troubleshooting

| Problem | Likely cause | Fix |
|---------|--------------|-----|
| No USB response | Wrong port or baud | Mega board selected; 9600 baud; correct COM port |
| Bluetooth connects but no motion | Wrong baud on HC-05 | Set HC-05 to 9600 via AT commands, or match `SERIAL_BAUD` |
| Garbled Bluetooth data | Missing voltage divider | Add divider on Mega TX2 → HC-05 RX if module is 3.3 V |
| `ERROR: Command too short` | Malformed input | Use `[PART][ACTION][DURATION]` with no spaces |
| Middle arm idle | Wiring or PWM | Check pin 4; tune `PWM_MIDDLE_ARM_*` |
| Tracks reversed | ESC wiring | Swap forward/backward constants or motor wires |
| Mega resets on motion | Power sag | Separate Mega and excavator supplies; common GND only |
| R12DS vs Mega conflict | Both driving same channel | Ensure only one source controls a channel at a time |

---

## Design notes

- **Blocking control:** Each command uses `delay()` and must finish before the next is accepted. For queued or concurrent control, add a non-blocking state machine.
- **Pump (pin 7):** Active during bucket and arm moves; not used for body rotation or tracks in current logic.
- **Main arm settle:** 1 s neutral pause before/after boom motion to reduce hydraulic shock.
- **Dual serial input:** USB (`Serial`) and HC-05 (`Serial2`) share the same parser; debug output goes to USB only.
- **Prior sketches:** Older folders in this repository (`Main_Arm/`, `Serial_First2/`, `BIM-day-show/`, etc.) contain development versions. This folder is the consolidated release for paper handoff.

---

## Quick reference

```
Board:   Arduino Mega 2560
Receiver: RadioLink R12DS (excavator native RC)
Wireless: HC-05 on Serial2 (pins 16 TX, 17 RX) @ 9600 baud
USB:     Serial @ 9600 baud

Format:  [PART][ACTION][DURATION_MS]

BU  OP/CL     Bucket
MI  OP/CL     Stick
MN  OP/CL     Boom
BR  CW/CC     Cab swing
MO  FW/BW/CW/CC   Tracks

Demo: Trial
```

---

## Handoff checklist

- [ ] Board set to **Arduino Mega 2560** in Arduino IDE
- [ ] Firmware uploaded successfully
- [ ] HC-05 wired to Serial2 (pins 16/17) with common ground
- [ ] Excavator powered; wiring matches pin table above
- [ ] USB: send `Trial` and confirm all subsystems move
- [ ] Bluetooth: pair HC-05 and send `BUOP1000` from a terminal app
- [ ] Document any PWM changes made for your hardware
