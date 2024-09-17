#ifndef CONFIG_H
#define CONFIG_H

/* App config */
#define CONFIG_APP_TARGET_MIN 0                //!< Minimum target temperature in degrees celsius
#define CONFIG_APP_TARGET_MAX_SAFE 350         //!< Maximum target temperature in degrees celsius
#define CONFIG_APP_TARGET_MAX_BOOST 400        //!< Maximum target temperature in degrees celsius
#define CONFIG_APP_BOOST_DURATION_LIMIT 30000  //!< Amount of time after which the boost will end, in milliseconds

/* Accelerometer config */
#define CONFIG_ACCEL_SAMPLE_PERIOD 100               //!< In milliseconds
#define CONFIG_ACCEL_IDLE_TIME 180000                //!< In milliseconds
#define CONFIG_ACCEL_IDLE_ANGULAR_SPEED_TRESHOLD 40  //!< In degrees per second.
#define CONFIG_ACCEL_WAKE_ACCELERATION_TRESHOLD 2    //!< In g. Has to be lower than the configured full scale.
#define CONFIG_ACCEL_FALL_ACCELERATION_TRESHOLD 0.3  //!< In g.

/* Buttons config */
#define CONFIG_BUTTONS_PRESS_SHORT_DURATION 30                    //!<
#define CONFIG_BUTTONS_PRESS_LONG_DURATION 500                    //!<
#define CONFIG_BUTTONS_INDIVIDUAL_PRESS_LONG_REPEAT_DURATION 250  //!<
#define CONFIG_BUTTONS_COMBINED_PRESS_LONG_REPEAT_DURATION 500    //!<

/* Dislay config */
#define CONFIG_DISPLAY_WIDTH 96   //!< Horizontal size of the display, in pixels.
#define CONFIG_DISPLAY_HEIGHT 16  //!< Vertical size of the display, in pixels.

/* Orders config */
#define CONFIG_COMMAND_LENGTH_LIMIT 256  //!<

/* User interface config */
#define CONFIG_UI_SPLASH_DURATION 1000  //!< Duration of the splash screen, in milliseconds.
#define CONFIG_UI_INFO_DURATION 1500    //!< Duration of each information screen, in milliseconds.
#define CONFIG_UI_ADJUST_DURATION 1000  //!< Amount of time the target temperature is displayed before reverting to the monitor temperature, in milliseconds.

#endif
