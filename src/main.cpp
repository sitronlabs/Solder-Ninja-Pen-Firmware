/* Project code */
#include "app/app.h"
#include "interface/interface.h"
#include "log/log.h"

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
    SPI.setSCK(18);
    SPI.setTX(19);
    SPI.setRX(16);
    SPI.setCS(17);
    SPI.begin();
#endif

    /* Setup log */
    res = log_setup();
    if (res < 0) {
    }

    /* Setup application */
    res = app_setup();
    if (res < 0) {
    }

    /* Setup user interface  */
    res = interface_setup();
    if (res < 0) {
    }
}

/*
 *
 */
void loop(void) {

    /* Application task */
    app_task();

    /* User interface task */
    interface_task();
}
