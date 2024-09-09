/* Self header */
#include "settings.h"

/* Project */
#include "errors/errors.h"
#include "log/log.h"

/* Arduino libraries */
#include <ArduinoJson.h>
#include <m24c64.h>

/* Peripherals */
static m24c64 m_eeprom;

/* */
StaticJsonDocument<1024> m_doc;

static bool m_cached = false;

/**
 * @brief
 * @param
 * @return
 */
static int m_unpack(void) {

    /* Don't do it again if already cached */
    if (m_cached == true) {
        return 0;
    }

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

    /* Flag as cached */
    m_cached = true;

    /* Return success */
    return 0;
}

/**
 *
 */
static int m_repack(void) {

    /* Invalidate cache */
    m_cached = false;

    /* Write to memory */
    m_eeprom.seek_write(0);
    serializeMsgPack(m_doc, m_eeprom);

    /* Return success */
    return 0;
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

    /* Ensure eeprom is detected */
    if (m_eeprom.detect() != true) {
        log_e("Failed to detect eeprom!");
        return -1;
    }

    /* Return success */
    return 0;
}

/**
 * @brief
 * @return
 */
int settings_memory_read(const size_t address, uint8_t *const data, const size_t length) {
    return m_eeprom.read(address, data, length);
}

/**
 * @brief
 * @return
 */
int settings_memory_wipe(void) {
    int res;

    /* Invalidate cache */
    m_cached = false;

    /* Set empty document */
    static const char empty[] = "{}";
    DeserializationError unpack_res = deserializeJson(m_doc, (char *)(&empty[0]));
    if (unpack_res != DeserializationError::Ok) {
        log_e("Failed to create empty document (%d)!", unpack_res.code());
        return -1;
    }

    /* Wipe eeprom contents
     * @todo Prevent wipe while heating is turned on because it takes way too long in the current implementation */
    for (size_t i = 0; i < 8192U; i++) {
        uint8_t clear = 0xFF;
        res = m_eeprom.write(i, &clear, 1);
        if (res < 0) {
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
int settings_temperature_get(float &temperature_c) {
    int res;

    /* Load json document */
    res = m_unpack();
    if (res < 0) {
        return -1;
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
int settings_temperature_set(const float temperature_c) {
    int res;

    /* Update json document */
    m_doc["temperature"]["unit"] = "C";
    m_doc["temperature"]["value"] = temperature_c;

    /* Save it */
    res = m_repack();
    if (res < 0) {
        return -1;
    }

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

    /* Load json document */
    res = m_unpack();
    if (res < 0) {
        return -1;
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
    int res;

    /* Update json document */
    for (size_t i = 0; i < 32; i++) {
        m_doc["user"]["icon"][i] = icon[i];
    }
    m_doc["user"]["name"][0] = line1;
    m_doc["user"]["name"][1] = line2;

    /* Save it */
    res = m_repack();
    if (res < 0) {
        return -1;
    }

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

    /* Load json document */
    res = m_unpack();
    if (res < 0) {
        return -1;
    }

    /* */
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
    int res;

    /* */
    if ((strlen(product_number) > 12) ||  //
        (strlen(serial_number) > 12)) {
        return -EINVAL;
    }

    /* */
    m_doc["product"]["product_number"] = product_number;
    m_doc["product"]["serial_number"] = serial_number;

    /* Save it */
    res = m_repack();
    if (res < 0) {
        return -1;
    }

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

    /* Load json document */
    res = m_unpack();
    if (res < 0) {
        return -1;
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
    int res;

    /* */
    m_doc["interface"]["rotation"] = (left_handed) ? 1 : 0;

    /* Save it */
    res = m_repack();
    if (res < 0) {
        return -1;
    }

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

    /* Load json document */
    res = m_unpack();
    if (res < 0) {
        return -1;
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
    int res;

    /* */
    if ((percent < 10) ||  //
        (percent > 100)) {
        return -EINVAL;
    }

    /* */
    m_doc["display"]["brightness"] = percent;

    /* Save it */
    res = m_repack();
    if (res < 0) {
        return -1;
    }

    /* Return success */
    return 0;
}
