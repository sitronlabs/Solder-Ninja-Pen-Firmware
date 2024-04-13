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

/* Work variables for idle detection */
static uint32_t m_idle_time;
static bool m_idle_detected;

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

int accelerometer_task(void) {
    int res;

    /* State machine */
    static enum {
        STATE_0,
        STATE_1,
        STATE_2,
        STATE_3,
        STATE_4,
        STATE_5,
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
            log_t("Before config");
            res = 0;
            res |= m_accel.range_set(LIS2DH12_RANGE_2G);
            res |= m_accel.sampling_set(LIS2DH12_SAMPLING_10HZ);
            res |= m_accel.axis_enabled_set(true, true, true);
            res |= m_accel.activity_configure(1100, 5000);
            res |= m_accel.activity_int2_routed_set(true);
            if (res != 0) {
                log_e("Failed to configure accelerometer!");
                m_sm = STATE_ERROR;
            }
            log_t("After config");

            /* Move on */
            m_sm = STATE_1;
            break;
        }

        case STATE_1: {

            /* Every now and then, compute angle */
            static uint32_t m_timestamp;
            if ((millis() - m_timestamp) > CONFIG_ACCEL_SAMPLE_PERIOD) {
                m_timestamp = millis();
                float x;
                m_accel.acceleration_read(LIS2DH12_AXIS_X, x);
                float angle_deg = asinf(x) * 57.2958;

                /* Compute angluar velocity */
                static float m_angle_previous;
                float angular_rotation = abs(angle_deg - m_angle_previous) / (1000.0 / CONFIG_ACCEL_SAMPLE_PERIOD);
                m_angle_previous = angle_deg;
                log_t("Angular rotation = %f deg/s", angular_rotation);

                /* Apply a thershold to detect inactivity */
                if (angular_rotation >= CONFIG_ACCEL_ACTIVE_ANGULAR_SPEED_TRESHOLD) {
                    m_idle_detected = false;
                    m_idle_time = 0;
                } else {
                    if (m_idle_time >= CONFIG_ACCEL_IDLE_TIME) {
                        m_idle_detected = true;
                    } else {
                        m_idle_time += CONFIG_ACCEL_SAMPLE_PERIOD;
                    }
                }
            }

            // /* Detect inactivity */
            // static uint32_t m_timestamp;
            // if (millis() - m_timestamp >= 1000) {

            //     m_timestamp = millis();
            //     float x, y, z;
            //     m_accel.acceleration_read(x, y, z);
            //     float movement = abs(1 - sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2)));
            // 	if(movement > 0.2
            //     m_movement_filter.add(movement);
            //     log_t("Movement = %f, Median = %f", movement, m_movement_filter.getMedian());
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

        case STATE_2: {
            break;
        }

        case STATE_3: {
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
