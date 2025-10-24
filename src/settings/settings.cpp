/* Self header */
#include "settings.h"

/* Project headers */
#include "log/log.h"

/* Arduino headers */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <m24c64.h>

/* Peripherals */
static m24c64 m_eeprom;

/* Json document */
StaticJsonDocument<1024> m_doc;

/* Settings state machine */
static enum {
    STATE_0_LOAD,
    STATE_1_IDLE,
    STATE_2_SAVE,
    STATE_ERROR,
} m_sm;

/* Settings modified */
static bool m_modified;
static uint32_t m_modified_timestamp;

/**
 * @brief
 * @param
 * @return
 */
static int m_eeprom_unpack(void) {

    /* Read from memory */
    m_eeprom.seek_read(0);
    DeserializationError unpack_res = deserializeMsgPack(m_doc, m_eeprom);
    if (unpack_res != DeserializationError::Ok) {
        log_e("Failed to deserialize!");
        return -1;
    }

    /* Dirty fix for when the document is empty */
    if (m_doc.memoryUsage() == 0) {
        static const char fix[] = "{}";
        DeserializationError unpack_res = deserializeJson(m_doc, (char *)(&fix[0]));
        if (unpack_res != DeserializationError::Ok) {
            log_e("Failed to create empty document (%d)!", unpack_res.code());
            return -1;
        }
    }

    /* Return success */
    return 0;
}

/**
 * @brief Marks settings as modified and schedules save operation
 * @return 0 on success, negative error code otherwise
 */
static int m_eeprom_repack(void) {

    /* Commit settings to EEPROM */
    m_eeprom.seek_write(0);
    WriteBufferingStream m_eeprom_buffered(m_eeprom, 32);
    serializeMsgPack(m_doc, m_eeprom_buffered);
    m_eeprom_buffered.flush();

    /* Return success */
    return 0;
}

/**
 * @brief Tries to ensure settings are loaded from EEPROM
 *
 * This function attempts to load settings if they haven't been loaded yet.
 * This speeds up subsequent task calls by proactively loading settings
 * when getter functions are called.
 *
 * @note This may not be the best architecture practice as it mixes
 *       state machine logic with immediate loading, but it improves
 *       responsiveness for the common case of accessing settings.
 *
 * @return 1 if settings are loaded, 0 if not yet loaded, negative error code if loading failed
 */
static int m_settings_ensure_loaded(void) {
    int res;

    /* Try to ensure settings are loaded */
    while (m_sm != STATE_1_IDLE) {
        switch (m_sm) {
            case STATE_0_LOAD: {
                res = settings_task();
                if (res < 0) {
                    log_e("Failed to load settings!");
                    m_sm = STATE_ERROR;
                    return -1;
                }
                log_d("Settings loaded successfully");
                break;
            }

            case STATE_ERROR: {
                return -1;
            }

            case STATE_2_SAVE: {
                /* Settings are loaded but being saved, return success */
                return 1;
            }

            default: {
                log_e("Unknown state machine state!");
                return -1;
            }
        }
    }

    /* Settings are loaded and ready */
    return 1;
}

/**
 * @brief
 * @param
 * @return
 */
int settings_setup(void) {

    /* Setup eeprom */
    if (m_eeprom.setup(Wire, 0x50) < 0) {
        log_e("Failed to setup eeprom!");
        return -1;
    }

    /* Initialize working variables */
    m_sm = STATE_0_LOAD;
    m_modified = false;
    m_modified_timestamp = 0;

    /* Return success */
    return 0;
}

/**
 * @brief Read data from the EEPROM memory
 *
 * This function provides a low-level interface to read raw data from the EEPROM.
 *
 * @param address The starting address in EEPROM to read from
 * @param data    Pointer to buffer where read data will be stored
 * @param length  Number of bytes to read from EEPROM
 * @return number of bytes successfully read, or negative error code on failure
 */
int settings_eeprom_read(const size_t address, uint8_t *const data, const size_t length) {
    return m_eeprom.read(address, data, length);
}

/**
 * @brief Write data to the EEPROM memory
 *
 * This function provides a low-level interface to write raw data to the EEPROM.
 *
 * @param address The starting address in EEPROM to write to
 * @param data    Pointer to buffer containing data to write
 * @param length  Number of bytes to write to EEPROM
 * @return number of bytes successfully written, or negative error code on failure
 */
int settings_eeprom_write(const size_t address, const uint8_t *const data, const size_t length) {
    return m_eeprom.write(address, data, length);
}

/**
 * @brief Completely erase all EEPROM contents and reset settings to defaults
 *
 * This function performs a complete wipe of the EEPROM memory by writing 0xFF
 * to all memory locations. It also resets the internal JSON document to an
 * empty state. This operation is irreversible and will permanently delete
 * all stored settings and configuration data.
 *
 * @warning This function takes around 1 second to complete. Call with caution
 * as it won't play nice with code that requires strict timing.
 *
 * @return 0 on successful completion, negative error code on failure
 */
int settings_eeprom_wipe(void) {
    int res;

    /* Set empty document */
    static const char empty[] = "{}";
    DeserializationError unpack_res = deserializeJson(m_doc, (char *)(&empty[0]));
    if (unpack_res != DeserializationError::Ok) {
        log_e("Failed to create empty document (%d)!", unpack_res.code());
        return -1;
    }

    /* Wipe eeprom contents
     * @note This may take around 1 second to complete */
    uint8_t chunk[m_eeprom.size_page_get()];
    memset(chunk, 0xFF, sizeof(chunk));
    for (size_t i = 0; i < m_eeprom.size_total_get(); i += sizeof(chunk)) {
        res = m_eeprom.write(i, chunk, sizeof(chunk));
        if (res < 0) {
            log_e("Failed to clear eeprom page!");
            return -EIO;
        }
    }

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param temperature_c
 * @return
 */
int settings_temperature_target_get(float &temperature_c) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Return if found */
    if (m_doc["temperature"]["value"].is<float>() == true) {
        temperature_c = m_doc["temperature"]["value"];
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param temperature_c
 * @return
 */
int settings_temperature_target_set(const float temperature_c) {

    /* Update json document */
    m_doc["temperature"]["unit"] = "C";
    m_doc["temperature"]["value"] = temperature_c;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param icon
 * @param line1
 * @param line2
 * @return
 */
int settings_user_get(uint8_t *const icon, char *const line1, char *const line2) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Handle icon */
    size_t icon_size = m_doc["user"]["icon"].size();
    if (icon_size != 32) {
        return 0;
    }
    for (size_t i = 0; i < icon_size; i++) {
        icon[i] = m_doc["user"]["icon"][i];
        // log_t("icon[%u] = 0x%02X", i, icon[i]);
    }

    /* Handle name */
    const char *line1_settings = m_doc["user"]["name"][0];
    const char *line2_settings = m_doc["user"]["name"][1];
    if ((strlen(line1_settings) > 12) ||  //
        (strlen(line2_settings) > 12)) {
        return 0;
    }
    strncpy(line1, line1_settings, 13);
    strncpy(line2, line2_settings, 13);
    line1[12] = 0;
    line2[12] = 0;

    /* Return found */
    return 1;
}

/**
 * @brief
 * @param icon
 * @param line1
 * @param line2
 * @return
 */
int settings_user_set(const uint8_t icon[32], const char *line1, const char *line2) {

    /* Update json document */
    for (size_t i = 0; i < 32; i++) {
        m_doc["user"]["icon"][i] = icon[i];
    }
    m_doc["user"]["name"][0] = line1;
    m_doc["user"]["name"][1] = line2;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param product_number
 * @param serial_number
 * @return
 */
int settings_product_get(char *const product_number, char *const serial_number) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Return if found */
    const char *pn = m_doc["product"]["product_number"];
    const char *sn = m_doc["product"]["serial_number"];
    if (((m_doc["product"].containsKey("product_number") != true) || (strlen(pn) > 12)) ||  //
        ((m_doc["product"].containsKey("serial_number") != true) || (strlen(sn) > 12))) {
        return 0;
    }
    strncpy(product_number, pn, 13);
    strncpy(serial_number, sn, 13);
    product_number[12] = 0;
    serial_number[12] = 0;

    /* Return found */
    return 1;
}

/**
 * @brief
 * @param product_number
 * @param serial_number
 * @return
 */
int settings_product_set(const char *const product_number, const char *const serial_number) {

    /* Ensure valid product number and serial number */
    if ((strlen(product_number) > 12) ||  //
        (strlen(serial_number) > 12)) {
        return -EINVAL;
    }

    /* Update json document */
    m_doc["product"]["product_number"] = product_number;
    m_doc["product"]["serial_number"] = serial_number;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param[out] fahrenheit
 * @return
 */
int settings_interface_units_get(bool &fahrenheit) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Return if found */
    if (m_doc["interface"]["units"].is<int>() == true) {
        fahrenheit = (m_doc["interface"]["units"] == 1);
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param[in] fahrenheit
 * @return
 */
int settings_interface_units_set(const bool fahrenheit) {

    /* Update json document */
    m_doc["interface"]["units"] = fahrenheit ? 1 : 0;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param left_handed
 * @return
 */
int settings_interface_rotation_get(bool &left_handed) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Return if found */
    if (m_doc["interface"]["rotation"].is<int>() == true) {
        left_handed = (m_doc["interface"]["rotation"] == 1);
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param left_handed
 * @return
 */
int settings_interface_rotation_set(const bool left_handed) {

    /* Update json document */
    m_doc["interface"]["rotation"] = (left_handed) ? 1 : 0;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param percent
 * @return
 */
int settings_display_brightness_get(int &percent) {
    int res;

    /* Ensure settings are loaded */
    res = m_settings_ensure_loaded();
    if (res <= 0) {
        return -EAGAIN;
    }

    /* Return if found */
    if (m_doc["display"]["brightness"].is<int>() == true) {
        percent = m_doc["display"]["brightness"];
        if (percent < 10) {
            percent = 10;
        } else if (percent > 100) {
            percent = 100;
        }
        return 1;
    }

    /* Return not found */
    return 0;
}

/**
 * @brief
 * @param percent
 * @return
 */
int settings_display_brightness_set(const int percent) {

    /* Ensure valid brightness */
    if ((percent < 10) ||  //
        (percent > 100)) {
        return -EINVAL;
    }

    /* Update json document */
    m_doc["display"]["brightness"] = percent;

    /* Mark settings as modified and record timestamp */
    m_modified = true;
    m_modified_timestamp = millis();

    /* Return success */
    return 0;
}

/**
 * @brief Settings management task - handles loading and saving settings
 *
 * This function implements a state machine that:
 * 1. Loads settings from EEPROM on startup
 * 2. Waits in idle state while settings are being used
 * 3. Saves settings to EEPROM after a delay when they are modified
 *
 * @return 0 on success, negative error code otherwise
 */
int settings_task(void) {
    int res;

    switch (m_sm) {

        case STATE_0_LOAD: {

            /* Ensure eeprom is detected */
            if (m_eeprom.detect() != true) {
                log_e("Failed to detect eeprom!");
                m_sm = STATE_ERROR;
                break;
            }

            /* Load settings from EEPROM */
            res = m_eeprom_unpack();
            if (res < 0) {
                log_e("Failed to load settings from EEPROM!");
                m_sm = STATE_ERROR;
                break;
            }

            /* Move to idle state */
            m_sm = STATE_1_IDLE;
            log_d("Settings loaded successfully");
            break;
        }

        case STATE_1_IDLE: {

            /* Wait for settings to be marked as modified */
            if (m_modified == false) {
                break;
            }

            /* Check if enough time has passed since last modification */
            if ((millis() - m_modified_timestamp) < CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS) {
                break;
            }

            /* Move to save state */
            m_sm = STATE_2_SAVE;
            break;
        }

        case STATE_2_SAVE: {

            /* Commit settings to EEPROM */
            res = m_eeprom_repack();
            if (res < 0) {
                log_e("Failed to commit settings to EEPROM!");
                m_sm = STATE_ERROR;
                break;
            }

            /* Clear modified flag */
            m_modified = false;

            /* Return to idle state */
            m_sm = STATE_1_IDLE;
            log_d("Settings saved to EEPROM");
            break;
        }

        default: {
            log_e("Hurray, we found a cosmic ray!");
            m_sm = STATE_0_LOAD;
            return -1;
        }
    }

    /* Return success */
    return 0;
}
