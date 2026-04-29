/* Self header */
#include "accelerometer.h"

/* Project */
#include "../settings/settings.h"
#include "log/log.h"

/* Arduino libraries */
#include <Arduino.h>
#include <lis2dh12.h>

/* Peripherals */
static lis2dh12 m_accel;
static bool m_accel_reconfigure_needed;

/* Variables for idle detection */
static volatile bool m_idle_detected;
static volatile bool m_wake_detected;
static uint32_t m_idle_time_ms;

/* Variables for freefall detection */
static volatile bool m_fall_detected;

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
        log_e("Failed to setup accelerometer!");
        return -1;
    }

    /* Setup interrupt lines from accelerometer
     * Int1 goes low to indicate freefall detected
     * Int2 goes low to indicate inactivity detected */
    pinMode(12, INPUT_PULLUP);
    pinMode(13, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(12), []() { m_fall_detected = true; }, FALLING);
    attachInterrupt(digitalPinToInterrupt(13), []() { if (digitalRead(13) == LOW) { m_idle_detected = true; } else { m_wake_detected = true; } }, CHANGE);
#endif

    /* Retrieve idle time from settings and fallback to default if not found */
    res = settings_accelerometer_idle_time_get(m_idle_time_ms);
    if (res <= 0) {
        m_idle_time_ms = CONFIG_ACCEL_IDLE_TIME_DEFAULT;
    }

    /* Return success */
    return 0;
}

/**
 * @brief Get the current accelerometer idle time setting
 * @return Current idle time in milliseconds (time after which no movement is interpreted as inactivity)
 */
uint32_t accelerometer_idle_time_get(void) {
    return m_idle_time_ms;
}

/**
 * @brief Set the accelerometer idle time and trigger reconfiguration
 *
 * This function updates the idle time value, flags the accelerometer for reconfiguration
 * (which will happen on the next accelerometer_task() call), and persists the new value
 * to settings storage.
 *
 * @param[in] time_ms Idle time in milliseconds (time after which no movement is interpreted as inactivity)
 * @return 0 on success
 */
int accelerometer_idle_time_set(const uint32_t time_ms) {

    /* Ensure valid idle time */
    if ((time_ms < CONFIG_ACCEL_IDLE_TIME_MIN) || (time_ms > CONFIG_ACCEL_IDLE_TIME_MAX)) {
        log_e("Invalid idle time!");
        return -EINVAL;
    }

    /* Update the cached idle time value */
    m_idle_time_ms = time_ms;

    /* Flag accelerometer for reconfiguration (will be handled by accelerometer_task()) */
    m_accel_reconfigure_needed = true;

    /* Persist the new value to settings storage */
    settings_accelerometer_idle_time_set(m_idle_time_ms);

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int accelerometer_idle_reset(void) {
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
                break;
            }

            /* Prepare the registers */
            float reg_act_dur = (((m_idle_time_ms / 1000.0) * 50.0) / 8) - 1;
            if (reg_act_dur > 255) {
                reg_act_dur = 255;
            }
            float reg_act_ths = (1000 * CONFIG_ACCEL_IDLE_ACCELERATION_THRESHOLD) / 16.0;
            if (reg_act_ths < 0) {
                reg_act_ths = 1;
            } else if (reg_act_ths > 255) {
                reg_act_ths = 255;
            }
            float reg_fall_ths = (1000 * CONFIG_ACCEL_FALL_ACCELERATION_THRESHOLD) / 16.0;
            if (reg_fall_ths < 0) {
                reg_fall_ths = 1;
            } else if (reg_fall_ths > 255) {
                reg_fall_ths = 255;
            }

            /* Configure the LIS2DH12 accelerometer:
             *
             * CTRL_REG1 (0x20) = 0b01001111: ODR[3:0]=0100 → 50 Hz; LPen=1 → low-power mode (with HR=0
             * in CTRL_REG4 this selects 8-bit output); Zen/Yen/Xen=1 → all axes enabled.
             *
             * CTRL_REG2 (0x21) = 0b00101010: HPM[1:0]=00 → high-pass normal mode (reference reset via
             * REFERENCE read); HPCF[2:1]=10 → cutoff per Table 34; FDS=1 → filtered data to OUT_* and
             * FIFO; HP_IA2=1 → HP filter on AOI path for INT2; HP_IA1=0 → HP filter off on AOI for INT1.
             *
             * CTRL_REG3 (0x22) = 0b01000000: I1_IA1=1 → route Interrupt 1 (IA1, configured via INT1_*)
             * to the INT1 pin (free-fall signalling on INT1).
             *
             * CTRL_REG4 (0x23) = 0b10000000: BDU=1 → block data update (LSB/MSB pair not split across
             * reads); FS[1:0]=00 → ±2 g full scale; HR=0 → matches low-power path from CTRL_REG1.
             *
             * CTRL_REG5 (0x24) = 0x00: defaults — FIFO off; interrupt latch / 4D options off.
             *
             * CTRL_REG6 (0x25) = 0b00001010: I2_ACT=1 → route sleep-to-wake / return-to-sleep (activity)
             * signalling to INT2; INT_POLARITY=0 → INT1/INT2 active-high (datasheet Table 43).
             *
             * INT1_CFG (0x30) = 0b10010101: AOI=1, 6D=0 → AND combination of threshold events; XLIE,
             * YLIE, ZLIE=1 — interrupt when X, Y and Z are simultaneously below their thresholds (typical
             * free-fall pattern); XHIE/YHIE/ZHIE=0.
             *
             * INT1_THS (0x32): IA1 threshold from reg_fall_ths (1 LSB = 16 mg at ±2 g FS).
             *
             * INT1_DURATION (0x33): minimum IA1 event duration = N/ODR (here N=2 at 50 Hz → 40 ms).
             *
             * ACT_THS (0x3E): sleep-to-wake activation threshold from reg_act_ths (1 LSB = 16 mg @ ±2 g).
             *
             * ACT_DUR (0x3F), driver symbol INACT_DUR: sleep-to-wake / return-to-sleep duration from
             * reg_act_dur (scaling vs ODR per datasheet Table 86). */
            res = 0;
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG1, 0b01001111);
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG2, 0b00101010);
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG3, 0b01000000);
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG4, 0b10000000);
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG5, 0b00000000);
            res |= m_accel.register_write(LIS2DH12_REGISTER_CTRL_REG6, 0b00001010);
            res |= m_accel.register_write(LIS2DH12_REGISTER_INT1_CFG, 0b10010101);
            res |= m_accel.register_write(LIS2DH12_REGISTER_INT1_THS, reg_fall_ths);
            res |= m_accel.register_write(LIS2DH12_REGISTER_INT1_DURATION, 2);
            res |= m_accel.register_write(LIS2DH12_REGISTER_ACT_THS, reg_act_ths);
            res |= m_accel.register_write(LIS2DH12_REGISTER_INACT_DUR, reg_act_dur);
            if (res != 0) {
                log_e("Failed to configure accelerometer!");
                m_sm = STATE_ERROR;
                break;
            }

            /* Perform a dummy read to force the HP filter to the current acceleration value */
            uint8_t reg;
            res |= m_accel.register_read(LIS2DH12_REGISTER_REFERENCE, reg);
            if (res != 0) {
                log_e("Failed to configure accelerometer!");
                m_sm = STATE_ERROR;
                break;
            }

            /* Move on */
            m_sm = STATE_1;
            break;
        }

        case STATE_1: {

            /* Reconfigure the accelerometer if needed */
            if (m_accel_reconfigure_needed == true) {
                m_accel_reconfigure_needed = false;
                m_sm = STATE_0;
                break;
            }

            /* Otherwise, do nothing */
            break;
        }

        case STATE_ERROR: {

            /* Do nothing */
            break;
        }
    }

    /* Return success */
    return 0;
}
