/* Self header */
#include "magnet.h"

/* Arduino libraries */
#include <Arduino.h>

/**
 *
 */
int magnet_setup(void) {
#if R4J
    // drv5032fadmrr
    // Omnipolar Push-pull
    // PA8
    pinMode(PA8, INPUT);
#elif R8A
    // mh253eso
    //  open-drain output
    //  15
    pinMode(15, INPUT_PULLUP);
#else
#endif
    return 0;
}

/**
 *
 */
int magnet_detected_get(void) {
#if R4J
    return (digitalRead(PA8) == HIGH) ? 1 : 0;
#elif R8A
    return (digitalRead(15) == HIGH) ? 1 : 0;
#else
    return 0;
#endif
}
