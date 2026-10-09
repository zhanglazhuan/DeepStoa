#ifndef LXJ_APP_CONTRACT_H
#define LXJ_APP_CONTRACT_H

/*
 * Lifecycle contract for applications migrated from the LXJ reference tree.
 *
 * This header intentionally contains no dependency on LVGL or any driver.
 * Each concrete app adapts this contract to application_t in app_manager.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    void (*init)(void);
    void (*open)(void);
    void (*close)(void);
} lxj_app_contract_t;

#ifdef __cplusplus
}
#endif

#endif /* LXJ_APP_CONTRACT_H */
