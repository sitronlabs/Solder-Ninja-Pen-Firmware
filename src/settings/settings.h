#ifndef SETTINGS_H
#define SETTINGS_H

/* Project headers */
#include "errors/errors.h"

/* Config */
#include "../../cfg/config.h"
#ifndef CONFIG_SETTINGS_JSON_DOCUMENT_SIZE
#define CONFIG_SETTINGS_JSON_DOCUMENT_SIZE 1024  //!< Size of the JSON document buffer (in bytes)
#endif
#ifndef CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS
#define CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS 2000  //!< Delay before saving settings to EEPROM (in milliseconds)
#endif

/* C/C++ headers */
#include <stddef.h>
#include <stdint.h>

/* Setup */
int settings_setup(void);

/* Direct eeprom access */
int settings_eeprom_read(const size_t address, uint8_t *const data, const size_t length);
int settings_eeprom_write(const size_t address, const uint8_t *const data, const size_t length);
int settings_eeprom_wipe(void);

/* Settings getters and setters */
int settings_temperature_target_get(float &temperature_c);
int settings_temperature_target_set(const float temperature_c);
int settings_user_get(uint8_t *const icon, char *const line1, char *const line2);
int settings_user_set(const uint8_t icon[32], const char *line1, const char *line2);
int settings_product_get(char *const product_number, char *const serial_number);
int settings_product_set(const char *const product_number, const char *const serial_number);
int settings_interface_units_get(bool &fahrenheit);
int settings_interface_units_set(const bool fahrenheit);
int settings_interface_rotation_get(bool &left_handed);
int settings_interface_rotation_set(const bool left_handed);
int settings_display_brightness_get(int &percent);
int settings_display_brightness_set(const int percent);

/* Task */
int settings_task(void);

#endif
