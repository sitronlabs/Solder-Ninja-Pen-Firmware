/* Self header */
#include "com.h"

/* Project */
#include "../cfg/config.h"
#include "../gen/version.h"
#include "app/app.h"
#include "log/log.h"
#include "settings/settings.h"

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
    static StaticJsonDocument<4 * CONFIG_COMMAND_LENGTH_LIMIT> doc;
    DeserializationError json_res = deserializeJson(doc, str, len);
    if (json_res != DeserializationError::Ok) {
        Serial.println("{\"result\":\"failure\", \"errors\" : [\"Failed to parse command!\"]}");
        log_e("Failed to parse command json (%d)!", json_res.code());
        return -1;
    }

    /* Command to retrieve the firmware version */
    if (doc[F("action")] == F("firmware_version_get")) {
        StaticJsonDocument<128> response;
        response["result"] = "success";
        response["firmware_version"] = k_version_string;
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

    /* Command to retrieve the product and serial numbers */
    else if (doc[F("action")] == F("product_information_get")) {

        /* Retrieve product information if available */
        char product_number[12 + 1];
        char serial_number[12 + 1];
        res = settings_product_get(product_number, serial_number);  // TODO Change to prevent overflow
        if (res == 1) {
            StaticJsonDocument<128> response;
            response["result"] = "success";
            response["product_number"] = product_number;
            response["serial_number"] = serial_number;
            serializeJson(response, Serial);
            Serial.println();
        } else {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"No product information!\"]}"));
        }
    }

    /* Command to set the product and serial numbers */
    else if (doc[F("action")] == F("product_information_set")) {

        /* Retrieve and verify arugments */
        const char *product_number = doc["product_number"];
        const char *serial_number = doc["serial_number"];
        if (strlen(product_number) <= 0 || strlen(product_number) > 12) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid product number!\"]}"));
            return 0;
        }
        if (strlen(serial_number) <= 0 || strlen(serial_number) > 12) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Invalid serial number!\"]}"));
            return 0;
        }

        /* Save */
        res = settings_product_set(product_number, serial_number);
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
        char text_line1[12 + 1];
        char text_line2[12 + 1];
        res = settings_user_get(icon, text_line1, text_line2);  // TODO Change to prevent overflow
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
            // log_t("icon[%u] = 0x%02X", i, icon[i]);
        }

        /* Handle name */
        const char *line1 = doc["name"][0];
        const char *line2 = doc["name"][1];
        if ((strlen(line1) <= 0) ||  //
            (strlen(line2) <= 0)) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Name too short!\"]}"));
            return 0;
        } else if ((strlen(line1) > 12) ||  //
                   (strlen(line2) > 12)) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Name too long!\"]}"));
            return 0;
        }

        /* Save */
        res = settings_user_set(icon, line1, line2);
        if (res < 0) {
            Serial.println(F("{\"result\":\"failure\", \"errors\":[\"Failed to save settings!\"]}"));
            return 0;
        }

        /* Report success */
        Serial.println(F("{\"result\":\"success\"}"));
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
