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
    float buck_voltage = sqrt(((power_limit) * 0.80) * 2.1);
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
int power_contract_get(struct power_option &contract) {
    float power_max;
    int index_max;

    /* Find out the best contract offered by USB PD first */
    power_max = 0;
    index_max = -1;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true && m_options[i].option.provider == POWER_PROVIDER_USB_PD) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
                index_max = i;
            }
        }
    }
    if (index_max >= 0) {
        contract = m_options[index_max].option;
        return 0;
    }

    /* If no USB PD option exists, find out the best contrat in amongst the other options */
    power_max = 0;
    index_max = -1;
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
        contract = m_options[index_max].option;
        return 0;
    } else {
        return -1;
    }
}

/**
 * @brief
 * @param power_limit
 * @return
 */
int power_negotiated_power_limit_get(float &power_limit) {

    int res;

    /* Find out the maximum amount of power offered by the options */
    struct power_option contract;
    res = power_contract_get(contract);
    if (res < 0) {
        power_limit = 0;
        return -1;
    }

    /* Return success */
    power_limit = m_option_power_max_compute(contract);
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
     * 1) Try usb tc
     * 2) Try usb bc
     * 3) Try usb pd
     * 4) Try usb hvdcp if pd didn't give anything */
    static uint8_t m_errors_tc;
    static uint8_t m_errors_bc;
    static uint8_t m_errors_qc;
    static uint8_t m_errors_pd;
    static bool m_dcp_detected;
    static uint8_t m_pd_next_message_id;
    static float m_pd_voltage;
    static float m_pd_current;
    static uint32_t m_timestamp;
    static enum {
        STATE_IDLE,
        STATE_TC_0,
        STATE_TC_1,
        STATE_TC_2,
        STATE_TC_3,
        STATE_BC_0,
        STATE_BC_1,
        STATE_BC_2,
        STATE_BC_3,
        STATE_BC_4,
        STATE_BC_5,
        STATE_PD_0,
        STATE_PD_1,
        STATE_PD_2,
        STATE_PD_3,
        STATE_PD_4,
        STATE_PD_5,
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
            m_errors_pd = 0;
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
                m_sm = STATE_TC_3;
                break;
            }

            /* Don't retry too many times */
            if (m_errors_tc > 5) {
                log_e("Too many tc errors!");
                m_sm = STATE_TC_3;
                break;
            }

            /* Move on */
            m_sm = STATE_TC_1;
            break;
        }

        case STATE_TC_1: {

            /* Reset internal registers */
            res = m_fusb302.reset();
            if (res < 0) {
                log_e("Failed to reset fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_TC_2;
            break;
        }

        case STATE_TC_2: {

            /* Wait a little bit after reset */
            if ((millis() - m_timestamp) < 15) {
                break;
            }

            /* Enable power to all internal circuitry */
            res = m_fusb302.power_set(true);
            if (res < 0) {
                log_e("Failed to configure fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Ensure pulldown resistors are enabled */
            res = m_fusb302.cc_pull_down();
            if (res < 0) {
                log_e("Failed to configure fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Measure voltages on the cc pins to determine
             * 1) the orientation of the usb type-c cable
             * 2) the current limit reported by the dfp */
            enum usb_typec_cc_status cc1, cc2;
            res = m_fusb302.cc_measure(cc1, cc2);
            if (res < 0) {
                log_e("Failed to read cc voltages!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Detect orientation */
            usb_typec_cc_orientation orientation;
            if (cc1 > USB_TYPEC_CC_STATUS_OPEN && cc2 == USB_TYPEC_CC_STATUS_OPEN) {
                log_d("Type-C orientation is default.");
                orientation = USB_TYPEC_CC_ORIENTATION_NORMAL;
            } else if (cc1 == USB_TYPEC_CC_STATUS_OPEN && cc2 > USB_TYPEC_CC_STATUS_OPEN) {
                log_d("Type-C orientation is flipped.");
                orientation = USB_TYPEC_CC_ORIENTATION_REVERSE;
            } else {
                log_w("Invalid cc pin logic");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Set orientation */
            res = m_fusb302.cc_orientation_set(orientation);
            if (res < 0) {
                log_e("Failed to set orientation!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Determine current limit based on cc pin voltage */
            float current = 0.0;
            if (cc1 == USB_TYPEC_CC_STATUS_RP_3_0 || cc2 == USB_TYPEC_CC_STATUS_RP_3_0) {
                log_i("Type-C src advertises 3.0A.");
                current = 3.0;
            } else if (cc1 == USB_TYPEC_CC_STATUS_RP_1_5 || cc2 == USB_TYPEC_CC_STATUS_RP_1_5) {
                log_i("Type-C src advertises 1.5A.");
                current = 1.5;
            } else if (cc1 == USB_TYPEC_CC_STATUS_RP_DEF || cc2 == USB_TYPEC_CC_STATUS_RP_DEF) {
                log_i("Type-C src advertises default current.");
                current = 0.5;
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_TC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0f,
                .voltage_max = 5.0f,
                .current_max = current,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_TC_3;
            break;
        }

        case STATE_TC_3: {

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
                log_e("Too many bc errors!");
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
                log_w("Failed to detect a usb device, trying again...");
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
                log_w("Failed to determine type of usb device attached, trying again...");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Determine current limit */
            float current = 0;
            switch (type) {
                case PI3USB9281C_DEVICE_TYPE_USB_CDP: {
                    log_i("Detected usb device of type cdp.");
                    current = 1.5f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_USB_DCP: {
                    log_i("Detected usb device of type dcp.");
                    current = 1.5f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_1A: {
                    log_i("Detected usb device of type 1A charger.");
                    current = 1.0f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2A: {
                    log_i("Detected usb device of type 2A charger.");
                    current = 2.0f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2_4A: {
                    log_i("Detected usb device of type 2.4A charger.");
                    current = 2.4f;
                    break;
                }
                default: {
                    log_i("Detected usb device of type sdp.");
                    current = 0.5f;
                    break;
                }
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_BC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0f,
                .voltage_max = 5.0f,
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

            /* Don't retry too many times */
            if (m_errors_pd > 5) {
                log_e("Too many pd errors!");
                m_sm = STATE_QC_0;
                break;
            }

            /* Move on */
            m_sm = STATE_PD_1;
            break;
        }

        case STATE_PD_1: {

            /* Enable automatic retransmission */
            res = m_fusb302.pd_autoretry_set(3);
            if (res < 0) {
                log_e("Failed to enable fusb302 auto retry!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Enable automatic goodcrc
             * @note Starting from here, ensure the firmware doesn't stall the processing of pd messages that needs to happen in roughly 10ms */
            log_t("autogoodcrc_enable");
            res = m_fusb302.pd_autogoodcrc_set(true);
            if (res < 0) {
                log_e("Failed to enable fusb302 auto goodcrc!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Flush TX fifo */
            res = m_fusb302.pd_tx_flush();
            if (res < 0) {
                log_e("Failed to flush fusb302 tx fifo!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Flush RX fifo */
            res = m_fusb302.pd_rx_flush();
            if (res < 0) {
                log_e("Failed to flush fusb302 rx fifo!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Reset PD logic */
            res = m_fusb302.pd_reset();
            if (res < 0) {
                log_e("Failed to reset fusb302 pd logic!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            // /* Try to receive power delivery source capabilities */
            // static struct usb_pd_pdo pdo[7];
            // res = usb_pd_source_capabilities_list(0, &pdo[0], sizeof(pdo) / sizeof(struct usb_pd_pdo));
            // if (res < 0) {
            //     log_e("Failed to retrieve power delivery source capabilities!");
            //     m_sm = STATE_ERROR;
            //     break;
            // }

            // TODO
            // TODO Set data role and power role

            // TODO Maybe clear m_pd_xxx variables
            // TODO Maybe clear pd related power options

            /* Move on */
            m_sm = STATE_PD_2;
            break;
        }

        case STATE_PD_2: {

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 0) {
                break;
            }

            /* Log */
            log_i("Received usb pd message: address=0x%04X, header=0x%04X, object_count=%d", response.address, response.header, response.object_count);
            for (unsigned int i = 0; i < response.object_count; i++) {
                log_i(" - Object %u=0x%08X", i, response.objects[i]);
            }

            /* Wait for source capabilities message */
            if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_DATA_SOURCE_CAPABILITIES) {

                /* Parse each pdo */
                double power_best = 0;
                int8_t power_best_index = -1;
                for (uint8_t i = 0; i < response.object_count; i++) {
                    switch (response.objects[i] >> 30U) {

                        case USB_PD_PDO_TYPE_FIXED: {
                            double voltage = ((response.objects[i] & 0x000FFC00) >> 10) * 0.05;
                            double current = ((response.objects[i] & 0x000001FF) >> 0) * 0.01;
                            log_d("Received fixed pdo %fV %fA", voltage, current);
                            double power = voltage * current;
                            if (power >= power_best) {
                                power_best = power;
                                power_best_index = i;
                                m_pd_current = current;
                                m_pd_voltage = voltage;
                            }
                            break;
                        }

                        default: {
                            log_w("Received unsupported pdo.");
                            break;
                        }
                    }
                }

                /* Request the most interesting pdo */
                if (power_best_index >= 0) {

                    /* Build message */
                    struct usb_pd_message request = {0};
                    request.address = 0;  // ?
                    request.header = 0;
                    request.header |= (m_pd_next_message_id & 0b111) << 9;
                    request.header |= (USB_PD_PROTOCOL_REVISION_2_0 << 6);
                    request.header |= USB_PD_MESSAGE_TYPE_DATA_REQUEST;
                    uint16_t current_10ma = m_pd_current * 100;
                    request.object_count = 1;
                    request.objects[0] = 0;
                    request.objects[0] |= ((power_best_index + 1) << 28);
                    request.objects[0] |= (0 << 27);  // No GiveBack support for now
                    request.objects[0] |= (1 << 25);  // USB Communications Capable
                    request.objects[0] |= (1 << 24);  // No USB Suspend
                    request.objects[0] |= (current_10ma << 10);
                    request.objects[0] |= (current_10ma << 0);

                    /* Log */
                    log_d("Requesting pdo at index %u", power_best_index);

                    /* Send message */
                    res = m_fusb302.pd_message_send(request);
                    if (res < 0) {
                        log_e("Failed to request pdo (%d)!", res);
                        m_sm = STATE_PD_0;
                        m_errors_pd++;
                        break;
                    }

                    /* Increment message id
                     * @todo Ideally wait for goodcrc */
                    m_pd_next_message_id = (m_pd_next_message_id + 1) & 0b111;

                    /* Move on */
                    m_timestamp = millis();
                    m_sm = STATE_PD_3;
                    break;
                }
            }

            /* Handle other messages */
            else {
                log_w("Unexpected message (0).");
            }

            /* Otherwise stay in this state
             * @note This is not ideal
             * @todo Move to a separate state machine maybe or implement timeout? */
            break;
        }

        case STATE_PD_3: {

            /* Watch for timeout
             * @see Section 6.6.2
             * @see tReceiverResponse = 15 ms
             * @todo Revert to 15ms? */
            if ((millis() - m_timestamp) >= 115) {
                log_e("No response received!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 0) {
                break;
            }

            /* Log */
            log_i("Received usb pd message: address=0x%04X, header=0x%04X, object_count=%d", response.address, response.header, response.object_count);
            for (unsigned int i = 0; i < response.object_count; i++) {
                log_i(" - Object %u=0x%08X", i, response.objects[i]);
            }

            /* Handle good crc */
            if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_GOODCRC) {
                log_i("Good crc.");
            }

            /* Handle accept message */
            else if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_ACCEPT) {
                log_i("Pdo request accepted.");

                /* Move on */
                m_timestamp = millis();
                m_sm = STATE_PD_4;
                break;
            }

            /* Handle reject message */
            else if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_REJECT) {
                log_e("Pdo request rejected.");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Handle other messages */
            else {
                log_w("Unexpected message (1).");
            }

            /* Otherwise stay in this state */
            break;
        }

        case STATE_PD_4: {

            /* Watch for timeout
             * @see Section 6.6.5.1
             * @see tPSTransition = 450 to 550 ms */
            if ((millis() - m_timestamp) >= 650) {
                log_e("No response received.");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 0) {
                break;
            }

            /* Log */
            log_i("Received usb pd message: address=0x%04X, header=0x%04X, object_count=%d", response.address, response.header, response.object_count);
            for (unsigned int i = 0; i < response.object_count; i++) {
                log_i(" - Object %u=0x%08X", i, response.objects[i]);
            }

            /* Handle ready message */
            if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_PS_RDY) {

                /* Log */
                log_d("Pd supply ready, delivering %.2fV %.2fA", m_pd_voltage, m_pd_current);

                /* Add power option */
                struct power_option option = {
                    .provider = POWER_PROVIDER_USB_PD,
                    .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                    .voltage_min = m_pd_voltage,
                    .voltage_max = m_pd_voltage,
                    .current_max = m_pd_current,
                };
                m_options_add(option);

                /* Move on */
                m_sm = STATE_PD_5;
                break;
            }

            /* Handle other messages */
            else {
                log_w("Unexpected message (2).");
            }

            /* Otherwise stay in this state */
            break;
        }

        case STATE_PD_5: {

            /* Move on */
            m_sm = STATE_DONE;
            break;
        }

        case STATE_QC_0: {

            /* Only look into hvdcp if we found a dcp charger */
            if (m_dcp_detected != true) {
                m_sm = STATE_QC_10;
            }

            /* Don't retry too many times */
            if (m_errors_qc > 5) {
                log_e("Too many qc errors!");
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
            /* @todo Maybe monitor voltage in case of pd / hvdcp */

            /* Look for incoming messages
             * Otherwise, the fusb302 will stop sending goodcrc */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 0) {
                break;
            }

            /* Log */
            log_i("Received usb pd message: address=0x%04X, header=0x%04X, object_count=%d", response.address, response.header, response.object_count);
            for (unsigned int i = 0; i < response.object_count; i++) {
                log_i(" - Object %u=0x%08X", i, response.objects[i]);
            }

            /* Wait for source capabilities message */
            if ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_DATA_SOURCE_CAPABILITIES) {

                /* Parse each pdo */
                double power_best = 0;
                int8_t power_best_index = -1;
                for (uint8_t i = 0; i < response.object_count; i++) {
                    switch (response.objects[i] >> 30U) {

                        case USB_PD_PDO_TYPE_FIXED: {
                            double voltage = ((response.objects[i] & 0x000FFC00) >> 10) * 0.05;
                            double current = ((response.objects[i] & 0x000001FF) >> 0) * 0.01;
                            log_d("Received fixed pdo %fV %fA", voltage, current);
                            double power = voltage * current;
                            if (power >= power_best) {
                                power_best = power;
                                power_best_index = i;
                                m_pd_current = current;
                                m_pd_voltage = voltage;
                            }
                            break;
                        }

                        default: {
                            log_w("Received unsupported pdo.");
                            break;
                        }
                    }
                }

                /* Request the most interesting pdo */
                if (power_best_index >= 0) {

                    /* Build message */
                    struct usb_pd_message request = {0};
                    request.address = 0;  // ?
                    request.header = 0;
                    request.header |= (m_pd_next_message_id & 0b111) << 9;
                    request.header |= (USB_PD_PROTOCOL_REVISION_2_0 << 6);
                    request.header |= USB_PD_MESSAGE_TYPE_DATA_REQUEST;
                    uint16_t current_10ma = m_pd_current * 100;
                    request.object_count = 1;
                    request.objects[0] = 0;
                    request.objects[0] |= ((power_best_index + 1) << 28);
                    request.objects[0] |= (0 << 27);  // No GiveBack support for now
                    request.objects[0] |= (1 << 25);  // USB Communications Capable
                    request.objects[0] |= (1 << 24);  // No USB Suspend
                    request.objects[0] |= (current_10ma << 10);
                    request.objects[0] |= (current_10ma << 0);

                    /* Log */
                    log_d("Requesting pdo at index %u", power_best_index);

                    /* Send message */
                    res = m_fusb302.pd_message_send(request);
                    if (res < 0) {
                        log_e("Failed to request pdo (%d)!", res);
                        m_sm = STATE_PD_0;
                        m_errors_pd++;
                        break;
                    }

                    /* Increment message id
                     * @todo Ideally wait for goodcrc */
                    m_pd_next_message_id = (m_pd_next_message_id + 1) & 0b111;

                    /* Move on */
                    m_timestamp = millis();
                    m_sm = STATE_PD_3;
                    break;
                }
            }

            /* Handle other messages */
            else {
                log_w("Unexpected message (3).");
            }

            /* Stay here */
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
