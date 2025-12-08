# Software Requirements Specification (SRS)
## Embedded Face Recognition & Surveillance System

**Project:** Secure Access Control on Edge Devices  
**Target Platform:** Raspberry Pi Zero 2 W (ARM Cortex-A53)  
**Authors:** Jaime Mejia Herrera, Johan Sebastian Mendieta Dilbert  
**Date:** December 2025  
**Version:** 3.0 (Final Release)

---

## 1. Introduction and Rationale

### 1.1 Problem Statement
In resource-constrained environments such as small laboratories or remote monitoring stations, there is a critical need for **autonomous, low-cost, and secure biometric systems**. Commercial solutions often rely on cloud API dependencies (privacy risk/latency) or expensive hardware (Nvidia Jetson). This project addresses the challenge of implementing a **Hard Real-Time (< 500ms)** Facial Recognition System on a $15 Single Board Computer with only 512MB of RAM.

### 1.2 Platform Selection and Trade-offs
The **Raspberry Pi Zero 2 W** was selected for its cost-to-performance ratio. However, this choice imposes severe constraints that dictated the software architecture.

| Feature | Constraint | Engineering Decision / Mitigation |
| :--- | :--- | :--- |
| **Memory** | **512 MB RAM** (Low) | **Decision:** Rejected Python/TensorFlow. Implemented a **C++17 Monolith** to eliminate interpreter overhead (~100MB saved). |
| **CPU** | **1GHz Quad-Core** | **Decision:** Rejected Deep CNNs (MobileNet/ResNet). Selected **LBPH (Local Binary Patterns)** for O(1) inference speed. |
| **Storage** | **MicroSD (Slow I/O)** | **Decision:** Minimized disk writes. Logs are buffered, and the model is loaded entirely into RAM at boot time. |
| **Display** | **No Physical Monitor** | **Decision:** Implemented a **Headless Web Server** (MJPEG) for universal remote visualization via HTTP. |

---

## 2. Hardware & Environmental Constraints (LIMIT)

These constraints are immutable physical properties of the system that the software *must* respect to avoid failure.

| ID | Constraint Description | Technical Impact | Software Mitigation Strategy |
| :--- | :--- | :--- | :--- |
| **LIMIT-01** | **Shared USB 2.0 Bus** | The Camera (V4L2) and Wi-Fi chip share a single USB bus. High-bandwidth video causes packet loss. | **Resolution Cap:** Input strictly limited to **640x480 @ 15 FPS**. **Compression:** Stream uses MJPEG (70% quality) to reduce bandwidth. |
| **LIMIT-02** | **Thermal Throttling** | The CPU throttles (1.0GHz $\to$ 0.6GHz) if core temp exceeds 80°C. | **Duty Cycle:** The main loop includes `std::this_thread::sleep_for(10ms)` to allow CPU idle time, keeping temp < 70°C. |
| **LIMIT-03** | **Memory OOM Kill** | If the app exceeds ~350MB, the Linux OOM Killer will terminate it to save the Kernel. | **Static Allocation:** Avoid dynamic memory growth. Video buffers are recycled. **Swap:** 2GB Swap enabled for safe compilation. |
| **LIMIT-04** | **No Hardware NPU** | No Neural Processing Unit available for acceleration. | **Algorithm Selection:** Use Haar Cascades (integral images) and LBPH (histograms) which run efficiently on general-purpose CPUs. |

---

## 3. Functional Requirements (REQ-F)

The system must perform the following functions to be considered operational.

### 3.1 Video Acquisition & Processing
| ID | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| **REQ-F01** | **Capture:** The system shall capture video from the default V4L2 device (`/dev/video0`). | System successfully grabs frames from a USB Camera. |
| **REQ-F02** | **Preprocessing:** Incoming frames must be converted to Grayscale for detection and kept in RGB for streaming. | Visual verification: Stream is Color, but logs show grayscale processing time. |
| **REQ-F03** | **Resolution Lock:** The input stream must be forced to **640x480** pixels to guarantee processing stability. | Application logs print: `Capture Resolution: 640x480`. |

### 3.2 Computer Vision (The Core)
| ID | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| **REQ-F04** | **Face Detection:** The system shall use **Haar Cascade Classifiers** to locate frontal faces. | A bounding box is drawn around the face when the subject faces the camera within ±30° rotation. |
| **REQ-F05** | **Identification:** The system shall classify the face using **LBPH** against a pre-loaded `.yml` model. | **Known:** Name displayed in Green.<br>**Unknown:** "Desconocido" displayed in Red. |
| **REQ-F06** | **Training (Offline):** A separate executable must generate the model from images in `assets/faces/`. | Running `./scripts/train.sh` updates the `assets/lbph_model.yml` timestamp. |

### 3.3 Network Streaming & Interface
| ID | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| **REQ-F07** | **Web Server:** A TCP server must listen on Port **8080** for incoming HTTP GET requests. | `netstat -tuln` shows port 8080 in LISTEN state. |
| **REQ-F08** | **Live Feed:** Video must be streamed using the **MJPEG** standard (`multipart/x-mixed-replace`). | Video plays natively in Chrome/Firefox without plugins. |
| **REQ-F09** | **Concurrency:** The streaming thread must not block the vision processing thread. | Video analysis continues (logging events) even if the web stream lags due to poor Wi-Fi. |

### 3.4 Security & Access Control
| ID | Requirement | Acceptance Criteria |
| :--- | :--- | :--- |
| **REQ-F10** | **Authentication:** Access to the video feed must be protected by **HTTP Basic Auth**. | Browser prompts for User/Pass. 401 Error if canceled. |
| **REQ-F11** | **PAM Integration:** Credentials must be validated against the Linux OS users (`/etc/shadow`). | Login succeeds with valid SSH credentials (e.g., `johan`). Fails with fake credentials. |
| **REQ-F12** | **Audit Logging:** Every detection event must be logged to a CSV file. | `logs/event_log.csv` contains entries: `ISO-Time, Face-Count, Names`. |

---

## 4. Non-Functional Requirements (REQ-NF)

Quality attributes that define the system's performance and reliability.

| ID | Attribute | Requirement | Metric / Verification |
| :--- | :--- | :--- | :--- |
| **REQ-NF01** | **Latency** | The "Glass-to-Glass" latency (Event $\to$ Screen) shall be minimized. | **Target:** $\le 500$ ms on local Wi-Fi. |
| **REQ-NF02** | **Stability** | The system must operate continuously without memory leaks. | **Test:** Run 4 hours. RAM usage variance $< 5\%$. |
| **REQ-NF03** | **Start-up Time** | The system must be ready to detect faces shortly after service start. | **Target:** $< 5$ seconds from command execution to first frame. |
| **REQ-NF04** | **Recovery** | The system must handle peripheral disconnection gracefully. | **Behavior:** If camera unplugs, retry loop triggers. App does not crash. |
| **REQ-NF05** | **Maintainability** | Logs must not consume all storage space. | **Mechanism:** Automatic Weekly Log Rotation (Keep 4 files). |

---

## 5. Software Module Specifications

The application is architected as a **C++ Monolith** with three primary modules running in a shared memory space.

### 5.1 Vision Controller (Producer Thread)
* **Responsibility:** Controls the Camera, executes OpenCV algorithms, and updates the shared frame buffer.
* **Key Logic:**
    1.  Acquire Frame $\to$ `cv::cvtColor(RGB2GRAY)`.
    2.  `CascadeClassifier::detectMultiScale()`.
    3.  `LBPHFaceRecognizer::predict()` $\to$ Returns `(Label, Confidence)`.
    4.  Draw UI Overlays (Rectangles, Text).
    5.  **Critical Section:** Lock Mutex $\to$ Update Global JPEG Buffer $\to$ Unlock.

### 5.2 Web Server (Consumer Thread)
* **Responsibility:** Handles TCP connections, HTTP parsing, and data transmission.
* **Key Logic:**
    1.  `accept()` incoming socket.
    2.  Parse HTTP Headers looking for `Authorization: Basic`.
    3.  Decode Base64 string $\to$ Call `PAM_Auth_Module`.
    4.  If Valid: Enter Streaming Loop.
    5.  **Critical Section:** Lock Mutex $\to$ Copy Global JPEG Buffer $\to$ Unlock.
    6.  `send()` JPEG boundary and data.

### 5.3 Training Module (Offline Executable)
* **Responsibility:** File I/O and Model Generation.
* **Key Logic:**
    1.  Recursive scan of `assets/faces/`.
    2.  Extract `PersonName` from directory structure.
    3.  Read Image $\to$ Convert to Grayscale.
    4.  `LBPHFaceRecognizer::train()`.
    5.  Serialize Model $\to$ `lbph_model.yml`.
    6.  Serialize Labels $\to$ `labels.csv`.

---

## 6. Verification & Test Plan

### 6.1 System Test Cases (Black Box)

| Test ID | Requirement | Description | Procedure | Expected Result |
| :--- | :--- | :--- | :--- | :--- |
| **TC-PERF-01** | **REQ-NF01** | **Latency Check** | 1. Film a running millisecond stopwatch.<br>2. View stream on PC.<br>3. Snapshot both screens.<br>4. Compare times. | Delta $\le 500ms$. |
| **TC-SEC-01** | **REQ-F11** | **Auth Penetration** | 1. Attempt access via `curl http://<IP>:8080/video_feed` without headers. | Server returns `HTTP 401 Unauthorized`. |
| **TC-CV-01** | **REQ-F05** | **Unknown Rejection** | 1. Present a face NOT in the database.<br>2. Observe stream. | Bounding box is **RED**. Label is "Desconocido". |
| **TC-CV-02** | **REQ-F05** | **Known Recognition** | 1. Present a registered user (e.g., Johan).<br>2. Observe stream. | Bounding box is **GREEN**. Label is "Johan". |
| **TC-STRESS-01** | **REQ-NF02** | **Memory Leak** | 1. Run `htop`.<br>2. Leave system running for 2 hours with motion. | RAM usage remains constant ($\pm$ 10MB). |

### 6.2 Failure Mode Analysis (FMEA)

| Failure Mode | Trigger | System Behavior | Severity |
| :--- | :--- | :--- | :--- |
| **Camera Loss** | USB cable disconnected. | Exception caught. Error logged. System enters "Retry Mode" (5s interval). | Medium |
| **Network Loss** | Wi-Fi Signal dropped. | Vision Thread continues (recording logs). Web Thread waits for socket timeout. | Low |
| **Model Missing** | `.yml` file deleted. | App starts in "Detection Only" mode (Yellow boxes). Logs Error. | Medium |
| **Auth Failure** | PAM Service down. | Web access denied for everyone (Fail-Safe). | High |