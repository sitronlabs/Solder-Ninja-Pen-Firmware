#ifndef CONFIG_H
#define CONFIG_H

/* App config */
#define CONFIG_APP_TARGET_MIN 0                //!< Minimum target temperature (in degrees celsius).
#define CONFIG_APP_TARGET_MAX_SAFE 350         //!< Maximum target temperature (in degrees celsius).
#define CONFIG_APP_TARGET_MAX_BOOST 400        //!< Maximum target temperature in temporary boost (in degrees celsius).
#define CONFIG_APP_BOOST_DURATION_LIMIT 30000  //!< Amount of time after which the boost will end (in milliseconds).

/* Accelerometer config */
#define CONFIG_ACCEL_IDLE_TIME 10000                    //!< Time after which no movement of the device will be interpreted as inactivity (in milliseconds).
#define CONFIG_ACCEL_IDLE_ACCELERATION_THRESHOLD 0.160  //!< Threshold under which acceleration will not be registered as movement (in g, after high pass filter).
#define CONFIG_ACCEL_FALL_ACCELERATION_THRESHOLD 0.336  //!< Threshold under which acceleration will be considered as free-fall (in g, absolute).

/* Buttons config */
#define CONFIG_BUTTONS_PRESS_SHORT_DURATION 30                    //!<
#define CONFIG_BUTTONS_PRESS_LONG_DURATION 500                    //!<
#define CONFIG_BUTTONS_INDIVIDUAL_PRESS_LONG_REPEAT_DURATION 250  //!<
#define CONFIG_BUTTONS_COMBINED_PRESS_LONG_REPEAT_DURATION 500    //!<

/* Dislay config */
#define CONFIG_DISPLAY_WIDTH 96   //!< Horizontal size of the display (in pixels).
#define CONFIG_DISPLAY_HEIGHT 16  //!< Vertical size of the display (in pixels).

/* Orders config */
#define CONFIG_COMMAND_LENGTH_LIMIT 256  //!<

/* Tip config */
#define CONFIG_TIP_CYCLE_TIME_LIMIT 1000  //!< Maximum amount of time that a measuring plus heating cycle should take (in milliseconds).
#define CONFIG_TIP_COEFFICIENT_C 466      //!< Specific heatt capacity of the heating element (in J / (kg * K)).
#define CONFIG_TIP_COEFFICIENT_M 0.002    //!< Mass of the heating element (in kg).
#define CONFIG_TIP_DEBOUNCE_TIME 100      //!< Amount of time to wait after a tip has been inserted (in milliseconds).
#define CONFIG_TIP_READ_PERIOD 35         //!< Period at which the temperature should be read (in milliseconds).
#define CONFIG_TIP_READ_TIMEOUT 2000      //!< Maximum time after which no valid temperature readins will lead to considering the tip is disconnected (in milliseconds).

/* User interface config */
#define CONFIG_UI_SPLASH_DURATION 1500  //!< Duration of the splash screen (in milliseconds).
#define CONFIG_UI_INFO_DURATION 1500    //!< Duration of each information screen (in milliseconds).
#define CONFIG_UI_ADJUST_DURATION 1000  //!< Amount of time the target temperature is displayed before reverting to the measured temperature (in milliseconds).

#endif
