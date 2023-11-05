#ifndef CONFIG_H
#define CONFIG_H

/* App config */
#define CONFIG_APP_TARGET_MIN 0    //!< Minimum target temperature in degrees celsius
#define CONFIG_APP_TARGET_MAX 350  //!< Maximum target temperature in degrees celsius

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

#endif
