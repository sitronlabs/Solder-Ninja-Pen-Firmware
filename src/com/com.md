# Solder Ninja Pen - Serial Communication API Reference

## Protocol Overview

All commands are sent as JSON objects over serial (115200 baud) with the following format:
- Request: `{"action": "<function_name>", ...arguments}`
- Response: `{"result": "success"|"failure", ...output_parameters, "errors": [...]}`

The API is designed to feel like calling C functions. Function names match the underlying C function names where possible.

**Response Format:**
- `result`: Either `"success"` or `"failure"`
- Output parameters are returned directly (mimicking C function output parameters)
- `errors`: Array of error messages (only present on error)

All responses end with `\r\n`.

---

## Commands

### 1. `firmware_version_get`

Retrieves all firmware version information including version numbers, build timestamp, git commit, and branch.

**Request:**
```json
{"action": "firmware_version_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "string": "v0.0.0+20251031234039.git3222e0f.feature/com.dirty",
  "core": {
    "major": 0,
    "minor": 0,
    "patch": 0
  },
  "datetime": "20251031234039",
  "git": {
    "hash": "3222e0f",
    "branch": "feature/com",
    "dirty": true,
    "post": 0
  }
}
```

**Response Fields:**
- `string`: Complete version string summary
- `core.major`: Major version number (integer)
- `core.minor`: Minor version number (integer)
- `core.patch`: Patch version number (integer)
- `datetime`: Build timestamp in UTC (string, format: YYYYMMDDHHmmss)
- `git.hash`: Git commit hash (string)
- `git.branch`: Git branch name (string)
- `git.dirty`: Whether the working tree was dirty at build time (boolean)
- `git.post`: Post-release version number (integer)

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Error message"]
}
```

---

### 2. `eeprom_dump`

Dumps the entire EEPROM contents as an array of bytes.

**Request:**
```json
{"action": "eeprom_dump"}
```

**Success Response:**
```json
{
  "data": [0, 1, 2, 3, ...],
  "result": "success"
}
```

**Failure Response:**
```json
{
  "data": [...],
  "result": "failure"
}
```

**Notes:**
- Returns all EEPROM bytes starting from address 0 until end of memory
- The `data` array contains byte values (0-255) as integers

---

### 3. `eeprom_read`

Reads a specified number of bytes from the EEPROM starting at a given address.

**C Function:** `int settings_eeprom_read(const size_t address, uint8_t *const data, const size_t length)`

**Request:**
```json
{
  "action": "eeprom_read",
  "address": 0,
  "length": 32
}
```

**Arguments:**
- `address` (number, required): Starting address (0-indexed)
- `length` (number, required): Number of bytes to read (minimum 1)

**Success Response:**
```json
{
  "data": [0, 1, 2, 3, ...],
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Invalid length!"]
}
```

**Notes:**
- The `data` array contains `length` byte values (0-255) as integers
- Mimics C function behavior: fills the `data` output parameter

---

### 4. `eeprom_write`

Writes data to the EEPROM at a specified address.

**C Function:** `int settings_eeprom_write(const size_t address, const uint8_t *const data, const size_t length)`

**Request:**
```json
{
  "action": "eeprom_write",
  "address": 0,
  "data": [255, 254, 253, ...]
}
```

**Arguments:**
- `address` (number, required): Starting address (0-indexed)
- `data` (array of numbers, required): Array of byte values (0-255) to write

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["No data array!", "Data array is empty!"]
}
```

**Notes:**
- Data array must not be empty
- Writes are performed in chunks for optimal performance

---

### 5. `eeprom_wipe`

Wipes (erases) the entire EEPROM by writing 0xFF to every byte.

**C Function:** `int settings_eeprom_wipe(void)`

**Request:**
```json
{"action": "eeprom_wipe"}
```

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Unknown error!"]
}
```

**Notes:**
- This operation may take significant time as it writes to every byte of the EEPROM
- EEPROM contents will be automatically rebuilt from `settings.json` in flash if available, so this command is rarely needed alone

---

### 6. `flash_wipe`

Wipes (deletes) all files from flash storage.

**C Function:** `int settings_flash_wipe(void)`

**Request:**
```json
{"action": "flash_wipe"}
```

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Flash is currently in use!", "Unknown error!"]
}
```

**Notes:**
- Deletes all files from flash storage, including the `settings.json` file
- The `settings.json` file will be automatically rebuilt from EEPROM contents if available
- Will fail with "Flash is currently in use!" error if flash is busy

---

### 7. `settings_wipe`

Permanently deletes all settings from both EEPROM and flash storage.

**C Function:** `int settings_wipe(void)`

**Request:**
```json
{"action": "settings_wipe"}
```

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Unknown error!"]
}
```

**Notes:**
- **Warning:** This command permanently deletes all settings from both EEPROM and flash
- Includes critical information such as product information (product number, revision, serial number)
- Use with caution as this operation cannot be undone
- Settings will not be automatically restored after this operation

---

### 8. `product_information_get`

Retrieves the product information (product number, revision, and serial number).

**C Function:** `int settings_product_get(char *const number, char *const revision, char *const serial)`

**Request:**
```json
{"action": "product_information_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "number": "SLTO00001",
  "revision": "R8A",
  "serial": "12345678"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["No product information!"]
}
```

**Notes:**
- Mimics C function behavior: fills the three output string parameters (`number`, `revision`, `serial`)

---

### 9. `product_information_set`

Sets the product information (product number, revision, and serial number).

**C Function:** `int settings_product_set(const char *const number, const char *const revision, const char *const serial)`

**Request:**
```json
{
  "action": "product_information_set",
  "number": "SLTO00001",
  "revision": "R8A",
  "serial": "12345678"
}
```

**Arguments:**
- `number` (string, required): Product number (max length defined by `CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH`)
- `revision` (string, required): Product revision (max length defined by `CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH`)
- `serial` (string, required): Serial number (max length defined by `CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH`)

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Invalid product number!", "Invalid product revision!", "Invalid serial number!", "Failed to save settings!"]
}
```

**Notes:**
- All string fields must have length > 0 and not exceed their maximum lengths

---

### 10. `user_information_get`

Retrieves the user information (icon data and name).

**C Function:** `int settings_user_get(uint8_t *const icon, char *const line1, char *const line2)`

**Request:**
```json
{"action": "user_information_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "user": {
    "icon": [0, 1, 2, ..., 31],
    "name": ["Line 1", "Line 2"]
  }
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["No user information!"]
}
```

**Notes:**
- Icon is always 32 bytes
- Name is an array of exactly 2 strings (line 1 and line 2)
- Mimics C function behavior: fills the three output parameters (`icon`, `line1`, `line2`)

---

### 11. `user_information_set`

Sets the user information (icon data and name).

**C Function:** `int settings_user_set(const uint8_t icon[32], const char *line1, const char *line2)`

**Request:**
```json
{
  "action": "user_information_set",
  "icon": [0, 1, 2, ..., 31],
  "name": ["Line 1", "Line 2"]
}
```

**Arguments:**
- `icon` (array of numbers, required): Icon data as array of exactly 32 bytes (values 0-255)
- `name` (array of strings, required): Name as array of exactly 2 strings (line 1 and line 2)

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Invalid icon size!", "Name too short!", "Name too long!", "Failed to save settings!"]
}
```

**Notes:**
- Icon array must contain exactly 32 elements
- Name array must contain exactly 2 non-empty strings
- Name strings must not exceed their maximum lengths (defined by `CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH` and `CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH`)

---

### 12. `controller_status_get`

Retrieves the current device status (state, target temperature, measured temperature).

**Request:**
```json
{"action": "controller_status_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "status": {
    "state": <state_value>,
    "temperature": {
      "target": <target_temperature>,
      "measured": <measured_temperature>
    }
  }
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Failed to get target temperature!", "Failed to get temperature!"]
}
```

**Notes:**
- `state`: Application state value (integer)
  - `APP_STATE_LOCKED` (0)
  - `APP_STATE_ASLEEP` (1)
  - `APP_STATE_ACTIVE` (2)
- `temperature.target`: Target temperature in Celsius (float)
- `temperature.measured`: Measured temperature in Celsius (float)

---

### 13. `controller_temperature_target_get`

Retrieves the target temperature setting (persisted in EEPROM).

**C Function:** `int controller_temperature_target_get(float &temperature_c)`

**Request:**
```json
{"action": "controller_temperature_target_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "temperature_c": 350.0
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Failed to get temperature!"]
}
```

---

### 14. `controller_temperature_target_set`

Sets the target temperature (persisted in EEPROM).

**C Function:** `int controller_temperature_target_set(const float temperature_c)`

**Request:**
```json
{
  "action": "controller_temperature_target_set",
  "temperature_c": 350.0
}
```

**Arguments:**
- `temperature_c` (number, required): Target temperature in Celsius

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Invalid temperature!", "Failed to set temperature!"]
}
```

---

### 15. `controller_temperature_measured_get`

Retrieves the current measured temperature from the heating element.

**C Function:** `int controller_temperature_measured_get(float &temperature_c)`

**Request:**
```json
{"action": "controller_temperature_measured_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "temperature_c": 345.2
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Failed to get temperature!"]
}
```

**Notes:**
- Will fail if the heating tip is not connected

---

### 16. `controller_lock`

Locks the device (disables heating for safety).

**C Function:** `int controller_lock(enum controller_lock_reason reason)`

**Request:**
```json
{
  "action": "controller_lock"
}
```

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Failed to lock device!"]
}
```

**Notes:**
- Locks the device and disables heating immediately
- The lock reason is automatically set to `CONTROLLER_LOCK_REASON_REMOTE`

---

### 17. `controller_unlock`

Unlocks the device (allows heating if in active state).

**C Function:** `int controller_unlock(enum controller_unlock_reason reason)`

**Request:**
```json
{
  "action": "controller_unlock"
}
```

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Failed to unlock device!"]
}
```

**Notes:**
- Unlocks the device and may enable heating if device is in active state
- The unlock reason is automatically set to `CONTROLLER_UNLOCK_REASON_REMOTE`

---

### 18. `accelerometer_idle_time_get`

Retrieves the current accelerometer idle time setting (time after which no movement is interpreted as inactivity).

**C Function:** `uint32_t accelerometer_idle_time_get(void)`

**Request:**
```json
{"action": "accelerometer_idle_time_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "time_ms": 40000
}
```

**Response Fields:**
- `time_ms`: Idle time in milliseconds (uint32_t)

**Notes:**
- The idle time determines how long the device must remain still before it's considered inactive
- Value is persisted in EEPROM settings

---

### 19. `accelerometer_idle_time_set`

Sets the accelerometer idle time (time after which no movement is interpreted as inactivity).

**C Function:** `int accelerometer_idle_time_set(const uint32_t time_ms)`

**Request:**
```json
{
  "action": "accelerometer_idle_time_set",
  "time_ms": 30000
}
```

**Arguments:**
- `time_ms` (number, required): Idle time in milliseconds (must be between 10000 and 40000)

**Success Response:**
```json
{
  "result": "success"
}
```

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["Idle time too short!", "Idle time too long!", "Failed to set idle time!"]
}
```

**Notes:**
- Valid range: 10000 ms (10 seconds) to 40000 ms (40 seconds) as defined by `CONFIG_ACCEL_IDLE_TIME_MIN` and `CONFIG_ACCEL_IDLE_TIME_MAX`
- The new value is persisted to EEPROM settings
- Accelerometer will be reconfigured on the next task cycle

---

### 20. `heating_time_get`

Retrieves the total heating time (diagnostics data).

**C Function:** `int settings_diagnostics_heating_time_get(uint32_t &seconds)`

**Request:**
```json
{"action": "heating_time_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "heating": {
    "time": 12345
  }
}
```

**Response Fields:**
- `heating.time`: Total heating time in seconds (uint32_t)

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["No heating time data!"]
}
```

**Notes:**
- Returns diagnostics data stored in EEPROM
- Value represents cumulative heating time

---

### 21. `usb_voltage_max_get`

Retrieves the maximum USB voltage recorded (diagnostics data).

**C Function:** `int settings_diagnostics_usb_voltage_max_get(float &voltage)`

**Request:**
```json
{"action": "usb_voltage_max_get"}
```

**Success Response:**
```json
{
  "result": "success",
  "usb": {
    "voltage_max": 5.2
  }
}
```

**Response Fields:**
- `usb.voltage_max`: Maximum USB voltage recorded in volts (float)

**Failure Response:**
```json
{
  "result": "failure",
  "errors": ["No USB voltage data!"]
}
```

**Notes:**
- Returns diagnostics data stored in EEPROM
- Value represents the maximum USB voltage detected during device operation

---

## Failure Responses

All commands follow a consistent failure response format:

```json
{
  "result": "failure",
  "errors": ["Error message 1", "Error message 2", ...]
}
```

**Common Failure Scenarios:**
- Invalid JSON: `{"result":"failure", "errors": ["Failed to parse command (<error>)!"]}`
- Unknown command: `{"result":"failure", "errors": ["Unknown command!"]}`
- Command too long: `{"result":"failure", "errors": ["Command too long!"]}`

---

## Command Naming Convention

Commands follow the C function naming pattern: `<module>_<resource>_<action>`

**Examples:**
- `eeprom_read` - matches `int settings_eeprom_read(...)`
- `product_information_get` - matches `int settings_product_get(...)`
- `controller_temperature_target_get` - matches `int controller_temperature_target_get(float &temperature_c)`
- `firmware_version_get` - convenience wrapper for firmware version

**Benefits:**
- Direct mapping to C function names
- Easy to understand and document
- Consistent with codebase naming conventions
- Feels like calling C functions over JSON

---

## Usage Examples

### Get Firmware Version
```json
Request:  {"action": "firmware_version_get"}
Response: {"result":"success","string":"v0.0.0+20251031234039.git3222e0f.feature/com.dirty","core":{"major":0,"minor":0,"patch":0},"datetime":"20251031234039","git":{"hash":"3222e0f","branch":"feature/com","dirty":true,"post":0}}
```

### Read EEPROM
```json
Request:  {"action": "eeprom_read", "address": 0, "length": 32}
Response: {"data":[0,1,2,3,...],"result":"success"}
```

### Set Product Information
```json
Request:  {"action": "product_information_set", "number": "SLTO00001", "revision": "R8A", "serial": "12345"}
Response: {"result":"success"}
```

### Get Target Temperature
```json
Request:  {"action": "controller_temperature_target_get"}
Response: {"result":"success","temperature_c":350.0}
```

### Get Status
```json
Request:  {"action": "controller_status_get"}
Response: {"result":"success","status":{"state":2,"temperature":{"target":350.0,"measured":345.2}}}
```
