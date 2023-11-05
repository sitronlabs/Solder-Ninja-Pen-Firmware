/* Self header */
#include "com.h"

/* Project */
#include "../../gen/version.h"
#include "../app/app.h"
#include "../cfg/config.h"
#include "../log/log.h"
#include "../settings/settings.h"

/* Arduino libraries */
#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief
 * @param
 * @return
 */
int com_setup(void) {

#if R8A
    /* Setup uart over usb */
    Serial.begin(115200);
#endif

    /* Return success */
    return 0;
}
/**
 * @brief
 * @param str
 * @param len
 * @return
 */
int com_command_process(const char *const str, const size_t len) {
    int res;

    /* Parse json */
    static StaticJsonDocument<CONFIG_COMMAND_LENGTH_LIMIT> doc;
    DeserializationError json_res = deserializeJson(doc, str, len);
    if (json_res != DeserializationError::Ok) {
        log_e("Failed to parse command json!");
        return -1;
    }

    /* Command to retrieve the firmware version */
    if (doc[F("action")] == F("firmware_version_get")) {
        StaticJsonDocument<128> response;
        response["result"] = "success";
        response["firmware_version"] = version_string;
        serializeJson(response, Serial);
        Serial.println();
    }

    /* Command to dump settings */
    else if (doc[F("action")] == F("settings_memory_dump")) {
        Serial.print("{\"settings\": [");
        bool failure = false;
        bool comma = false;
        for (size_t i = 0;;) {
            uint8_t data[32];
            res = settings_memory_read(i, data, 32);
            if (res == 0 || res == -EINVAL) {
                break;
            } else if (res < 0) {
                failure = true;
                break;
            } else {
                for (size_t j = 0; j < (size_t)res; j++) {
                    if (comma == false) {
                        Serial.printf("%u", data[j]);
                        comma = true;
                    } else {
                        Serial.printf(",%u", data[j]);
                    }
                }
                i += res;
            }
        }
        Serial.printf("], \"result\": \"%s\"}\r\n", failure ? "failure" : "success");
    }

    /* Command to wipe settings */
    else if (doc[F("action")] == F("settings_memory_wipe")) {
        res = settings_memory_wipe();
        if (res == 0) {
            Serial.println("{\"result\":\"success\"}");
        } else {
            Serial.println("{\"result\":\"failure\", \"errors\" : [\"Unknown error!\"]}");
        }
    }

    /* Unknown command */
    else {
        Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Unknown command!\"]}"));
    }

    /* Return success */
    return 0;
}

/**
 * Hardware version get
 * Firmware version get
 * Settings get all
 * Settings set
 * App_heating_turn_on
 * app.lock -> app_lock(COM);
 * app.unlock -> app_unlock(COM);
 */
int com_task(void) {
    int res;

    /* Process incoming commands */
    static char cmd_bytes[CONFIG_COMMAND_LENGTH_LIMIT];
    static size_t cmd_index = 0;
    while (Serial.available() > 0) {
        char rx = Serial.read();

        /* Skip any garbage data that might be sent accidentaly when the port is opened */
        if (cmd_index == 0 && rx != '{') {
            continue;
        }

        /* Wait for a new line */
        else if (rx == '\r' || rx == '\n') {

            /* Skip empty lines */
            if (cmd_index <= 0) {
                continue;
            }

            /* Process command */
            res = com_command_process(cmd_bytes, cmd_index);
            if (res < 0) {
                log_w("Failed to process command!");
            }

            /* Reset index */
            cmd_index = 0;
        }

        /* Append received byte */
        else {
            cmd_bytes[cmd_index++] = rx;
            if (cmd_index >= CONFIG_COMMAND_LENGTH_LIMIT) {
                Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Command too long!\"]}"));
                cmd_index = 0;
            }
        }
    }

    /* Return success */
    return 0;
}
