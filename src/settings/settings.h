#ifndef SETTINGS_H
#define SETTINGS_H

/* Project headers */
#include "errors/errors.h"

/* Config */
#include "../../cfg/config.h"
#ifndef CONFIG_SETTINGS_JSON_DOCUMENT_SIZE
#define CONFIG_SETTINGS_JSON_DOCUMENT_SIZE 4096  //!< Size of the JSON document buffer (in bytes)
#endif
#ifndef CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS
#define CONFIG_SETTINGS_EEPROM_DEFFERED_WRITE_DELAY_MS 2000  //!< Delay before saving settings to EEPROM (in milliseconds)
#endif
#ifndef CONFIG_SETTINGS_EEPROM_HEADER_SIZE
#define CONFIG_SETTINGS_EEPROM_HEADER_SIZE 32  //!< Size of the EEPROM header (in bytes)
#endif
#ifndef CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH
#define CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH 9  //!< Maximum length of the product number eg "SLTO00001" (not including the null terminator)
#endif
#ifndef CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH
#define CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH 3  //!< Maximum length of the product revision eg "R8B" (not including the null terminator)
#endif
#ifndef CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH
#define CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH 7  //!< Maximum length of the serial number eg "2501001" (not including the null terminator)
#endif
#ifndef CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH
#define CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH 12  //!< Maximum length of the username line 1 (not including the null terminator)
#endif
#ifndef CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH
#define CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH 12  //!< Maximum length of the username line 2 (not including the null terminator)
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
int settings_product_get(char *const number, char *const revision, char *const serial);
int settings_product_set(const char *const number, const char *const revision, const char *const serial);
int settings_interface_units_get(bool &fahrenheit);
int settings_interface_units_set(const bool fahrenheit);
int settings_interface_rotation_get(bool &left_handed);
int settings_interface_rotation_set(const bool left_handed);
int settings_display_brightness_get(int &percent);
int settings_display_brightness_set(const int percent);

/* Task */
int settings_task(void);

#endif
