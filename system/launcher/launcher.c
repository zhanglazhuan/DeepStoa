/*
 * DeepStoa — App Launcher Implementation
 *
 * Reimplemented from EPOS/Zephyr (epos_launcher.c) for 480×800 e-ink.
 *
 * Manages app lifecycle: open app, close callback, return home.
 *
 * Dependencies:
 *   - app_manager.h
 *   - lvgl.h
 *   - esp_log.h
 */

#include "esp_log.h"
#include "lvgl.h"
#include "app_manager.h"
#include "launcher.h"

static const char *TAG = "launcher";

/* ── Async helper: release input after app close ──────────────────────── */

static void async_release_input(void *unused)
{
    (void)unused;
    /* Placeholder — re-enable launcher-level input state after app exit.
     * In EPOS this calls async_turn_off_buttons_allocation(). */
}

/* ── App lifecycle ────────────────────────────────────────────────────── */

void launcher_open_app(void *app_name)
{
    ESP_LOGI(TAG, "open_app: %s", app_name ? (const char *)app_name : "(null)");

    app_manager_show(launcher_on_app_close, lv_screen_active(), NULL,
                     (const char *)app_name);
}

void launcher_on_app_close(void)
{
    ESP_LOGI(TAG, "App closed, returning to home");

    app_manager_delete();
    lv_async_call(async_release_input, NULL);
    launcher_home_ui();
}

void launcher_return_home(void)
{
    ESP_LOGI(TAG, "Global gesture: returning to home UI");

    /* Kill current app */
    app_manager_delete();

    /* Release any app-level input hooks */
    lv_async_call(async_release_input, NULL);

    /* Reload home screen */
    launcher_home_ui();
}
