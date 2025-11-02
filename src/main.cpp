/* Project */
#include "../gen/version.h"
#include "com/com.h"
#include "controller/controller.h"
#include "interface/interface.h"
#include "log/log.h"
#include "settings/settings.h"
#include "watchdog/watchdog.h"

/* Arduino libraries */
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

/*
 *
 */
void setup(void) {
    int res;

    /* Setup i2c */
#if R8A
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.setClock(400000);
    Wire.begin();
#endif

    /* Setup spi */
#if R8A
    SPI1.setSCK(26);
    SPI1.setTX(27);
    SPI1.setRX(24);
    SPI1.begin();
    pinMode(20, OUTPUT);
    pinMode(25, OUTPUT);
    digitalWrite(20, HIGH);
    digitalWrite(25, HIGH);
#endif

    /* Setup log */
    res = log_setup();
    if (res < 0) {
        log_e("Failed to setup log task!");
    }
    log_i("Hello world.");

    /* Setup settings */
    res = settings_setup();
    if (res < 0) {
        log_e("Failed to setup settings!");
    }

    /* Setup controller */
    res = controller_setup();
    if (res < 0) {
        log_e("Failed to setup controller task!");
    }

    /* Setup user interface  */
    res = interface_setup();
    if (res < 0) {
        log_e("Failed to setup interface task!");
    }

    /* Setup communication */
    res = com_setup();
    if (res < 0) {
        log_e("Failed to setup communication task!");
    }

    /* Setup watchdog */
    res = watchdog_setup();
    if (res < 0) {
        log_e("Failed to setup watchdog task!");
    }

    /* Log some information */
    log_i("Firmware version: %s", k_version_string);
    char serial_number[CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH + 1] = {0};
    res = settings_product_get(NULL, NULL, serial_number);
    if (res < 0) {
        log_i("Serial number: Not available");
    } else {
        log_i("Serial number: %s", serial_number);
    }
#if R8A
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    log_i("Board UID: %02X%02X%02X%02X%02X%02X%02X%02X", id.id[0], id.id[1], id.id[2], id.id[3], id.id[4], id.id[5], id.id[6], id.id[7]);
#endif

    /* That's it */
    log_i("Setup done.");
}

/*
 *
 */
void loop(void) {

    /* Controller task */
    controller_task();

    /* User interface task */
    interface_task();

    /* Communication task */
    com_task();

    /* Settings task */
    settings_task();

    /* Log task */
    log_task();

    /* Watchdog task */
    watchdog_task();
}
