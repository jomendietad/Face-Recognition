# Embedded Face Recognition and Video Surveillance System (RPI Zero 2W)

**Version:** 3.0 (Offline Training & Log Rotation Support)  
**Target Hardware:** Raspberry Pi Zero 2W (ARM Cortex-A53)

This repository hosts a high-performance, intelligent security system designed specifically for extreme resource-constrained environments. It implements a full biometric pipeline (Detection, Recognition, and Network Streaming) using a custom **C++ Monolithic Architecture** to maximize efficiency on the Raspberry Pi Zero 2W (512MB RAM).

---

## 1. Problem Statement & Engineering Constraints

Deploying biometric security typically demands expensive hardware (GPUs, NPUs) or dependency on cloud APIs, which introduces latency and privacy risks. The engineering challenge of this project was to implement a **hard-real-time (< 500ms latency)** facial recognition system entirely on the "Edge" using a $15 Single Board Computer.

### Critical Constraints
1.  **Memory Starvation (512MB RAM):** The OS consumes ~100MB. The application must fit Vision, Logic, and Networking into the remaining ~400MB. This disqualifies Python/TensorFlow/PyTorch solutions due to interpreter overhead.
2.  **Thermal Throttling:** The Pi Zero 2W has no active cooling. Continuous high-load processing (like CNN inference) causes thermal throttling (downclocking from 1GHz to 600MHz), destroying real-time performance.
3.  **Bus Bandwidth:** The device shares a single USB 2.0 bus for the camera and Wi-Fi, creating a bottleneck for high-resolution video streams.

**Solution:** A highly optimized **C++17** application using **OpenCV** for native performance and **Linux PAM** for system-level security.

---

## 2. System Architecture

The solution uses a **Multithreaded Producer-Consumer Architecture** to decouple the heavy computer vision tasks from the latency-sensitive network streaming.

### 2.1 Hardware Block Diagram


[Image of Hardware Block Diagram]

```mermaid
graph TD
    subgraph "Peripherals"
        CAM["USB Camera (V4L2) - /dev/video0"] -->|Raw YUV/MJPEG| RPI
        Power["5V/2.5A Power Supply"] --> RPI
    end

    subgraph "Raspberry Pi Zero 2W"
        direction TB
        RPI["SoC (Quad-core Cortex-A53)"]
        
        subgraph "Software Monolith (C++)"
            Vision["Vision Thread (Producer)"]
            Web["Web Server Thread (Consumer)"]
            Auth["PAM Auth Module"]
        end
        
        subgraph "Storage / File System"
            FS_Models["lbph_model.yml"]
            FS_Logs["event_log.csv"]
        end
        
        RPI --> Vision
        Vision --"Mutex Lock"--> Web
        Vision --> FS_Logs
        Vision -.->|Load| FS_Models
        Web -.->|Verify| Auth
    end

    subgraph "Remote Clients"
        Browser["Web Dashboard (HTTP/1.1)"]
    end

    Web -->|"MJPEG Stream (Port 8080)"| Browser
````

### 2.2 Software Data Flow

1.  **Acquisition:** The **Vision Thread** captures frames at 640x480 resolution via V4L2.
2.  **Inference:** Frames are converted to Grayscale. Haar Cascades detect faces, and LBPH predicts identities based on local texture patterns.
3.  **Synchronization:** The processed frame is compressed to JPEG and stored in a `std::vector<uchar>` buffer protected by a `std::mutex`.
4.  **Distribution:** The **Web Thread** wakes up, locks the mutex, copies the buffer, and streams it to connected clients using the `multipart/x-mixed-replace` protocol.

<!-- end list -->

```mermaid
sequenceDiagram
    participant User
    participant Main as Vision Thread
    participant Model as LBPH Model
    participant Shared as Shared Memory
    participant Web as Web Thread

    User->>Main: Launch ./scripts/run.sh
    Main->>Model: Deserializes .yml & .csv (Offline Training)
    
    loop Real-Time Pipeline (15 FPS)
        Main->>Main: Capture Frame & Preprocess
        Main->>Main: Detect (Haar) -> Crop ROI
        Main->>Model: Predict(ROI) -> {Label, Confidence}
        Main->>Main: Draw Bounding Box & Name
        Main->>Shared: Lock Mutex -> Write JPEG -> Unlock
    end

    Note over Web: Async Network Handler
    loop Client Streaming
        User->>Web: GET /video_feed
        Web->>Web: Authenticate (Linux PAM)
        Web->>Shared: Lock Mutex -> Read JPEG -> Unlock
        Web-->>User: Send HTTP Chunk (--frame)
    end
```

-----

## 3\. Performance & Confusion Matrix Analysis

Current model performance based on extensive field testing with the RPi USB Camera.

### 3.1 The "Domain Shift" Phenomenon (Critical Finding)

Initial iterations failed because the model was trained with **high-resolution smartphone photos** but ran inference on a **low-resolution webcam**. The LBPH algorithm relies on pixel-level texture statistics. The noise, dynamic range, and lens distortion differences caused a "Domain Shift," leading to near-zero accuracy.

  * **Resolution:** The dataset **MUST** be rebuilt using images captured **directly from the RPi camera** to align the texture patterns between training and inference.

### 3.2 Confusion Matrix (Simplified)

Even with correct training, inter-class confusion persists between similar faces due to the low dimensionality of the LBPH features.

| Actual \\ Predicted | Johan | Jaime | Unknown |
| :--- | :---: | :---: | :---: |
| **Johan** | **High** | Medium | Low |
| **Jaime** | Medium | **High** | Low |
| **Stranger** | **0%** | **0%** | **100%** |

  * **Zero False Positives for Strangers:** The system successfully rejects 100% of unregistered users.
  * **High Inter-class Confusion:** Users "Jaime" and "Johan" are frequently misidentified as each other. This is a limitation of the LBPH algorithm when facial structures and lighting conditions are similar.

-----

## 4. Project Structure & File Descriptions

```text
Face-Recognition/
├── assets/
│   ├── haarcascades/    # Pre-trained XML classifiers (downloaded automatically)
│   └── faces/           # RAW TRAINING DATASET
│       ├── Johan/       # Directory containing .jpg/.png images for User A
│       └── Jaime/       # Directory containing .jpg/.png images for User B
├── src/
│   ├── sistema_final.cpp # RUNTIME ENGINE: Handles video, web server, and inference.
│   ├── train_model.cpp   # OFFLINE TRAINER: Reads images, computes histograms, saves .yml.
│   └── CMakeLists.txt    # BUILD CONFIG: Defines library linking (OpenCV, Threads, PAM).
├── scripts/
│   ├── install_dependencies.sh # SETUP: Installs apt packages & configures Logrotate.
│   ├── build.sh                # COMPILER: Runs CMake and Make to generate binaries.
│   ├── train.sh                # TRAINER WRAPPER: Compiles and runs the training logic.
│   └── run.sh                  # LAUNCHER: Sets environment vars and runs the system.
├── logs/                # DATA: Contains event_log.csv (Rotated weekly).
├── REQUIREMENTS.md      # SPECS: Detailed engineering requirements, failure modes, and hardware constraints.
├── CONFUSION_MATRIX_ANALYSIS.md # REPORT: Technical analysis of model accuracy, "Domain Shift", and limitations.
└── README.md            # MAIN DOCS: General project overview.
```

-----

## 5\. Installation & Deep Setup

### Step 1: Hardware Preparation (Critical Swap)

The GCC compiler requires significant RAM to instantiate C++ templates for OpenCV. The Pi Zero 2W will crash (OOM) without extended Swap.

```bash
# 1. Open swap configuration
sudo nano /etc/dphys-swapfile

# 2. Modify this line to allocate 2GB of Swap
CONF_SWAPSIZE=2048

# 3. Apply changes (Restart swap service)
sudo /etc/init.d/dphys-swapfile restart
```

### Step 2: Dependency Injection & Log Rotation

We use a smart script that installs required system libraries and configures **Logrotate**.

  * **Libraries:** `libopencv-dev` (Core), `libpam0g-dev` (Auth), `libopenblas-dev` (Math optimization).
  * **Logrotate:** Automatically creates a rule in `/etc/logrotate.d/` to compress `event_log.csv` weekly and keep 4 weeks of history, preventing SD card saturation.

<!-- end list -->

```bash
cd "Face-Recognition"
chmod +x scripts/*.sh
./scripts/install_dependencies.sh
```

### Step 3: Compiling the Monolith

This step translates the C++ source code into machine code (ARM64 binaries). It generates two executables:

1.  `train_model`: For processing images.
2.  `sistema_final`: For the runtime security system.

<!-- end list -->

```bash
./scripts/build.sh
```

-----

## 6\. Training & Operation Workflows

This system separates the **heavy learning phase** from the **real-time execution phase** to ensure fast boot times.

### Phase A: Offline Training (On Database Change)

**When to run:** Only when adding new users or updating photos.
**What happens:**

1.  Iterates recursively through `assets/faces/`.
2.  Assigns an Integer ID to each folder name (Labeling).
3.  Computes Local Binary Patterns for every image.
4.  Serializes the mathematical model to `assets/lbph_model.yml` and the name map to `assets/labels.csv`.

<!-- end list -->

```bash
# 1. Create directory and add 10-15 photos
mkdir -p assets/faces/NewUser
# (Upload photos here)

# 2. Run the trainer
./scripts/train.sh
```

### Phase B: Runtime Execution (Daily Operation)
**When to run:** Every time the device boots for surveillance.
**What happens:**
1.  Deserializes `lbph_model.yml` into RAM (takes < 1 second).
2.  Starts the Video Capture thread (Camera).
3.  Starts the Web Server thread (Port 8080).
4.  Performs authentication and logging.

**Option 1: Standard USB Camera**
Use this for the default camera connected to the Raspberry Pi (Index 0).
```bash
./scripts/run.sh
````

**Option 2: IP Camera / Network Stream**
The system supports ingesting video from a remote source (e.g., a laptop streaming via Flask or another IP camera) by passing the URL as an argument.

```bash
# Syntax: ./scripts/run.sh "URL_OF_THE_STREAM"
./scripts/run.sh "http://192.168.1.50:5000/video_feed"
```

  * **Web Access:**
    Navigate to `http://<RPI_IP>:8080`.
      * **Username/Password:** Uses your **Linux System Credentials** (e.g., user `johan`). This means you don't manage separate web passwords; the system uses the OS's shadow file security.

<!-- end list -->

-----

## 7\. Technical Specifications & Limitations

| Feature | Specification | Constraint / Limitation |
| :--- | :--- | :--- |
| **Algorithm** | LBPH (Local Binary Patterns Histograms) | **Lighting Sensitivity:** High. Requires consistent lighting between training and inference. **Rotation:** Fails if head rotation \> 30°. |
| **Resolution** | 640 x 480 (VGA) | **Throughput:** Higher resolutions cause massive frame drops due to USB 2.0 bandwidth sharing with Wi-Fi. |
| **Frame Rate** | Capped at 15 FPS | **Thermal:** Higher FPS overheats the CPU, triggering thermal throttling and lag spikes. |
| **Latency** | 300ms - 500ms (Glass-to-Glass) | **Network:** Dependent on Wi-Fi signal quality (2.4GHz only on Pi Zero). |
| **Storage** | CSV Logging with Weekly Rotation | **Capacity:** Text logs are negligible, but images (if enabled) would fill the SD card quickly. |
| **Security** | Linux PAM (Pluggable Auth Modules) | **Scope:** Security is bound to the Linux user permissions. Root access compromises the system. |

### Technology Stack

  * **Language:** C++17 (Standard ISO/IEC 14882:2017)
  * **Compiler:** GCC 11.4.0 (Aarch64)
  * **Computer Vision:** OpenCV 4.6.0 (Contrib Modules for `FaceRecognizer`)
  * **Build System:** CMake 3.22
  * **OS:** Raspberry Pi OS Lite (Debian 12 Bookworm)

-----

*Project developed for the Embedded Linux Systems Programming course - Universidad Nacional de Colombia.*