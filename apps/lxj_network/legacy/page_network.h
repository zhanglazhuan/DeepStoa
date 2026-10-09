#ifndef PAGE_NETWORK_H
#define PAGE_NETWORK_H

#include "esp_err.h"

extern "C" bool wifi_is_connected(void);

esp_err_t safe_wifi_stop();
esp_err_t safe_wifi_deinit();
void save_wifi_enable_to_nvs(bool enable);
bool load_wifi_enable_from_nvs();
void wifi_set_enable(bool enable);
void page_network_show(void);   // Display the network information page
void page_network_config(void); // Enter the Web distribution network process
void page_network_init(void);   // initialize
void page_network_init_main(void);
void page_handle_network_key_event();

int page_network_init_mode(void);

#ifdef __cplusplus
#include <functional>

using page_network_event_cb_t = std::function<void(void)>;
void page_network_set_callbacks(page_network_event_cb_t on_connected,
                                page_network_event_cb_t on_disconnected);
#endif


#endif // PAGE_NETWORK_H
