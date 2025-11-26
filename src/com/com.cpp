/* Self header */
#include "com.h"

/* Project */
#include "../gen/version.h"
#include "controller/controller.h"
#include "element/element.h"
#include "interface/accelerometer.h"
#include "log/log.h"
#include "settings/settings.h"

/* Arduino libraries */
#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief Initialize serial communication interface
 *
 * Sets up the serial communication port for receiving and sending JSON commands.
 *
 * @return 0 on success (always returns 0)
 */
int com_setup(void) {

#if R8A
    /* Initialize USB serial port at 115200 baud for command interface */
    Serial.begin(115200);
#endif

    /* Return success */
    return 0;
}

/**
 * @brief Parse and execute a JSON command
 *
 * Parses the JSON command string, identifies the action, and executes the corresponding
 * handler. Sends a JSON response to the serial port indicating success or failure.
 *
 * @param[in] str Pointer to the JSON command string (must be null-terminated or len-valid)
 * @param[in] len Length of the command string in bytes
 * @return 0 on success, negative error code on failure
 */
static int m_command_process(const char *const str, const size_t len) {
    int res;

    /* Parse json */
    static StaticJsonDocument<CONFIG_COMMAND_JSON_DOCUMENT_SIZE> doc;
    DeserializationError json_res = deserializeJson(doc, str, len);
    if (json_res != DeserializationError::Ok) {
        Serial.printf("{\"result\":\"failure\", \"errors\" : [\"Failed to parse command (%s)!\"]}\r\n", json_res.c_str());
        log_e("Failed to parse command json (%s)!", json_res.c_str());
        return -1;
    }

    /* Command to retrieve the firmware version */
    if (doc[F("action")] == F("firmware_version_get")) {
        StaticJsonDocument<512> response;
        response["result"] = "success";
        response["string"] = k_version_string;
        response["core"]["major"] = k_version_major;
        response["core"]["minor"] = k_version_minor;
        response["core"]["patch"] = k_version_patch;
        response["datetime"] = k_version_datetime_utc;
        response["git"]["hash"] = k_version_commit;
        response["git"]["branch"] = k_version_branch;
        response["git"]["dirty"] = k_version_dirty;
        response["git"]["post"] = k_version_post;
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to dump eeprom contents */
    else if (doc[F("action")] == F("eeprom_dump")) {

        /* Start response */
        Serial.print("{\"data\": [");

        /* Read eeprom by chunks of arbitrary size to speed up reading while avoiding large memory allocations */
        bool failure = false;
        bool comma = false;
        for (size_t i = 0;;) {

            /* Build chunk */
            uint8_t chunk[32];
            res = settings_eeprom_read(0 + i, chunk, 32);
            if (res == 0 || res == -EINVAL) {
                break;
            } else if (res < 0) {
                failure = true;
                break;
            } else {

                /* Append contents of chunk to response */
                for (size_t j = 0; j < (size_t)res; j++) {
                    if (comma == false) {
                        Serial.printf("%" PRIu8, chunk[j]);
                        comma = true;
                    } else {
                        Serial.printf(",%" PRIu8, chunk[j]);
                    }
                }

                /* Increment the number of bytes read so far */
                i += res;
            }
        }

        /* End response */
        Serial.printf("], \"result\": \"%s\"}\r\n", failure ? "failure" : "success");
    }

    /* Command to read from eeprom */
    else if (doc[F("action")] == F("eeprom_read")) {

        /* Validate address */
        const size_t address = doc["address"] | 0;

        /* Validate length */
        const size_t length = doc["length"] | 0;
        if (length < 1) {
            Serial.println(F("{\"result\":\"failure\", \"errors\" : [\"Invalid length!\"]}"));
            return 0;
        }

        /* Start response */
        Serial.print("{\"data\": [");

        /* Read eeprom by chunks of arbitrary size to speed up reading while avoiding large memory allocations */
        bool failure = false;
        bool comma = false;
        for (size_t i = 0; i < length;) {

            /* Build chunk */
            uint8_t chunk[32];
            size_t chunk_length = (length - i > 32) ? 32 : length - i;
            res = settings_eeprom_read(address + i, chunk, chunk_length);
            if (res == 0 || res == -EINVAL) {
                break;
            } else if (res < 0) {
                failure = true;
                break;
            } else {

                /* Append contents of chunk to response */
                for (size_t j = 0; j < (size_t)res; j++) {
                    if (comma == false) {
                        Serial.printf("%" PRIu8, chunk[j]);
                        comma = true;
                    } else {
                        Serial.printf(",%" PRIu8, chunk[j]);
                    }
                }

                /* Increment the number of bytes read so far */
                i += res;
            }
        }

        /* End response */
        Serial.printf("], \"result\": \"%s\"}\r\n", failure ? "failure" : "success");
    }

    /* Command to write to eeprom */
    else if (doc[F("action")] == F("eeprom_write")) {

        /* Validate address */
        const size_t address = doc["address"] | 0;

        /* Validate data array */
        JsonArray data = doc["data"].as<JsonArray>();
        if (data.isNull()) {
            Serial.println(F("{\"result\":\"failure\", \"errors\" : [\"No data array!\"]}"));
            return 0;
        }
        const size_t length = data.size() | 0;
        if (length < 1) {
            Serial.println(F("{\"result\":\"failure\", \"errors\" : [\"Data array is empty!\"]}"));
            return 0;
        }

        /* Write to eeprom by chunks sufficiently large to benefit of page write operations */
        bool failure = false;
        for (size_t i = 0; i < length;) {

            /* Build chunk */
            uint8_t chunk[128];
            size_t chunk_length = (length - i > 128) ? 128 : length - i;
            for (size_t j = 0; j < chunk_length; j++) {
                chunk[j] = data[i + j].as<uint8_t>();
            }

            /* Write chunk */
            res = settings_eeprom_write(address + i, chunk, chunk_length);
            if (res < 0) {
                failure = true;
                break;
            }

            /* Increment the number of bytes written so far */
            i += chunk_length;
        }

        /* Send response */
        Serial.printf("{\"result\": \"%s\"}\r\n", failure ? "failure" : "success");
    }

    /* Command to wipe EEPROM
     * Writes 0xFF to every byte, which may take significant time.
     * EEPROM contents will be automatically rebuilt from settings.json in flash if available, so this command is not really useful alone. */
    else if (doc[F("action")] == F("eeprom_wipe")) {
        res = settings_eeprom_wipe();
        if (res == 0) {
            Serial.println("{\"result\":\"success\"}");
        } else {
            Serial.println("{\"result\":\"failure\", \"errors\" : [\"Unknown error!\"]}");
        }
    }

    /* Command to wipe flash
     * Deletes all files from flash storage.
     * The settings.json file will be automatically rebuilt from EEPROM contents if available. */
    else if (doc[F("action")] == F("flash_wipe")) {
        res = settings_flash_wipe();
        if (res == 0) {
            Serial.println("{\"result\":\"success\"}");
        } else if (res == -EBUSY) {
            Serial.println("{\"result\":\"failure\", \"errors\" : [\"Flash is currently in use!\"]}");
        } else {
            Serial.println("{\"result\":\"failure\", \"errors\" : [\"Unknown error!\"]}");
        }
    }

    /* Command to wipe all settings
     * Permanently deletes all settings from both EEPROM and flash, including
     * critical information such as product information. Use with caution. */
    else if (doc[F("action")] == F("settings_wipe")) {
        res = settings_wipe();
        if (res == 0) {
            Serial.println("{\"result\":\"success\"}");
        } else {
            Serial.println("{\"result\":\"failure\", \"errors\" : [\"Unknown error!\"]}");
        }
    }

    /* Command to retrieve the product and serial numbers */
    else if (doc[F("action")] == F("product_information_get")) {

        /* Retrieve product information if available */
        char number[CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH + 1] = {0};
        char revision[CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH + 1] = {0};
        char serial[CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH + 1] = {0};
        res = settings_product_get(number, revision, serial);
        if (res == 1) {
            StaticJsonDocument<128> response;
            response["result"] = "success";
            response["number"] = number;
            response["revision"] = revision;
            response["serial"] = serial;
            serializeJson(response, Serial);
            Serial.println();
        } else {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"No product information!\"]}"));
        }
    }

    /* Command to set the product and serial numbers */
    else if (doc[F("action")] == F("product_information_set")) {

        /* Retrieve and verify arugments */
        const char *number = doc["number"];
        const char *revision = doc["revision"];
        const char *serial = doc["serial"];
        if (strlen(number) <= 0 || strlen(number) > CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid product number!\"]}"));
            return 0;
        }
        if (strlen(revision) <= 0 || strlen(revision) > CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid product revision!\"]}"));
            return 0;
        }
        if (strlen(serial) <= 0 || strlen(serial) > CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid serial number!\"]}"));
            return 0;
        }

        /* Save */
        res = settings_product_set(number, revision, serial);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to save settings!\"]}"));
            return 0;
        }

        /* Report success */
        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to retrieve the user information */
    else if (doc[F("action")] == F("user_information_get")) {

        /* Retrieve user information if available */
        uint8_t icon[32];
        char text_line1[CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH + 1];
        char text_line2[CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH + 1];
        res = settings_user_get(icon, text_line1, text_line2);
        if (res == 1) {
            StaticJsonDocument<1024> response;
            response["result"] = "success";
            for (size_t i = 0; i < 32; i++) {
                response["user"]["icon"][i] = icon[i];
            }
            response["user"]["name"][0] = text_line1;
            response["user"]["name"][1] = text_line2;
            serializeJson(response, Serial);
            Serial.println();
        } else {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"No user information!\"]}"));
        }
    }

    /* Command to set the user information */
    else if (doc[F("action")] == F("user_information_set")) {

        /* Handle icon */
        size_t icon_size = doc["icon"].size();
        if (icon_size != 32) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid icon size!\"]}"));
            return 0;
        }
        uint8_t icon[32];
        for (size_t i = 0; i < icon_size; i++) {
            icon[i] = doc["icon"][i];
        }

        /* Handle name */
        const char *line1 = doc["name"][0];
        const char *line2 = doc["name"][1];
        if ((strlen(line1) <= 0) ||  //
            (strlen(line2) <= 0)) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Name too short!\"]}"));
            return 0;
        } else if ((strlen(line1) > CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH) ||  //
                   (strlen(line2) > CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH)) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Name too long!\"]}"));
            return 0;
        }

        /* Save */
        res = settings_user_set(icon, line1, line2);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to save settings!\"]}"));
            return 0;
        }

        /* Send success */
        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to get target temperature */
    else if (doc[F("action")] == F("controller_temperature_target_get")) {
        float temperature_c = 0;
        res = controller_temperature_target_get(temperature_c);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to get temperature!\"]}"));
            return 0;
        }

        /* Build response */
        StaticJsonDocument<128> response;
        response["result"] = "success";
        response["temperature_c"] = temperature_c;

        /* Send response */
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to set target temperature */
    else if (doc[F("action")] == F("controller_temperature_target_set")) {

        /* Retrieve and validate temperature */
        const float temperature_c = doc["temperature_c"] | 0.0f;
        if (temperature_c < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid temperature!\"]}"));
            return 0;
        }

        /* Set target temperature */
        res = controller_temperature_target_set(temperature_c);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to set temperature!\"]}"));
            return 0;
        }

        /* Send success */
        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to get measured temperature */
    else if (doc[F("action")] == F("controller_temperature_measured_get")) {
        float temperature_c = 0;
        res = controller_temperature_measured_get(temperature_c);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to get temperature!\"]}"));
            return 0;
        }

        /* Build response */
        StaticJsonDocument<128> response;
        response["result"] = "success";
        response["temperature_c"] = temperature_c;

        /* Send response */
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to lock the device */
    else if (doc[F("action")] == F("controller_lock")) {
        res = controller_lock(CONTROLLER_LOCK_REASON_REMOTE);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to lock device!\"]}"));
            return 0;
        }

        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to unlock the device */
    else if (doc[F("action")] == F("controller_unlock")) {
        res = controller_unlock(CONTROLLER_UNLOCK_REASON_REMOTE);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to unlock device!\"]}"));
            return 0;
        }

        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to retrieve the device status */
    else if (doc[F("action")] == F("controller_status_get")) {

        /* Retrieve temperature */
        float temperature_target_c = 0;
        res = controller_temperature_target_get(temperature_target_c);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to get target temperature!\"]}"));
            return 0;
        }
        float temperature_measured_c = 0;
        res = controller_temperature_measured_get(temperature_measured_c);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to get temperature!\"]}"));
            return 0;
        }

        /* Retrieve state and convert to string */
        enum controller_state state = controller_state_get();
        const char *state_string;
        switch (state) {
            case CONTROLLER_STATE_LOCKED:
                state_string = "locked";
                break;
            case CONTROLLER_STATE_ASLEEP:
                state_string = "asleep";
                break;
            case CONTROLLER_STATE_ACTIVE:
                state_string = "active";
                break;
            default:
                state_string = "unknown";
                break;
        }

        /* Build response */
        StaticJsonDocument<1024> response;
        response["result"] = "success";
        response["state"] = state_string;
        response["temperature"]["target"] = temperature_target_c;
        response["temperature"]["measured"] = temperature_measured_c;
        response["boost"] = controller_boost_activated_get();
        response["element"][0]["connected"] = (element_connected_get() != 0);
        response["element"][0]["temperature"] = temperature_measured_c;
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to retrieve the accelerometer idle time */
    else if (doc[F("action")] == F("accelerometer_idle_time_get")) {

        /* Retrieve accelerometer idle time */
        uint32_t time_ms = accelerometer_idle_time_get();
        StaticJsonDocument<128> response;
        response["result"] = "success";
        response["time_ms"] = time_ms;
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to set the accelerometer idle time */
    else if (doc[F("action")] == F("accelerometer_idle_time_set")) {

        /* Retrieve and verify argument */
        uint32_t time_ms = doc["time_ms"] | 0;
        if (time_ms < CONFIG_ACCEL_IDLE_TIME_MIN) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Idle time too short!\"]}"));
            return 0;
        }
        if (time_ms > CONFIG_ACCEL_IDLE_TIME_MAX) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Idle time too long!\"]}"));
            return 0;
        }

        /* Set new idle time */
        res = accelerometer_idle_time_set(time_ms);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to set idle time!\"]}"));
            return 0;
        }

        /* Report success */
        Serial.println(F("{\"result\":\"success\"}"));
    }

    /* Command to get heating time */
    else if (doc[F("action")] == F("heating_time_get")) {
        uint32_t heating_time = 0;
        res = settings_diagnostics_heating_time_get(heating_time);
        if (res == 1) {
            StaticJsonDocument<128> response;
            response["result"] = "success";
            response["heating"]["time"] = heating_time;
            serializeJson(response, Serial);
            Serial.println();
        } else {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"No heating time data!\"]}"));
        }
    }

    /* Command to get max USB voltage */
    else if (doc[F("action")] == F("usb_voltage_max_get")) {
        float voltage = 0.0f;
        res = settings_diagnostics_usb_voltage_max_get(voltage);
        if (res == 1) {
            StaticJsonDocument<128> response;
            response["result"] = "success";
            response["usb"]["voltage_max"] = voltage;
            serializeJson(response, Serial);
            Serial.println();
        } else {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"No USB voltage data!\"]}"));
        }
    }

    /* Command to reboot the system */
    else if (doc[F("action")] == F("system_reboot")) {
        /* Send success response */
        Serial.println(F("{\"result\":\"success\"}"));

        /* Flush serial output to ensure response is sent */
        Serial.flush();

        /* Small delay to ensure response is transmitted */
        delay(100);

        /* Reboot the system */
#if R8A
        /* Use rp2040.restart() for Arduino-Pico */
        rp2040.restart();
#else
        /* For STM32, trigger watchdog reset */
        /* The watchdog will reset the system if not fed */
        while (1) {
            /* Infinite loop - watchdog will reset the system */
        }
#endif
    }

    /* Unknown command */
    else {
        Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Unknown command!\"]}"));
    }

    /* Return success */
    return 0;
}

/**
 * @brief Process incoming serial commands
 *
 * Reads incoming serial data byte-by-byte, accumulates JSON commands until a newline
 * is received, then processes the complete command. Handles edge cases such as:
 * - Garbage data when serial port is first opened (skips until '{' is found)
 * - Empty lines (ignored)
 * - Buffer overflow (sends error response and resets)
 *
 * Commands must be JSON objects starting with '{' and terminated by '\r' or '\n'.
 *
 * @return 0 on success (always returns 0, errors are logged but don't affect return value)
 */
int com_task(void) {
    int res;

    /* Command buffer for accumulating incoming bytes */
    static char buffer[CONFIG_COMMAND_LENGTH_LIMIT];
    static size_t buffer_index = 0;

    /* Process all available bytes from serial port */
    while (Serial.available() > 0) {
        char received_byte = Serial.read();

        /* Skip garbage data that may be sent when serial port is first opened.
         * Valid JSON commands always start with '{', so we wait for it before
         * starting to accumulate bytes. */
        if (buffer_index == 0 && received_byte != '{') {
            continue;
        }

        /* Command is complete when we receive a newline character */
        else if (received_byte == '\r' || received_byte == '\n') {

            /* Ignore empty lines (no data accumulated) */
            if (buffer_index <= 0) {
                continue;
            }

            /* Process the complete command */
            res = m_command_process(buffer, buffer_index);
            if (res < 0) {
                log_w("Failed to process command!");
            }

            /* Reset buffer for next command */
            buffer_index = 0;
        }

        /* Accumulate received byte into command buffer */
        else {
            buffer[buffer_index++] = received_byte;

            /* Handle buffer overflow: send error and reset */
            if (buffer_index >= CONFIG_COMMAND_LENGTH_LIMIT) {
                Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Command too long!\"]}"));
                buffer_index = 0;
            }
        }
    }

    /* Return success */
    return 0;
}
