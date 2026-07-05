# sprinky
Landscape irrigation sprinkler controller.

🤖 AI Explanation: (Provided by Hugging Face)
This Arduino sketch is a sophisticated smart irrigation controller designed for ESP32 microcontrollers. Let me break down its comprehensive functionality:

## Main Purpose
This is a networked smart sprinkler/watering controller that can:
- Control up to 8 solenoid valves for irrigation
- Monitor temperature using DS18B20 sensors or analog diode
- Connect to WiFi networks for remote control via web interface
- Operate on scheduled timers
- Integrate with Home Assistant for home automation
- Provide OTA updates
- Display real-time status information

## Key Functions and Tasks

### Core System Functions:
- `setup()`: Initializes hardware, connects to WiFi, configures UI, sets up time synchronization, and initializes all components
- `loop()`: Main execution loop handling continuous operations like time updates, sensor readings, relay control, and UI updates
- `connectWifi()`: Attempts to connect to saved WiFi credentials, falls back to AP mode if connection fails
- `getTempF()`: Reads temperature from either DS18B20 sensor or analog diode with calibration

### Hardware Interactions:
- GPIO Pins: Uses `TEMP_PIN` (pin 21 on ESP32) for temperature sensor
- Relay Control: Controls up to 8 solenoid valves through relay outputs (RELAY8 macro enables 8-relay support)
- LED Indicator: Built-in LED toggles for system heartbeat monitoring
- Temperature Sensors:
- DS18B20 digital temperature sensor (if enabled)
- Analog diode sensor on A0 pin as fallback
- WiFi Connectivity: ESP32's built-in WiFi capabilities for network communication

### Algorithms and Logic:
- Time Management: Implements timezone-aware scheduling using the Timezone library with multiple global time zones
- Temperature Averaging: Maintains 24-hour rolling buffer of temperature readings for statistical analysis
- Schedule Control: Implements automatic valve activation based on programmed start times
- Safety Logic: Automatic shutoff timers for manually activated valves (60-second limit)
- Fallback Mechanisms: WiFi connection recovery, AP mode fallback, sensor failure handling

### Special Features and Details:

#### User Interface (ESPUI):
- Web-based GUI with tabs for:
- WiFi credentials management
- Valve control buttons
- Schedule configuration
- Temperature monitoring
- System status display
- Real-time updates of temperature, time, signal strength, and system status

#### Home Assistant Integration (USE_WITH_HA):
- Full MQTT integration for Home Assistant compatibility
- Sensor reporting for garden temperature
- Button controls for individual valves
- Switch to disable/enable the entire watering system
- Device discovery and proper naming conventions

#### Advanced Capabilities:
- OTA Updates: Over-the-air firmware updates via ElegantOTA
- Time Synchronization: NTP client with timezone support
- Memory Management: Preferences storage for settings persistence
- Error Handling: Comprehensive error checking and recovery mechanisms
- Multiple Time Zones: Support for 10+ global time zones including DST rules
- Debugging Tools: Extensive serial output and debugging capabilities

#### Power and System Management:
- Sleep Mode: Disables WiFi sleep on ESP32 for better UI responsiveness
- Auto-reconnect: WiFi auto-reconnection capability
- Reset Detection: Identifies and reports system reset reasons
- Memory Management: Proper NVS flash handling for persistent storage

#### Security and Reliability:
- Configuration Persistence: Stores WiFi credentials and settings in non-volatile memory
- Safe Shutdown: Automatic valve shutdown after manual activation
- Connection Monitoring: Continuous WiFi signal strength monitoring
- System Health: Built-in LED heartbeat for system status indication

The controller is designed to be highly reliable for outdoor irrigation applications, with robust networking, scheduling, and monitoring capabilities. It can operate autonomously or be remotely controlled via web interface or Home Assistant integration.

#### Further setup info

If it is not set up to attach to a local wifi AP, it will form it's own AP at 192.168.4.1. I've had a version of this running in my garden for about two years with no watering or plant disasters, however, use this code at your own risk. It is a work in progress and I am a lame coder.

Note: Your ElegantOTA library will need to be switched to asynch mode (see library doc https://github.com/ayushsharma82/ElegantOTA)
