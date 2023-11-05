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
