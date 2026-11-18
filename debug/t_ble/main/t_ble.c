#include <assert.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define DEVICE_NAME "DeepStoa-BLE"
#define VALUE_MAX_LEN 128

static const char *TAG = "t_ble";
/* UUIDs: 7b910001/0002-2e56-4a90-8d74-6079ba31c8e0 */
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0xe0, 0xc8, 0x31, 0xba, 0x79, 0x60, 0x74, 0x8d,
    0x90, 0x4a, 0x56, 0x2e, 0x01, 0x00, 0x91, 0x7b);
static const ble_uuid128_t value_uuid = BLE_UUID128_INIT(
    0xe0, 0xc8, 0x31, 0xba, 0x79, 0x60, 0x74, 0x8d,
    0x90, 0x4a, 0x56, 0x2e, 0x02, 0x00, 0x91, 0x7b);
static uint8_t own_addr_type;
/* Accessed only by the NimBLE host task. */
static uint8_t value[VALUE_MAX_LEN] = "Hello from DeepStoa";
static uint16_t value_len = sizeof("Hello from DeepStoa") - 1;

static int value_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "READ connection=%u bytes=%u", conn_handle, value_len);
        return os_mbuf_append(ctxt->om, value, value_len) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > sizeof(value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t incoming[VALUE_MAX_LEN];
        int rc = ble_hs_mbuf_to_flat(ctxt->om, incoming, sizeof(incoming), &len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        memcpy(value, incoming, len);
        value_len = len;
        ESP_LOGI(TAG, "WRITE connection=%u bytes=%u (read back to verify)",
                 conn_handle, len);
        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &value_uuid.u,
                .access_cb = value_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "CONNECTED handle=%u", event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "DISCONNECTED reason=%d", event->disconnect.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete reason=%d", event->adv_complete.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU connection=%u mtu=%u", event->mtu.conn_handle,
                 event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    /* Keep the 128-bit service UUID in advertising; name fits in scan response. */
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Set advertising fields failed rc=%d", rc);
        return;
    }
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)DEVICE_NAME;
    response.name_len = strlen(DEVICE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "Set scan response failed rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 160; /* 100 ms in 0.625 ms units. */
    params.itvl_max = 240;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Start advertising failed rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising as %s; connect using a BLE scanner app", DEVICE_NAME);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE address initialization failed rc=%d", rc);
        return;
    }
    uint8_t addr[6];
    rc = ble_hs_id_copy_addr(own_addr_type, addr, NULL);
    assert(rc == 0);
    ESP_LOGI(TAG, "BLE address %02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset reason=%d; waiting for resync", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting standalone BLE peripheral test");
    /* Do not erase shared NVS automatically if its format is incompatible. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);
    rc = ble_gatts_count_cfg(services);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(services);
    assert(rc == 0);
    nimble_port_freertos_init(host_task);
}
