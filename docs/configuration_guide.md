# Configuration Guide

## Overview

The Akita CarNode now supports **runtime configuration** via serial commands, with persistent storage using ESP32's Preferences API. This eliminates the need to recompile and reflash the firmware when changing settings.

## Configuration Methods

### 1. Serial Command Interface (Recommended)

Connect to the device via Serial Monitor (115200 baud) and use interactive commands:

#### Quick Start
```
help          - Show configuration menu
show          - Display current configuration
```

#### Common Commands
```
vehicle MyCar01                    - Set vehicle ID
wifi MyNetwork mypassword          - Set WiFi credentials
obdname OBDII                      - Set OBD-II adapter name
obduuid <service> <characteristic> - Set OBD-II UUIDs
rnsapp akita_car_node             - Set Reticulum app name
rnsdest <address>                  - Set Reticulum destination (or "clear" for announce)
use_wifi 1                         - Use WiFi (1) or LoRa (0)
enable_gps 1                       - Enable/disable GPS
enable_led 1                       - Enable/disable status LED
led_pin 2                          - Set status LED pin
save                               - Save configuration to persistent storage
reset                              - Reset to defaults from config.h
```

### 2. Default Configuration (config.h)

The `config.h` file still serves as the **default configuration** and is used when:
- First time boot (no saved configuration exists)
- Configuration is reset via `reset` command
- Configuration version mismatch

### 3. Persistent Storage

Configuration is automatically saved to ESP32's non-volatile storage (Preferences API) when you use the `save` command. Settings persist across reboots.

## Configuration Flow

1. **On First Boot:**
   - Loads defaults from `config.h`
   - Saves defaults to persistent storage
   - Ready to use immediately

2. **On Subsequent Boots:**
   - Loads saved configuration from persistent storage
   - Falls back to `config.h` defaults if saved config is invalid or version mismatch

3. **Runtime Changes:**
   - Use serial commands to change settings
   - Use `save` command to persist changes
   - Some settings (like WiFi credentials) take effect immediately
   - Some settings (like GPS enable/disable) may require restart

## Example Configuration Session

```
[INFO] Akita CarNode Initializing...
[INFO] Type 'help' or 'config' for configuration menu

> help

=== Akita CarNode Configuration Menu ===
Commands:
  show          - Show current configuration
  vehicle <id>  - Set vehicle ID
  wifi <ssid> <pass> - Set WiFi credentials
  ...

> show

--- Current Configuration ---
Vehicle ID: AkitaCar01
WiFi SSID: YOUR_WIFI_SSID
WiFi Password: ***
OBD-II Device Name: OBDII
...

> wifi MyHomeNetwork MySecurePassword
[CONFIG] WiFi SSID set to: MyHomeNetwork
[CONFIG] WiFi password set

> vehicle MyTruck
[CONFIG] Vehicle ID set to: MyTruck

> save
[CONFIG] Configuration saved successfully!
[CONFIG] Note: Some settings require restart to take effect.
```

## Configuration Parameters

### Vehicle Settings
- **vehicle_id**: Unique identifier for this node (used in JSON payloads)

### WiFi Settings
- **wifi_ssid**: WiFi network name
- **wifi_password**: WiFi password
- **use_wifi**: 1 for WiFi, 0 for LoRa

### OBD-II Settings
- **obdname**: BLE adapter device name (e.g., "OBDII", "VEEPEAK")
- **obduuid**: Service and Characteristic UUIDs (more reliable than name)

### Reticulum Settings
- **rnsapp**: Application name for Reticulum service discovery
- **rnsdest**: Specific destination address (hex) or empty for announce mode

### Hardware Settings
- **enable_gps**: Enable/disable GPS module
- **enable_led**: Enable/disable status LED
- **led_pin**: GPIO pin for status LED

## Tips

1. **First Time Setup:**
   - Connect via Serial Monitor
   - Type `help` to see all commands
   - Configure WiFi credentials first
   - Save configuration

2. **Changing WiFi:**
   - Use `wifi <ssid> <password>` command
   - Save with `save` command
   - Device will reconnect automatically

3. **Resetting Configuration:**
   - Use `reset` command to restore defaults from config.h
   - Configuration is automatically saved after reset

4. **Troubleshooting:**
   - Use `show` command to verify current settings
   - Check serial output for configuration errors
   - If device won't boot, you may need to reflash firmware

## Migration from config.h

If you previously configured via `config.h`:
1. Your settings are still in `config.h` as defaults
2. On first boot, defaults are loaded and saved
3. You can then use serial commands to change settings
4. Or continue editing `config.h` and use `reset` command

## Advanced: Programmatic Configuration

For advanced users, you can also access configuration programmatically:

```cpp
#include "config_manager.h"

// Get configuration values
const char* vehicleId = getVehicleID();
const char* wifiSSID = getWiFiSSID();
bool useWiFi = getUseWiFiForReticulum();

// Get full config structure
RuntimeConfig* config = getRuntimeConfig();
config->vehicle_id[0] = 'M'; // Modify (then call saveConfig())
```

## Notes

- Configuration is stored in ESP32's Preferences (non-volatile)
- Configuration version is tracked for migration
- Invalid or corrupted config automatically falls back to defaults
- Some settings require device restart to take full effect

