# PestInPeace - Raspberry Pi Edition

A comprehensive pest detection and monitoring system built for Raspberry Pi, combining multiple applications and IoT sensors to effectively detect and manage pest activity.

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Project Structure](#project-structure)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Building](#building)
- [Usage](#usage)
- [Project Components](#project-components)
- [Contributing](#contributing)
- [License](#license)

## 🎯 Overview

PestInPeace is an intelligent pest detection system that leverages Raspberry Pi computing power and various IoT sensors to monitor and detect pest activity in real-time. The system processes sensor data and triggers appropriate responses to manage pest populations effectively.

## ✨ Features

- **Multi-Sensor Support**: Integrates with various sensors including the LTR559 light and proximity sensor
- **IoT Connectivity**: Full IoT application support for remote monitoring and control
- **Periodic Monitoring**: Continuous periodic capture of sensor data
- **Real-time Processing**: Fast C++ implementation for low-latency pest detection
- **Modular Architecture**: Separate applications for different detection and monitoring tasks

## 📁 Project Structure

```
PestInPeace_rashberrypi/
├── README.md              # This file
├── Makefile               # Build configuration
├── include/               # Header files
├── src/                   # Source code directory
├── app                    # Main application binary
├── iot_app                # IoT connectivity application
├── ltr559_iot             # LTR559 sensor IoT integration
└── periodic_capture       # Periodic sensor capture utility
```

## 🔧 Hardware Requirements

- **Raspberry Pi** (3 or later recommended)
- **LTR559 Light and Proximity Sensor**
- **Additional I/O sensors** (as configured in applications)
- **Power supply** (appropriate for your Raspberry Pi model)
- **Optional**: Network connectivity for IoT features

## 💻 Software Requirements

- **C++** (C++11 or later)
- **Make** build system
- **Raspberry Pi OS** or compatible Linux distribution
- Appropriate sensor libraries and GPIO libraries

## 🚀 Installation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/RickyzZ1/PestInPeace_rashberrypi.git
   cd PestInPeace_rashberrypi
   ```

2. **Install dependencies** (adjust based on your specific sensor libraries):
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential
   ```

## 🔨 Building

Build the project using Make:

```bash
make
```

This will compile all applications and generate the necessary binaries.

To clean build artifacts:
```bash
make clean
```

## 📖 Usage

### Main Application
```bash
./app
```

### IoT Application
```bash
./iot_app
```

### LTR559 Sensor Integration
```bash
./ltr559_iot
```

### Periodic Capture Utility
```bash
./periodic_capture
```

## 🔌 Project Components

### **app**
The main detection and monitoring application. Handles core pest detection logic and system coordination.

### **iot_app**
Provides IoT connectivity features for remote monitoring, control, and data transmission.

### **ltr559_iot**
Specialized module for LTR559 light and proximity sensor integration, enabling distance-based pest detection.

### **periodic_capture**
Utility for periodic sensor data capture and logging. Useful for continuous environmental monitoring and trend analysis.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

## 📄 License

[Specify your license here - e.g., MIT, GPL, etc.]

---

For more information or support, please open an issue on the GitHub repository.