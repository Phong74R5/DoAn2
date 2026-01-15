# Face Recognition Access Control
**Raspberry Pi – Embedded Edge AI System**

---

## 1. Overview

This project is a **real-time face recognition access control system** designed to run directly on a **Raspberry Pi** as a **headless embedded edge device**.

The system is optimized for embedded and industrial use cases such as:

- Access control terminals
- Attendance & time-keeping systems
- Kiosk devices
- Smart embedded appliances

**Key design principles:**

- No desktop stack (no X11, Wayland, DRM, or framebuffer)
- Direct hardware access (SPI, GPIO)
- Deterministic multi-threaded execution
- Offline-first with optional cloud synchronization

---

## 2. Hardware Specification

### 2.1 Main Controller

| Item | Specification |
|------|---------------|
| Platform | Raspberry Pi 3B / 3B+ / Raspberry Pi 4 |
| SoC | Broadcom BCM2835 / BCM2837 / BCM2711 |
| CPU | ARMv7 / ARMv8 Cortex-A53/A72 |
| RAM | 1–4 GB (model dependent) |
| Power | 5V DC, 2.5A minimum |

---

### 2.2 Display Module

| Item | Specification |
|------|---------------|
| Type | SPI TFT LCD |
| Controller IC | ILI9341 |
| Resolution | 320 × 240 pixels |
| Color Format | RGB565 (16-bit) |
| Interface | SPI (BCM2835) |
| Driver | Custom low-level SPI driver |
| Framebuffer | ❌ Not used |

---

### 2.3 Camera Module

| Item | Specification |
|------|---------------|
| Type | USB UVC Camera |
| Interface | USB 2.0 |
| Resolution | 320 × 240 |
| Frame Rate | ~30 FPS |
| Driver | UVC (Linux V4L2) |

---

### 2.4 GPIO & User Inputs

| Signal | Description |
|--------|-------------|
| Register Button | User enrollment trigger |
| Sleep Button | Sleep / shutdown control |
| Status LED | System status indication |
| LCD DC | Data / Command control |
| LCD RESET | LCD reset line |

**GPIO Configuration:**
- GPIO library: **BCM2835**
- Internal pull-up resistors enabled
- Polling-based button handling

---

## 3. Software Specification

### 3.1 Operating System

| Item | Value |
|------|-------|
| OS | Raspberry Pi OS / Yocto Linux |
| Mode | Headless |
| Init System | systemd |
| Runtime | Native Linux |

---

### 3.2 Programming Environment

| Item | Value |
|------|-------|
| Language | C++17 |
| Build System | Make |
| Threading | POSIX pthread |
| Networking | libcurl |
| JSON | nlohmann/json |

---

### 3.3 AI & Computer Vision

| Component | Description |
|-----------|-------------|
| Face Detection | YuNet (ONNX) |
| Face Embedding | MobileFaceNet (ONNX) |
| Inference Engine | OpenCV DNN |
| Embedding Size | 128-D float |
| Similarity Metric | Cosine similarity |
| Match Threshold | 0.9 |

---

### 3.4 Functional Modes

#### Register Mode
- Triggered by hardware button
- Captures 5 high-quality face samples
- Averages embeddings
- Stores data locally (encrypted) and on cloud

#### Scan Mode
- Continuous face recognition
- Displays user name and confidence
- Attendance logging
- Per-user debounce: 30 seconds

#### Power Management

| Action | Behavior |
|--------|----------|
| Short press | Enter sleep mode |
| Long press (≥3s) | Safe system shutdown |

---

## 4. System Architecture

### 4.1 Multi-threaded Design

| Thread | Responsibility |
|--------|----------------|
| Camera Thread | Frame capture |
| AI Thread | Detection & recognition |
| LCD Thread | UI rendering & SPI transfer |
| Network Thread | Cloud sync |
| Button Thread | User input handling |

---

## 5. Block Diagram (Datasheet Style)

```mermaid
flowchart LR
    PWR[Power Supply 5V] --> RPI[Raspberry Pi SoC]

    CAM[USB Camera] -->|USB| RPI
    BTN[Buttons] -->|GPIO| RPI

    RPI -->|SPI| LCD[SPI TFT LCD<br/>ILI9341]

    RPI -->|HTTPS| CLOUD[(Cloud / Firebase)]

    subgraph RPI[Embedded Edge System]
        CPU[CPU & RAM]
        AI[AI Inference Engine]
        IO[GPIO / SPI Driver]
        NET[Network Stack]
    end
```

---

## 6. System Flow Chart

```mermaid
flowchart TD
    START([System Boot]) --> INIT[Initialize Hardware:<br/>SPI, GPIO, Camera, Network]
    
    INIT --> BOOT_LOGO[Display Boot Logo<br/>2 seconds]
    BOOT_LOGO --> LOAD_DB[Load User Database<br/>from Cloud/Local]
    LOAD_DB --> MAIN_LOOP{Main Loop}
    
    %% Main Decision Tree
    MAIN_LOOP --> CHECK_WIFI{WiFi Config<br/>Mode Active?}
    CHECK_WIFI -->|Yes| WIFI_UI[Display WiFi UI]
    WIFI_UI --> WIFI_INPUT{Button Input}
    WIFI_INPUT -->|Short Press| WIFI_NEXT[Next Option]
    WIFI_INPUT -->|Long Press| WIFI_SELECT[Select/Enter]
    WIFI_INPUT -->|3s Hold| WIFI_CONNECT[Connect to WiFi]
    WIFI_INPUT -->|Double Click| WIFI_RESCAN[Rescan Networks]
    WIFI_NEXT --> MAIN_LOOP
    WIFI_SELECT --> MAIN_LOOP
    WIFI_CONNECT --> WIFI_TEST{Connection<br/>Success?}
    WIFI_TEST -->|Yes| EXIT_WIFI[Exit WiFi Mode]
    WIFI_TEST -->|No| WIFI_FAIL[Display: Failed]
    WIFI_FAIL --> WIFI_UI
    EXIT_WIFI --> MAIN_LOOP
    WIFI_RESCAN --> WIFI_UI
    
    CHECK_WIFI -->|No| CHECK_SLEEP{Sleep Mode<br/>Active?}
    CHECK_SLEEP -->|Yes| SLEEP_DISPLAY[Display: Dimmed Screen<br/>Backlight Fade 40%]
    SLEEP_DISPLAY --> SLEEP_WAIT{Button Press?}
    SLEEP_WAIT -->|Short Press| WAKE_UP[Wake Up<br/>Fade to 100%]
    SLEEP_WAIT -->|3s Hold| SHUTDOWN[Safe Shutdown<br/>Fade to 0%]
    WAKE_UP --> MAIN_LOOP
    SHUTDOWN --> END([System Off])
    SLEEP_WAIT -->|No Press| MAIN_LOOP
    
    CHECK_SLEEP -->|No| CHECK_MODE{Register Mode<br/>Enabled?}
    
    %% Register Mode Branch
    CHECK_MODE -->|Yes| REG_CAPTURE[Capture Frame]
    REG_CAPTURE --> REG_DETECT[Face Detection<br/>YuNet]
    REG_DETECT --> REG_FOUND{Face Found?}
    REG_FOUND -->|No| REG_SHOW_FACE[Display: Show Face]
    REG_SHOW_FACE --> MAIN_LOOP
    
    REG_FOUND -->|Yes| REG_QUALITY[Check Face Quality<br/>Blur/Angle]
    REG_QUALITY --> REG_GOOD{Quality > 10%<br/>& Delay > 1.5s?}
    REG_GOOD -->|No| REG_WAIT[Display: Pose Guide<br/>+ Quality %]
    REG_WAIT --> MAIN_LOOP
    
    REG_GOOD -->|Yes| REG_SAVE[Save Sample<br/>Flash Green]
    REG_SAVE --> REG_COUNT{5 Samples<br/>Collected?}
    REG_COUNT -->|No| REG_NEXT[Display: Next Pose<br/>Look Left/Right/Up/Down]
    REG_NEXT --> MAIN_LOOP
    
    REG_COUNT -->|Yes| REG_PROCESS[Extract 5 Embeddings<br/>MobileFaceNet]
    REG_PROCESS --> REG_SAVE_DB[Save to Database<br/>Local + Cloud Sync]
    REG_SAVE_DB --> REG_SUCCESS{Save Success?}
    REG_SUCCESS -->|Yes| REG_DONE[Display: Success<br/>User Name + Green]
    REG_SUCCESS -->|No| REG_ERROR[Display: Save Failed<br/>Red]
    REG_DONE --> EXIT_REG[Exit Register Mode]
    REG_ERROR --> EXIT_REG
    EXIT_REG --> MAIN_LOOP
    
    %% Recognition Mode Branch
    CHECK_MODE -->|No| REC_CAPTURE[Capture Frame<br/>30 FPS]
    REC_CAPTURE --> REC_DETECT[Face Detection<br/>YuNet Confidence > 0.85]
    REC_DETECT --> REC_FOUND{Face Found?}
    REC_FOUND -->|No| REC_SCAN[Display: Scanning...]
    REC_SCAN --> MAIN_LOOP
    
    REC_FOUND -->|Yes| REC_EXTRACT[Extract Embedding<br/>MobileFaceNet 128D]
    REC_EXTRACT --> REC_COMPARE[Compare with Database<br/>Cosine Similarity]
    REC_COMPARE --> REC_MATCH{Best Match<br/>> 90%?}
    
    REC_MATCH -->|No| REC_UNKNOWN[Display: Unknown<br/>Low Similarity % + Red]
    REC_UNKNOWN --> MAIN_LOOP
    
    REC_MATCH -->|Yes| REC_DEBOUNCE{Same User<br/>Within 30s?}
    REC_DEBOUNCE -->|Yes| REC_DISPLAY_ONLY[Display: Welcome Name<br/>Skip Logging]
    REC_DEBOUNCE -->|No| REC_LOG[Log Attendance<br/>Timestamp + ID]
    REC_LOG --> REC_CLOUD[Queue Cloud Sync<br/>Non-blocking]
    REC_CLOUD --> REC_DISPLAY[Display: Match Success<br/>Name + Similarity % + Green]
    REC_DISPLAY_ONLY --> MAIN_LOOP
    REC_DISPLAY --> MAIN_LOOP
    
    %% Network Monitor
    MAIN_LOOP --> NET_CHECK{Network Monitor<br/>Every 5s}
    NET_CHECK -->|6 Fails| NET_LOST[Internet Lost<br/>Auto Enter WiFi Mode]
    NET_LOST --> WIFI_UI
    NET_CHECK -->|Connected| NET_OK[Update Status<br/>Green Dot]
    NET_CHECK -->|Disconnected| NET_OFFLINE[Update Status<br/>Red Dot]
    NET_OK --> MAIN_LOOP
    NET_OFFLINE --> MAIN_LOOP
    
    %% Button Events
    MAIN_LOOP --> BTN_EVENTS{Button Events}
    BTN_EVENTS -->|Short Press| BTN_TOGGLE[Toggle Register Mode]
    BTN_EVENTS -->|Long Press| BTN_SAME[Toggle Register Mode]
    BTN_EVENTS -->|3s Hold Reg| BTN_WIFI[Enter WiFi Config]
    BTN_TOGGLE --> MAIN_LOOP
    BTN_SAME --> MAIN_LOOP
    BTN_WIFI --> WIFI_UI
    
    style START fill:#90EE90
    style END fill:#FFB6C1
    style SHUTDOWN fill:#FFB6C1
    style REG_SUCCESS fill:#98FB98
    style REG_ERROR fill:#FF6B6B
    style REC_MATCH fill:#FFD700
    style REC_UNKNOWN fill:#FF6B6B
    style WIFI_TEST fill:#87CEEB
    style NET_LOST fill:#FFA500
```

---

## 7. Data Flow Sequence

```mermaid
sequenceDiagram
    participant Camera
    participant AI
    participant LCD
    participant Network

    Camera->>AI: Video Frame
    AI->>AI: Face Detection
    AI->>AI: Embedding Extraction
    AI->>Network: User ID / Attendance Log
    Network-->>AI: Sync Result
    AI->>LCD: Frame + UI Overlay
```

---

## 7. Storage & Networking

### 7.1 Local Storage

**File:** `userdata.dat`
- **Format:** Binary
- **Encryption:** XOR (`0xAA`)
- **Maximum embedding storage:** 1024 floats

---

### 7.2 Cloud Integration

| Node | Purpose |
|------|---------|
| `/users` | User metadata |
| `/attendance` | Attendance records |

**Network Features:**
- HTTPS communication
- Timeout & retry protection
- Offline-first design

---

## 8. Build & Execution

### 8.1 Dependencies

- OpenCV (with DNN module)
- BCM2835
- libcurl
- pthread

---

### 8.2 Build

```bash
make
```

---

### 8.3 Run

```bash
sudo ./app_camera
```

> **Note:** Root permission is required for SPI and GPIO access.

---

## 9. Deployment Notes

- Headless embedded operation
- systemd service supported
- Compatible with Yocto-based images
- Suitable for industrial deployment

---

## 10. Limitations

- Single camera support
- SPI bandwidth limits LCD refresh rate
- Performance depends on Raspberry Pi model

---

## 11. Authors

- **Le Hong Phong**
- **Tran Gia Huy**

---

## 12. License

**Internal / Embedded Use Only**

Redistribution requires author approval.

---

**Document Version:** 1.0  
**Last Updated:** December 2025
