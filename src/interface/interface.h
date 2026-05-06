#ifndef INTERFACE_H
#define INTERFACE_H

/* C/C++ headers */
#include <stdint.h>

/* Config */
#include "../../cfg/config.h"
#define CONFIG_UI_DISPLAY_WIDTH 96                                                                    //!< Horizontal size of the display (in pixels).
#define CONFIG_UI_DISPLAY_HEIGHT 16                                                                   //!< Vertical size of the display (in pixels).
#define CONFIG_UI_DISPLAY_FRAMEBUFFER_BYTES (CONFIG_UI_DISPLAY_WIDTH * CONFIG_UI_DISPLAY_HEIGHT / 8)  //!< Size of the display framebuffer (in bytes).
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
 * @brief Interface icon structure
 *
 * This structure represents an icon that can be displayed on the interface.
 * Icons are generated automatically from PNG files in the res/icons/ directory.
 * But for reference, an online converter can be used at https://javl.github.io/image2cpp/
 *
 * @param width The width of the icon (in pixels).
 * @param height The height of the icon (in pixels).
 * @param data The data of the icon (in bytes).
 */
struct interface_icon {
    const uint8_t width;
    const uint8_t height;
    const uint8_t data[];
};

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

/**
 * @brief Read-only pointer to the live SSD1306 framebuffer
 *
 * On success, @p framebuffer points at @ref CONFIG_UI_DISPLAY_FRAMEBUFFER_BYTES bytes
 * (driver page layout). That pointer stays valid for the program lifetime; the bytes are
 * overwritten whenever the UI redraws (@ref interface_task) — copy first if you need a
 * stable snapshot across further UI updates.
 *
 * @param[out] framebuffer Receives the framebuffer address (caller must pass an lvalue)
 * @return 0 on success (reserved for future error conditions)
 */
int interface_display_capture(const uint8_t *&framebuffer);

#endif
