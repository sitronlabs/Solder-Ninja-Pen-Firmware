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
    // TODO Prevent wipe while heating is turned on because it takes way too long in the current implementation
    for (size_t i = 0; i < 8192U; i++) {
        uint8_t clear = 0xFF;
        res = m_eeprom.write(i, &clear, 1);
        if (res < 0) {
            return -EIO;
        }
    }
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
