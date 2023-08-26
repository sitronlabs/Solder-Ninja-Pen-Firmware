/* Project */
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
