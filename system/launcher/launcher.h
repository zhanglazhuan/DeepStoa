/*
 * DeepStoa — App Launcher (reimplemented from EPOS/Zephyr for 480×800 e-ink)
 *
 * Three parts:
 *   launcher.h           — Public API (open app, close callback, return home)
 *   launcher_home_ui.c   — Home screen with 3-column app icon grid
 *   launcher_gesture.c   — Global swipe gestures (exit app, control panel)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open an app by name (called from home UI grid). */
void launcher_open_app(void *app_name);

/** @brief Callback when an app closes — returns to home UI. */
void launcher_on_app_close(void);

/** @brief Global gesture: return to home, killing current app. */
void launcher_return_home(void);

/** @brief Initialize global swipe gestures (uses global touch_indev). */
void launcher_gesture_init(void);

/** @brief Show the home screen (app icon grid). */
void launcher_home_ui(void);

#ifdef __cplusplus
}
#endif
