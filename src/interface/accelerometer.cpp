/* Self header */
#include "accelerometer.h"

/* Project */
#include "../cfg/config.h"
#include "log/log.h"

/* Arduino libraries */
#include <Arduino.h>
#include <RunningMedian.h>
#include <lis2dh12.h>

/* Peripherals */
static lis2dh12 m_accel;

/* Filter library */
static RunningMedian m_magnitude_filter(10);

/* Variables for idle detection */
static uint32_t m_idle_time;
static bool m_idle_detected;

/* Variables for wake detection */
static bool m_wake_detected;

/* Variables for freefall detection */
static bool m_fall_detected;

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_setup(void) {
    int res;

#if R8A

    /* Setup accelerometer */
    res = m_accel.setup(Wire, 0x18);
    if (res < 0) {
        log_e("Failed to detect accelerometer!");
        return -1;
    }

    /* Setup int1 and int2 interrupt lines from accelerometer */
    pinMode(12, INPUT_PULLUP);
    pinMode(13, INPUT_PULLUP);
#endif

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_idle_reset(void) {
    m_idle_time = 0;
    m_idle_detected = false;
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_idle_detected_get(void) {
    return m_idle_detected;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_wake_reset(void) {
    m_wake_detected = false;
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_wake_detected_get(void) {
    return m_wake_detected;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_fall_reset(void) {
    m_fall_detected = false;
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_fall_detected_get(void) {
    return m_fall_detected;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_task(void) {
    int res;

    /* State machine */
    static enum {
        STATE_0,
        STATE_1,
        STATE_ERROR,
    } m_sm;
    switch (m_sm) {

        case STATE_0: {

            /* Ensure the accelerometer is detected */
            if (m_accel.detect() != true) {
                log_e("Failed to detect accelerometer!");
                m_sm = STATE_ERROR;
            }

            /* Configure the accelerometer */
            res = 0;
            res |= m_accel.range_set(LIS2DH12_RANGE_2G);
            res |= m_accel.resolution_set(LIS2DH12_RESOLUTION_10BITS);
            res |= m_accel.axis_enabled_set(true, true, true);
            // res |= m_accel.activity_configure(1100, 5000);
            // res |= m_accel.activity_int2_routed_set(true);
            // res |= m_accel.doubletap_configure(250, 120, 10, 560, true);
            res |= m_accel.sampling_set(LIS2DH12_SAMPLING_10HZ);
            if (res != 0) {
                log_e("Failed to configure accelerometer!");
                m_sm = STATE_ERROR;
            }

            /* Move on */
            m_sm = STATE_1;
            break;
        }

        case STATE_1: {

            /* Periodically */
            static uint32_t m_timestamp;
            if ((millis() - m_timestamp) > CONFIG_ACCEL_SAMPLE_PERIOD) {
                m_timestamp = millis();

                /* Read accelerations */
                float x, y, z;
                res = m_accel.acceleration_read(x, y, z);
                if (res < 0) {
                    log_w("Failed to read acceleration!");
                    break;
                }

                /* Compute magnitude of acceleration */
                float magnitude = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2));
                // log_t("Magnitude = %f", magnitude);

                /* Compute movement as difference between magnitude and median magnitude */
                m_magnitude_filter.add(magnitude);
                float movement = fabs(m_magnitude_filter.getMedian() - magnitude);
                // log_t("magnitude = %f, median = %f, ecart = %f", magnitude, m_magnitude_filter.getMedian(), movement);

                /* Detect idle */
                if (movement >= CONFIG_ACCEL_IDLE_ACCELERATION_TRESHOLD) {
                    m_idle_detected = false;
                    m_idle_time = 0;
                } else {
                    if (m_idle_time >= CONFIG_ACCEL_IDLE_TIME) {
                        m_idle_detected = true;
                    } else {
                        m_idle_time += CONFIG_ACCEL_SAMPLE_PERIOD;
                    }
                }

                /* Detect wake */
                if (movement >= CONFIG_ACCEL_WAKE_ACCELERATION_TRESHOLD) {
                    m_wake_detected = true;
                }

                /* Detect freefall */
                if (magnitude <= CONFIG_ACCEL_FALL_ACCELERATION_TRESHOLD) {
                    m_fall_detected = true;
                    // log_t("Frefall!");
                }
            }

            // /* Detect double tap */
            // static uint32_t m_t2;
            // if (millis() - m_t2 >= 500) {
            //     m_t2 = millis();
            //     // uint8_t reg_int1_src, reg_int2_src;
            //     // m_accel.register_read(LIS2DH12_REGISTER_INT1_SRC, reg_int1_src);
            //     // m_accel.register_read(LIS2DH12_REGISTER_INT2_SRC, reg_int2_src);
            //     // log_t("0x%02X 0x%02X, %d, %d", reg_int1_src, reg_int2_src, digitalRead(12), digitalRead(13));
            //     uint8_t reg_click_src;
            //     m_accel.register_read(LIS2DH12_REGISTER_CLICK_SRC, reg_click_src);
            //     if (reg_click_src & (1 << 5)) {
            //         log_t("0x%02X DOUBLE", reg_click_src);
            //     } else if (reg_click_src & (1 << 4)) {
            //         log_t("0x%02X SINGLE", reg_click_src);
            //     } else {
            //         log_t("0x%02X", reg_click_src);
            //     }
            // }

            // /* Detect inactivity */
            // static uint32_t m_timestamp;
            // if (millis() - m_timestamp >= 1000) {

            //     m_timestamp = millis();
            //     float x, y, z;
            //     m_accel.acceleration_read(x, y, z);
            //     float movement = abs(1 - sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2)));
            // 	if(movement > 0.2
            //     m_magnitude_filter.add(movement);
            //     log_t("Movement = %f, Median = %f", movement, m_magnitude_filter.getMedian());
            // }

            // /* Temp */
            // static uint32_t t;
            // if (millis() - t > 500) {
            //     t = millis();
            //     float x, y, z;
            //     m_accel.acceleration_read(x, y, z);
            //     log_t("%f, %f, %f -> %f", x, y, z, abs(1 - (pow(x, 2) + pow(y, 2) + pow(z, 2))));

            //     /* TODO Detect events ?*/
            //     uint8_t reg_int1_src, reg_int2_src;
            //     m_accel.register_read(LIS2DH12_REGISTER_INT1_SRC, reg_int1_src);
            //     m_accel.register_read(LIS2DH12_REGISTER_INT2_SRC, reg_int2_src);
            //     log_t("0x%02X 0x%02X, %d, %d", reg_int1_src, reg_int2_src, digitalRead(12), digitalRead(13));
            // }
            break;
        }

        case STATE_ERROR: {
            // Do nothing
            break;
        }
    }

    /* Return success */
    return 0;
}
