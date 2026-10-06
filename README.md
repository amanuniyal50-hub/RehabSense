# RehabSense

**An ESP32-S3 wearable that turns joint rehab exercises into measured, guided sessions — instead of "do your exercises and hope you're doing them right."**

🏆 **2nd Prize — SAKSHAM'26 Hardware Hackathon** (IEEE & IETE student chapters, Vidyavardhini's College of Engineering & Technology, Vasai)

## What it does

RehabSense straps two IMU sensors to a patient's thigh and shin to track orthopedic rehab exercises in real time — joint flexion angle, movement speed, and whether the patient is compensating with hip/torso rotation instead of moving the joint correctly (a common mistake that's hard to self-correct without feedback).

The device guides the patient through physio-prescribed exercises with live on-device feedback, counts valid reps, flags faults (hold too short, over the safety limit, moving too fast, incorrect posture), and logs every session — syncing it to the cloud so a clinician can review progress remotely.

## Key features

- **Dual-IMU joint tracking** (MPU6050 × 2, thigh + shin) — computes real joint angle via a complementary filter (gyro integration + accelerometer correction), not just raw sensor readings
- **Three exercises**: Knee Flexion, Straight Leg Raise, Elbow Flexion — each with physio-configurable target angle, safety limit, hold time, and rep count
- **Two modes**: Guided (live audio + visual cues) and Assessment (silent tracking for objective measurement)
- **Fault detection**: incomplete reps, short holds, over-limit movement, too-fast movement, and hip/torso compensation (out-of-plane rotation)
- **On-device feedback**: 128×64 OLED showing live angle, target/limit band, rep count, and session summary, plus buzzer audio cues for each event
- **Local + cloud logging**: every session is saved locally (LittleFS) and synced to ThingSpeak over Wi-Fi, so nothing is lost even if the device is offline mid-session
- **Auto-calibration**: one-touch calibration routine that detects sensor mounting errors and corrects for gyro bias automatically — no manual tuning needed between patients

## Hardware

- ESP32-S3 (main controller — Wi-Fi, dual-core)
- 2× MPU6050 IMU (thigh + shin, I²C, one at address 0x68 / one at 0x69 via AD0)
- 128×64 SSD1306 OLED display (I²C)
- Push buttons for on-device navigation
- Buzzer for audio feedback cues
- LittleFS onboard flash storage for session logs

## Project structure

| File | Purpose |
|---|---|
| `RehabSense.ino` | Entry point required by the Arduino build system; all real logic lives in the `.cpp`/`.h` files |
| `main.cpp` | App state machine — screens, button handling, main loop |
| `rehab_logic.cpp/h` | Core exercise logic — joint angle model, rep counter, calibration, fault detection |
| `sensing.cpp/h` | Sensor task — reads both IMUs, runs the joint model, publishes live data |
| `imu.cpp/h` | Low-level MPU6050 driver |
| `ui.cpp/h` | OLED screen drawing |
| `buttons.cpp/h` | Button input handling |
| `feedback.cpp/h` | Buzzer cue patterns |
| `storage.cpp/h` | Persistent config (NVS) and session log storage (LittleFS) |
| `netsvc.cpp/h` | Wi-Fi, web dashboard server, ThingSpeak sync |
| `web_page.h` | Web dashboard HTML/JS |
| `config.h` | Pin definitions and hardware configuration |

## Setup

1. Open `RehabSense.ino` in the Arduino IDE (or PlatformIO).
2. Install required libraries: `Adafruit_GFX`, `Adafruit_SSD1306`, `Preferences`, `LittleFS`.
3. Check `config.h` and adjust pin numbers to match your wiring.
4. Wire the thigh MPU6050 with AD0 → GND (address 0x68) and the shin MPU6050 with AD0 → 3V3 (address 0x69) — both on the same I²C bus.
5. Flash to an ESP32-S3 board.
6. On first boot, connect to the device's Wi-Fi AP to configure your home Wi-Fi and (optionally) a ThingSpeak write key for cloud sync.

## Team — Team Arogya

- Aman Uniyal
- Ishwari Pokharkar
- Atharv Kharade
- Gopika Menon

## Roadmap

- Re-integrate haptic (vibration motor) feedback alongside the buzzer
- Surface the built-in clinical assessment battery (30s Chair Stand, Timed Up & Go, Position Sense test) in the on-device menu
- Session history/trend visualization on the web dashboard

## License

This project is licensed under the [MIT License](LICENSE).