#ifndef CONFIG_H
#define CONFIG_H

/* Controller config */
#define CONFIG_CONTROLLER_TARGET_MIN 0                //!< Minimum target temperature (in degrees celsius).
#define CONFIG_CONTROLLER_TARGET_MAX_SAFE 350         //!< Maximum target temperature (in degrees celsius).
#define CONFIG_CONTROLLER_TARGET_MAX_BOOST 400        //!< Maximum target temperature in temporary boost (in degrees celsius).
#define CONFIG_CONTROLLER_BOOST_DURATION_LIMIT 30000  //!< Amount of time after which the boost will end (in milliseconds).

/* Buttons config */
#define CONFIG_BUTTONS_PRESS_SHORT_DURATION 30                    //!<
#define CONFIG_BUTTONS_PRESS_LONG_DURATION 500                    //!<
#define CONFIG_BUTTONS_INDIVIDUAL_PRESS_LONG_REPEAT_DURATION 250  //!<
#define CONFIG_BUTTONS_COMBINED_PRESS_LONG_REPEAT_DURATION 500    //!<

/* Tip config */
#define CONFIG_TIP_CYCLE_TIME_LIMIT 1000  //!< Maximum amount of time that a measuring plus heating cycle should take (in milliseconds).
#define CONFIG_TIP_COEFFICIENT_C 466      //!< Specific heatt capacity of the heating element (in J / (kg * K)).
#define CONFIG_TIP_COEFFICIENT_M 0.002    //!< Mass of the heating element (in kg).
#define CONFIG_TIP_DEBOUNCE_TIME 200      //!< Debounce and settling time after tip insertion before measurement (in milliseconds).
#define CONFIG_TIP_READ_PERIOD 35         //!< Period at which the temperature should be read (in milliseconds).
#define CONFIG_TIP_READ_TIMEOUT 2000      //!< Maximum time after which no valid temperature readins will lead to considering the tip is disconnected (in milliseconds).
#define CONFIG_TIP_SOFT_START_DURATION 100  //!< Default duration of the soft-start current ramp (in milliseconds).
#define CONFIG_TIP_CURRENT_LIMIT_FACTOR 0.9f //!< Default factor applied to negotiated USB current limits.
#define CONFIG_TIP_SHUNT_MILLIOHMS 10.0f  //!< Actual shunt resistor value used on the INA219 current-sense path (10 mΩ).
#define CONFIG_TIP_BUCK_EFFICIENCY 0.80f  //!< Assumed DC-DC converter efficiency.
#define CONFIG_TIP_RESISTANCE_DEFAULT_OHMS 2.1f  //!< Default tip resistance used when measurement is unavailable or invalid.
#define CONFIG_TIP_RESISTANCE_ALTERNATIVE_OHMS 1.6f  //!< Alternative nominal tip resistance selected by the resistance measurement.
#define CONFIG_TIP_RESISTANCE_CLASSIFICATION_THRESHOLD_OHMS 1.85f  //!< Resistance boundary used to distinguish 1.6 Ω and 2.1 Ω tips.
#define CONFIG_TIP_RESISTANCE_MEASUREMENT_MIN_VALID_OHMS 1.0f  //!< Minimum valid measured tip resistance (ohms).
#define CONFIG_TIP_RESISTANCE_MEASUREMENT_MAX_VALID_OHMS 3.0f  //!< Maximum valid measured tip resistance (ohms).
#define CONFIG_TIP_RESISTANCE_OFFSET_DEFAULT_OHMS 0.0f  //!< Default user-configurable resistance offset (ohms).
#define CONFIG_TIP_RESISTANCE_OFFSET_INVALID_OHMS -999.0f  //!< Sentinel for an unset or invalid stored resistance offset.
#define CONFIG_TIP_RESISTANCE_OFFSET_MIN_TENTHS -15  //!< Minimum resistance offset in tenths of an ohm.
#define CONFIG_TIP_RESISTANCE_OFFSET_MAX_TENTHS 15  //!< Maximum resistance offset in tenths of an ohm.
#define CONFIG_TIP_RESISTANCE_MEASUREMENT_SETTLE_TIME 25  //!< Time to wait for current to stabilize before sampling (milliseconds).
#define CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_PERIOD 5  //!< Interval between resistance probe samples (milliseconds).
#define CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_COUNT 8  //!< Number of samples used to average the resistance probe.
#define CONFIG_TIP_RESISTANCE_MINIMUM_USB_VOLTAGE_V 5.0f  //!< Minimum negotiated USB voltage required for tip resistance measurement (volts).
#define CONFIG_TIP_RESISTANCE_MINIMUM_USB_CURRENT_A 1.0f  //!< Minimum negotiated USB current required for tip resistance measurement (amperes).
#define CONFIG_TIP_RESISTANCE_POWER_WAIT_TIMEOUT 2000  //!< Maximum time to wait for a USB power contract of at least 5 V and 1 A before using the default tip resistance (milliseconds).

/* User interface config */
#define CONFIG_UI_SPLASH_DURATION 1500  //!< Duration of the splash screen (in milliseconds).
#define CONFIG_UI_INFO_DURATION 1500    //!< Duration of each information screen (in milliseconds).
#define CONFIG_UI_ADJUST_DURATION 1000  //!< Amount of time the target temperature is displayed before reverting to the measured temperature (in milliseconds).
#define CONFIG_UI_HEATING_CURRENT_AVERAGE_SAMPLE_COUNT 10  //!< Number of INA219 samples in the heating-screen current average.
#define CONFIG_UI_HEATING_CURRENT_AVERAGE_SAMPLE_PERIOD_MS 100  //!< Minimum interval between samples in the heating-screen current average (milliseconds).

#endif
