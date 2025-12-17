# Performance Analysis and Confusion Matrix Report

## 1. Executive Summary
This document analyzes the current performance of the Facial Recognition system based on field tests conducted in December 2025. The system utilizes the **LBPH (Local Binary Patterns Histograms)** algorithm.

**Current Status:** The system demonstrates high robustness against false positives from strangers (Unknowns) but exhibits significant **inter-class confusion** between registered users (specifically between subjects "Jaime" and "Johan") when operating under the target hardware configuration.

## 2. Methodology
* **Algorithm:** LBPH (Radius=1, Neighbors=8, Grid=8x8).
* **Hardware:** Raspberry Pi Zero 2 W with USB Webcam (Generic).
* **Training Dataset:** 10-15 images per subject, captured via the RPi USB camera.
* **Inference Environment:** Same RPi USB camera, varied lighting conditions.

## 3. Confusion Matrix Analysis

The following matrix represents the probability of the system classifying a real subject (Rows) as a predicted class (Columns).

| Actual \ Predicted | Johan | Jaime | Other User | Unknown |
| :--- | :---: | :---: | :---: | :---: |
| **Johan** | **45%** | <span style="color:red">**40%**</span> | 5% | 10% |
| **Jaime** | <span style="color:red">**35%**</span> | **50%** | 5% | 10% |
| **Registered User C** | 5% | 5% | **85%** | 5% |
| **Stranger (Unregistered)**| 0% | 0% | 0% | **100%** |

### Key Observations
1.  **High False Acceptance Rate (FAR) between Registered Users:** There is a critical confusion cluster between Johan and Jaime. The system struggles to differentiate their specific facial features under current resolution and lighting.
2.  **Excellent True Rejection Rate (TRR):** The system has achieved a **100% success rate** in rejecting unregistered individuals (strangers). No stranger has been falsely identified as a user.
3.  **Moderate False Rejection Rate (FRR):** Users are occasionally classified as "Unknown" (10%), likely due to extreme head poses or poor lighting frames.

## 4. Root Cause Analysis: The "Domain Shift" Problem

A critical finding during testing revealed a **hardware dependency** on the model's accuracy.

### 4.1. The Experiment
* **Test A:** Training with high-resolution smartphone images -> Inference on Laptop Webcam.
    * *Result:* High Accuracy, good separation between Jaime/Johan.
* **Test B:** Training with high-resolution smartphone images -> Inference on Raspberry Pi USB Camera.
    * *Result:* **Catastrophic Failure.** Almost zero recognition.
* **Test C (Current):** Training with RPi USB Camera -> Inference on RPi USB Camera.
    * *Result:* Moderate Accuracy, but high confusion between similar users.

### 4.2. Technical Explanation
The LBPH algorithm extracts features based on **local pixel texture contrasts**. It does not "understand" facial geometry like deep learning models (CNNs).
* **Sensor Noise:** The RPi USB camera has a different noise profile (grain) compared to a smartphone or laptop webcam.
* **Dynamic Range:** The USB camera likely has lower dynamic range, causing shadows to appear "flatter" (losing texture data LBPH needs).
* **Focal Length:** Different lens distortions change the relative pixel distances between eyes/nose/mouth.

**Conclusion:** Using smartphone photos for training created a "Domain Shift." The model learned "HD textures," but the live camera provided "Low-Res/Noisy textures," leading to a mismatch. Training directly on the target hardware (Test C) mitigated this but revealed the sensor's limitations in distinguishing similar faces.

## 5. Recommendations for Improvement

To resolve the Jaime/Johan confusion without changing the core algorithm (due to hardware limits):

1.  **Lighting Normalization:** Ensure the training images for both subjects have distinct lighting or, conversely, highly varied lighting to force the model to learn invariant features.
2.  **Strict Pose Control:** Re-capture the dataset ensuring strictly frontal faces for both users. LBPH is not rotation-invariant.
3.  **Distance Threshold Tuning:**
    * *Current Threshold:* 90.
    * *Action:* Lower the threshold to **65 or 70**.
    * *Expected Outcome:* "Unknown" classifications will increase (more False Negatives), but confusion between Jaime and Johan will decrease (fewer False Positives). This trades convenience for security.