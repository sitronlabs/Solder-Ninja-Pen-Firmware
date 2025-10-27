#ifndef INTERFACE_H
#define INTERFACE_H

/* Config */
#include "../../cfg/config.h"
#ifndef CONFIG_UI_DISPLAY_WIDTH
#define CONFIG_UI_DISPLAY_WIDTH 96  //!< Horizontal size of the display (in pixels).
#endif
#ifndef CONFIG_UI_DISPLAY_HEIGHT
#define CONFIG_UI_DISPLAY_HEIGHT 16  //!< Vertical size of the display (in pixels).
#endif
#ifndef CONFIG_UI_SPLASH_DURATION
#define CONFIG_UI_SPLASH_DURATION 1500  //!< Duration of the splash screen (in milliseconds).
#endif
#ifndef CONFIG_UI_INFO_DURATION
#define CONFIG_UI_INFO_DURATION 1500  //!< Duration of each information screen (in milliseconds).
#endif
#ifndef CONFIG_UI_ADJUST_DURATION
#define CONFIG_UI_ADJUST_DURATION 1000  //!< Amount of time the target temperature is displayed before reverting to the measured temperature (in milliseconds).
#endif

/**
 * @brief Initialize the user interface system
 *
 * This function sets up all interface components including buttons, magnet sensor,
 * accelerometer, and the OLED display. It configures display settings such as
 * rotation and brightness based on stored preferences.
 *
 * @return 0 on success, negative error code on failure
 */
int interface_setup(void);

/**
 * @brief Main interface task handler
 *
 * This function implements the main user interface state machine that handles
 * all display updates, user input processing, and state transitions. It should
 * be called regularly to poll for user interactions and update the display
 * accordingly.
 *
 * @return 0 on success, negative error code on failure
 */
int interface_task(void);

#endif
