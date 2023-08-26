/* Self header */
#include "power.h"

/* Project */
#include "log/log.h"

/* Arduino libraries */
#include <fsusb43.h>
#include <fusb302.h>
#include <pi3usb9281c.h>

/* */
static fsusb43 m_fsusb43;
static fusb302 m_fusb302;
static pi3usb9281c m_pi3usb9281;
static int m_qc_dp_h_pin = 18;
static int m_qc_dp_l_pin = 19;
static int m_qc_dn_h_pin = 16;
static int m_qc_dn_l_pin = 17;
static struct {
    bool used;
    struct power_contract contract;
} m_contracts[10];

/**
 * @brief
 * @param
 * @return
 */
int power_setup(void) {
    // Setup pi3
    // Setup fusb302
    // Setup qc
    // Setup mux
    int res;

    /* Setup charger detection ic */
    res = m_pi3usb9281.setup(Wire, 0x25, 7);  // TODO R4
    if (res < 0) {
        log_e("Failed to setup pi3usb9281 ic!");
        return -1;
    }
    if (m_pi3usb9281.detect() != true) {
        log_e("Failed to detect pi3usb9281 ic!");
        return -1;
    }

    /* Setup usb mux */
    m_fsusb43.setup(7);
    m_fsusb43.output_select(FSUSB43_OUTPUT_1);

    /* Setup power delivery phy */
    res = m_fusb302.setup(Wire, 0x22);
    if (res < 0) {
        log_e("Failed to setup fusb302 ic!");
        return -1;
    }
    if (m_fusb302.detect() != true) {
        log_e("Failed to detect fusb302 ic!");
        return -1;
    }

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param contract
 * @return
 */
int power_contract_get(struct power_contract *contract) {
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int power_task(void) {
    int res;

    /* State machine
     * 1) Perform a usb-pd discovery
     * 2) Perform a usb-bc discovery
     * 3) Try hvdcp if possible */
    static uint8_t m_errors_bc;
    static uint8_t m_errors_qc;
    static uint32_t m_timestamp;
    static enum {
        STATE_PD_0,
        STATE_BC_0,
        STATE_BC_1,
        STATE_BC_2,
        STATE_BC_3,
        STATE_BC_4,
        STATE_QC_0,
        STATE_QC_1,
        STATE_QC_2,
        STATE_QC_3,
        STATE_3,
        STATE_ERROR,
    } m_sm;
    switch (m_sm) {

        case STATE_PD_0: {

            /* Skip for now */
            m_sm = STATE_BC_0;
            break;
        }

        case STATE_BC_0: {

            /* Inspired by the chromebook-ec source code, perform a debounce.
             * @see https://git.furworks.de/coreboot-mirror/chrome-ec/src/branch/master/driver/bc12/pi3usb9281.c */
            res = m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_OPEN);
            if (res < 0) {
                log_e("Failed to configure usb switches!");
                m_errors_bc++;
                m_sm = STATE_ERROR;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_1;
            break;
        }

        case STATE_BC_1: {

            /* Wait */
            if (millis() - m_timestamp < 1000) {
                break;
            }

            /* Reset ic */
            res = m_pi3usb9281.reset();
            if (res < 0) {
                log_e("Failed to reset ic!");
                m_errors_bc++;
                m_sm = STATE_ERROR;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_2;
            break;
        }

        case STATE_BC_2: {

            /* Wait */
            if (millis() - m_timestamp < 100) {
                break;
            }

            /* Wait for a device attach event */
            res = m_pi3usb9281.device_attach_wait(1000);
            if (res < 0) {
                log_e("Failed to detect a usb device!");
                m_errors_bc++;
                m_sm = STATE_ERROR;
                break;
            }

            /* Move on */
            m_sm = STATE_BC_3;
            break;
        }

        case STATE_BC_3: {
            enum pi3usb9281c_device_type type;
            res = m_pi3usb9281.device_type_get(&type);
            if (res < 0) {
                log_e("Failed to determine type of usb device attached!");
                m_errors_bc++;
                m_sm = STATE_ERROR;
                break;
            }
            log_t("Type : %d, res %d", (int)type, res);

            /* Try hvcdp */
            if (type == PI3USB9281C_DEVICE_TYPE_USB_DCP) {
                m_sm = STATE_QC_0;
                break;
            }

            /* Move on */
            m_sm = STATE_BC_4;
            break;
        }

        case STATE_BC_4: {
            break;
        }

        case STATE_QC_0: {

            /* Route usb signal to resistor network */
            if (m_fsusb43.output_select(FSUSB43_OUTPUT_2) < 0 ||
                m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_CLOSED) < 0) {
                log_e("Failed to route signals!");
                m_errors_qc++;
                m_sm = STATE_ERROR;
                break;
            }

            /* Move on */
            m_sm = STATE_QC_1;
            break;
        }

        case STATE_QC_1: {
            /* Apply 0.325V-2V to D+ line */
            pinMode(m_qc_dn_h_pin, OUTPUT);
            digitalWrite(m_qc_dn_h_pin, HIGH);
            pinMode(m_qc_dn_l_pin, OUTPUT);
            digitalWrite(m_qc_dn_l_pin, LOW);

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_2;
            break;
        }

        case STATE_QC_2: {

            /* Wait */
            if (millis() - m_timestamp < 100) {
                break;
            }
            break;
        }

        case STATE_ERROR: {
            log_t("error");
            break;
        }

        default: {
            log_e("Hurray, we found a cosmic ray!");
            m_sm = STATE_PD_0;
            return -1;
        }
    }

    /* Return success */
    return 0;
}
