/* Self header */
#include "power.h"

/* Project */
#include "errors/errors.h"
#include "log/log.h"

/* Arduino libraries */
#include <dac5311.h>
#include <fsusb43.h>
#include <fusb302.h>
#include <pi3usb9281c.h>

/* Config */
#define POWER_OPTIONS_LIMIT 10

/* Peripherals */
static fsusb43 m_fsusb43;
static fusb302 m_fusb302;
static pi3usb9281c m_pi3usb9281;
static dac5311 m_dac;

/* Internal list of power options */
static struct {
    bool assigned;
    struct power_option option;
} m_options[POWER_OPTIONS_LIMIT];

/* Power contract, an index within the power options */
static unsigned int m_contract;

/* Other local variables */
static int m_qc_dn_h_pin = 16;
static int m_qc_dn_l_pin = 17;
static int m_qc_dp_h_pin = 18;
static int m_qc_dp_l_pin = 19;
static int m_qc_dn_m_pin = 29;
static int m_qc_dp_m_pin = 28;

/**
 *
 * @return
 */
static int m_adjust_buck(const float power_limit) {

    /* Compute buck voltage */
    float buck_voltage = sqrt(((power_limit)*0.80) * 2.1);
    log_d("Using buck voltage of %.2fV.", buck_voltage);

    /* Configure dac5311 */
    const float rtop = 590 * 1000;
    const float rbot = 63.4 * 1000;
    const float rdac = 300 * 1000;
    const float vref = 0.7;
    for (uint16_t i = 0; i < 256; i++) {
        float vdac = 3.3 * (i / 255.00);
        float vout = vref * (1 + rtop / rbot) + (vref - vdac) * (rtop / rdac);

        /* Voltage for setting matches desired one */
        if (vout <= buck_voltage) {

            /* Update dac */
            log_d("Using vdac %.2fV for vout %.2fV.", vdac, vout);
            int res = m_dac.output_voltage_set(vdac);
            if (res < 0) {
                log_e("Failed to configure dac!");
                return -1;
            }

            /* Return success */
            return 0;
        }
    }

    /* Return failure */
    return -1;
}

/**
 *
 */
static float m_option_power_max_compute(struct power_option &option) {
    switch (option.type) {
        case POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT:
        case POWER_TYPE_VARIABLE_VOLTAGE_FIXED_CURRENT: {
            return option.voltage_max * option.current_max;
        }
        case POWER_TYPE_VARIABLE_VOLTAGE_FIXED_POWER: {
            return option.power_max;
        }
        default: {
            return 0;
        }
    }
}

/**
 * Add the given option to the list.
 * @return 1 if the new option offers more power than the previous ones, 0 if it doesn't, or a negative error code otherwise.
 */
static int m_options_add(struct power_option &option) {

    /* Find out the maximum amount of power offered by the options already in the list */
    float power_max = 0;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
            }
        }
    }

    /* Add the new option in the list */
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == false) {
            memcpy(&m_options[i].option, &option, sizeof(struct power_option));
            m_options[i].assigned = true;

            /* Return wether or not the new option offers more power */
            if (m_option_power_max_compute(option) > power_max) {
                m_adjust_buck(m_option_power_max_compute(option));  // Temp
                return 1;
            } else {
                m_adjust_buck(power_max);  // Temp
                return 0;
            }
        }
    }

    /* Return no space */
    return -1;
}

/**
 * @brief
 * @param
 * @return
 */
int power_setup(void) {
    int res;

    /* Setup charger detection ic */
    res = m_pi3usb9281.setup(Wire, 0x25, 7);  // TODO R4
    if (res < 0) {
        log_e("Failed to setup pi3usb9281 ic!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup usb mux */
    m_fsusb43.setup(7);
    m_fsusb43.output_select(FSUSB43_OUTPUT_1);

    /* Setup type-c and pd phy */
    res = m_fusb302.setup(Wire, 0x22);
    if (res < 0) {
        log_e("Failed to setup fusb302 ic!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }
    // TODO Enable automatic retransmission
    // TODO Flush RX buffer (is it necessary after reset???)
    // TODO Flush TX (same question)

#if R4J
    /* Setup dac for dc-dc regulation */
    res = m_dac.setup(SPI, 8000000, PA1, 3.3);
    if (res < 0) {
        log_e("Failed to setup dac!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }
#elif R8A
    /* Setup dac for dc-dc regulation */
    res = m_dac.setup(SPI1, 8000000, 20, 3.3);
    if (res < 0) {
        log_e("Failed to setup dac!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }
#endif

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param contract
 * @return
 */
int power_contract_get(struct power_option *contract) {

    /* Find out the maximum amount of power offered by the options already in the list */
    float power_max = 0;
    int index_max = -1;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
                index_max = i;
            }
        }
    }
    if (index_max >= 0) {
        *contract = m_options[index_max].option;
        return 0;
    } else {
        return -1;
    }

    // if (m_options[m_contract].assigned == true) {
    //     *contract = m_options[m_contract].option;
    //     return 0;
    // } else {
    //     return -1;
    // }
}

/**
 * @brief
 * @param power_limit
 * @return
 */
int power_negotiated_power_limit_get(float *const power_limit) {

    /* Find out the maximum amount of power offered by the options already in the list */
    float power_max = 0;
    int index_max = -1;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
                index_max = i;
            }
        }
    }
    if (index_max >= 0) {
        *power_limit = power_max;
        return 0;
    } else {
        *power_limit = 0;
        return -1;
    }
}

/**
 * @brief
 * @param
 * @return
 */
int power_task(void) {
    int res;

    /* State machine
     * 1) Try usb tc
     * 2) Try usb bc
     * 3) Try usb pd
     * 4) Try usb hvdcp if pd didn't give anything */
    static uint8_t m_errors_tc;
    static uint8_t m_errors_bc;
    static uint8_t m_errors_qc;
    static bool m_dcp_detected;
    static uint32_t m_timestamp;
    static enum {
        STATE_IDLE,
        STATE_TC_0,
        STATE_TC_1,
        STATE_TC_2,
        STATE_BC_0,
        STATE_BC_1,
        STATE_BC_2,
        STATE_BC_3,
        STATE_BC_4,
        STATE_BC_5,
        STATE_PD_0,
        STATE_QC_0,
        STATE_QC_1,
        STATE_QC_2,
        STATE_QC_3,
        STATE_QC_4,
        STATE_QC_5,
        STATE_QC_10,
        STATE_DONE,
    } m_sm;
    switch (m_sm) {

        case STATE_IDLE: {

            /* Reset error counters and flags */
            m_errors_tc = 0;
            m_errors_bc = 0;
            m_errors_qc = 0;
            m_dcp_detected = false;

            /* Clear list of options */
            for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
                m_options[i].assigned = false;
            }

            /* Start with Type-C */
            m_sm = STATE_TC_0;
            break;
        }

        case STATE_TC_0: {

            /* Ensure ic is detected */
            if (m_fusb302.detect() != true) {
                log_e("Failed to detect fusb302 ic!");
                m_sm = STATE_TC_2;
                break;
            }

            /* Reset internal registers */
            res = m_fusb302.reset();
            if (res < 0) {
                log_e("Failed to reset fusb302 ic!");
                m_sm = STATE_TC_2;
                break;
            }

            /* Enable power to all internal circuitry */
            res = m_fusb302.power_set(true);
            if (res < 0) {
                log_e("Failed to configure fusb302 ic!");
                m_sm = STATE_TC_2;
                break;
            }

            /* Don't retry too many times */
            if (m_errors_tc > 5) {
                log_e("Too many errors!");
                m_sm = STATE_TC_2;
                break;
            }

            /* Move on */
            m_sm = STATE_TC_1;
            break;
        }

        case STATE_TC_1: {

            /* Measure voltages on the cc pins to determine
             * 1) the orientation of the usb type-c cable
             * 2) the current limit reported by the dfp */
            enum fusb302_cc_voltage cc1, cc2;
            res = m_fusb302.cc_measure(&cc1, &cc2);
            if (res < 0) {
                log_e("Failed to read cc voltages!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Detect orientation */
            fusb302_orientation orientation;
            if (cc1 > FUSB302_CC_VOLTAGE_OPEN && cc2 == FUSB302_CC_VOLTAGE_OPEN) {
                log_d("Type-C orientation is default.");
                orientation = FUSB302_ORIENTATION_0;
            } else if (cc1 == FUSB302_CC_VOLTAGE_OPEN && cc2 > FUSB302_CC_VOLTAGE_OPEN) {
                log_d("Type-C orientation is flipped.");
                orientation = FUSB302_ORIENTATION_1;
            } else {
                log_w("Invalid cc pin logic");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Set orientation */
            res = m_fusb302.orientation_set(orientation);
            if (res < 0) {
                log_e("Failed to set orientation!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Determine current limit based on cc pin voltage */
            float current = 0.0;
            if (cc1 == FUSB302_CC_VOLTAGE_SNK_3_0 || cc2 == FUSB302_CC_VOLTAGE_SNK_3_0) {
                log_i("Usb type-c src advertises 3.0A.");
                current = 3.0;
            } else if (cc1 == FUSB302_CC_VOLTAGE_SNK_1_5 || cc2 == FUSB302_CC_VOLTAGE_SNK_1_5) {
                log_i("Usb type-c src advertises 1.5A.");
                current = 1.5;
            } else if (cc1 == FUSB302_CC_VOLTAGE_SNK_0_5 || cc2 == FUSB302_CC_VOLTAGE_SNK_0_5) {
                log_i("Usb type-c src advertises default current.");
                current = 0.5;
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_TC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0,
                .voltage_max = 5.0,
                .current_max = current,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_TC_2;
            break;
        }

        case STATE_TC_2: {

            /* Move on */
            m_sm = STATE_BC_0;
            break;
        }

        case STATE_BC_0: {

            /* Ensure the ic is detected */
            if (m_pi3usb9281.detect() != true) {
                log_e("Failed to detect pi3usb9281 ic!");
                m_sm = STATE_BC_5;
                break;
            }

            /* Don't retry too many times */
            if (m_errors_bc > 5) {
                log_e("Too many errors!");
                m_sm = STATE_BC_5;
                break;
            }

            /* Move on */
            m_sm = STATE_BC_1;
            break;
        }

        case STATE_BC_1: {

            /* Inspired by the chromebook-ec provider code, perform a debounce.
             * @see https://git.furworks.de/coreboot-mirror/chrome-ec/src/commit/1e800ac838504c0d2950c7aa90cdfe7bde251545/driver/bc12/pi3usb9281.c#L314 */
            res = m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_OPEN);
            if (res < 0) {
                log_e("Failed to configure usb switches!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_2;
            break;
        }

        case STATE_BC_2: {

            /* Leave the switches open for 2 seconds before performing a new detection,
             * less than that seems to not reliabily detect some chargers. */
            if (millis() - m_timestamp < 2000) {
                break;
            }

            /* Reset ic which will automatically perform a new detection */
            res = m_pi3usb9281.reset();
            if (res < 0) {
                log_e("Failed to reset ic!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_3;
            break;
        }

        case STATE_BC_3: {

            /* Wait */
            if (millis() - m_timestamp < 15) {
                break;
            }

            /* Wait for a device attach event */
            res = m_pi3usb9281.device_attach_wait(1000);
            if (res < 0) {
                log_e("Failed to detect a usb device!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Move on */
            m_sm = STATE_BC_4;
            break;
        }

        case STATE_BC_4: {

            /* Retrieve type of device */
            enum pi3usb9281c_device_type type;
            res = m_pi3usb9281.device_type_get(&type);
            if (res < 0) {
                log_e("Failed to determine type of usb device attached!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Determine current limit */
            float current = 0;
            switch (type) {
                case PI3USB9281C_DEVICE_TYPE_USB_CDP:
                case PI3USB9281C_DEVICE_TYPE_USB_DCP: {
                    current = 1.5;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_1A: {
                    current = 1.0;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2A: {
                    current = 2.0;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2_4A: {
                    current = 2.4;
                    break;
                }
                default: {
                    current = 0.5;
                    break;
                }
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_BC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0,
                .voltage_max = 5.0,
                .current_max = current,
            };
            m_options_add(option);

            /* Try hvdcp later if pd doesn't give anything */
            if (type == PI3USB9281C_DEVICE_TYPE_USB_DCP) {
                m_dcp_detected = true;
            }

            /* Move on */
            m_sm = STATE_BC_5;
            break;
        }

        case STATE_BC_5: {

            /* Move on */
            m_sm = STATE_PD_0;
            break;
        }

        case STATE_PD_0: {

            /* Skip for now */
            m_sm = STATE_QC_0;
            break;
        }

        case STATE_QC_0: {

            /* Only look into hvdcp if we found a dcp charger */
            if (m_dcp_detected != true) {
                m_sm = STATE_QC_10;
            }

            /* Don't retry too many times */
            if (m_errors_qc > 5) {
                log_e("Too many errors!");
                m_sm = STATE_QC_10;
                break;
            }

            /* Move on */
            m_sm = STATE_QC_1;
            break;
        }

        case STATE_QC_1: {

            /* Reset all the pins as inputs */
            pinMode(m_qc_dn_h_pin, INPUT);
            pinMode(m_qc_dn_m_pin, INPUT);
            pinMode(m_qc_dn_l_pin, INPUT);
            pinMode(m_qc_dp_h_pin, INPUT);
            pinMode(m_qc_dp_m_pin, INPUT);
            pinMode(m_qc_dp_l_pin, INPUT);

            /* Route usb signal to resistor network */
            if (m_fsusb43.output_select(FSUSB43_OUTPUT_2) < 0 ||
                m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_CLOSED) < 0) {
                log_e("Failed to route signals!");
                m_sm = STATE_QC_0;
                m_errors_qc++;
                break;
            }

            /* Move on */
            m_sm = STATE_QC_2;
            break;
        }

        case STATE_QC_2: {

            /* Advertise that we are a hvdcp compliant sink device
             * by setting 0.325V - 2V to D+ for at least 1.25 seconds */
            pinMode(m_qc_dp_h_pin, OUTPUT);
            pinMode(m_qc_dp_l_pin, OUTPUT);
            digitalWrite(m_qc_dp_h_pin, HIGH);
            digitalWrite(m_qc_dp_l_pin, LOW);

            /* Configure ADC on D- pin */
            analogRead(A3);

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_3;
            break;
        }

        case STATE_QC_3: {

            /* Wait */
            if (millis() - m_timestamp < 1000) {
                break;
            }

            /* Wait for D- pin to be pulled low by the source with a timeout */
            float pin_dn_voltage = (3.3 * analogRead(A3)) / 1023.0;
            log_t("USB DN = %f", pin_dn_voltage);
            if (pin_dn_voltage > 0.2) {
                if (millis() - m_timestamp > 3000) {
                    log_w("HVDCP handshake timed out.");
                    m_sm = STATE_QC_10;
                    break;
                } else {
                    break;
                }
            }
            log_i("HVDCP handshake completed.");

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_4;
            break;
        }

        case STATE_QC_4: {

            /* Wait */
            if (millis() - m_timestamp < 2) {
                break;
            }

            /* Ask for 12V */
            log_d("Asking for 12V");
            pinMode(m_qc_dp_h_pin, OUTPUT);
            pinMode(m_qc_dp_l_pin, OUTPUT);
            digitalWrite(m_qc_dp_h_pin, HIGH);
            digitalWrite(m_qc_dp_l_pin, LOW);
            pinMode(m_qc_dn_h_pin, OUTPUT);
            pinMode(m_qc_dn_l_pin, OUTPUT);
            digitalWrite(m_qc_dn_h_pin, HIGH);
            digitalWrite(m_qc_dn_l_pin, LOW);

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_5;
            break;
        }

        case STATE_QC_5: {

            /* Wait */
            if (millis() - m_timestamp < 60) {
                break;
            }

            /* Confirm vbus is now 12V */
            float vbus = 0;
            m_fusb302.vbus_measure(vbus);
            log_t("VBUS = %f", vbus);
            if (fabs(12.0 - vbus) > 12.0 * 0.1) {
                log_w("HVDCP Invalid voltage");
                m_sm = STATE_QC_10;
                break;
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_QC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 12.0,
                .voltage_max = 12.0,
                .current_max = 1.5,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_QC_10;
            break;
        }

        case STATE_QC_10: {

            /* Move on */
            m_sm = STATE_DONE;
            break;
        }

        case STATE_DONE: {

            /* Do nothing
             * @todo Maybe monitor voltage in case of pd / hvdcp */
            break;
        }

        default: {
            log_e("Hurray, we found a cosmic ray!");
            m_sm = STATE_IDLE;
            return -1;
        }
    }

    /* Return success */
    return 0;
}
