// PhotoPaint V2 assembly workaround: only ES7210 has SDA/SCL crossed.
// Wrap all blocking IDF master operations so pin routing and each complete
// transaction are atomic across the BSP, codecs and third-party drivers.
#include "driver/i2c_master.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/i2c_periph.h"

static SemaphoreHandle_t mutex;
static portMUX_TYPE init_lock = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_bus_handle_t shared_bus;
static i2c_master_dev_handle_t swapped_devices[16];
static bool swapped;

static bool lock_bus(void) {
    if (!mutex) {
        SemaphoreHandle_t candidate = xSemaphoreCreateRecursiveMutex();
        if (!candidate) return false;
        portENTER_CRITICAL(&init_lock);
        if (!mutex) { mutex = candidate; candidate = NULL; }
        portEXIT_CRITICAL(&init_lock);
        if (candidate) vSemaphoreDelete(candidate);
    }
    return xSemaphoreTakeRecursive(mutex, portMAX_DELAY) == pdTRUE;
}
static void unlock_bus(void) { xSemaphoreGiveRecursive(mutex); }
static bool is_swapped_device(i2c_master_dev_handle_t dev) {
    if (!dev) return false;
    for (unsigned i=0;i<16;i++) if (swapped_devices[i] == dev) return true;
    return false;
}
static void route(bool reverse) {
    if (!shared_bus || swapped == reverse) return;
    // Both pads are already open-drain with input and pull-up enabled by IDF.
    // Transactions finish before switching; do not recreate buses/device handles.
    int sda = reverse ? 18 : 17, scl = reverse ? 17 : 18;
    const i2c_signal_conn_t *sig = &i2c_periph_signal[0];
    esp_rom_gpio_connect_out_signal(sda, sig->sda_out_sig, false, false);
    esp_rom_gpio_connect_out_signal(scl, sig->scl_out_sig, false, false);
    esp_rom_gpio_connect_in_signal(sda, sig->sda_in_sig, false);
    esp_rom_gpio_connect_in_signal(scl, sig->scl_in_sig, false);
    esp_rom_delay_us(5);
    swapped = reverse;
}

esp_err_t __real_i2c_new_master_bus(const i2c_master_bus_config_t *, i2c_master_bus_handle_t *);
esp_err_t __wrap_i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *out) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    bool target = cfg && cfg->i2c_port == 0 && cfg->sda_io_num == 17 && cfg->scl_io_num == 18;
    if (target && (shared_bus || cfg->trans_queue_depth != 0)) {
        unlock_bus(); return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t r = __real_i2c_new_master_bus(cfg, out);
    if (r == ESP_OK && target) {
        shared_bus = *out; swapped = false;
        ESP_LOGW("I2C_PIN_SWAP", "ES7210 0x40 uses SDA=18 SCL=17; all blocking I2C transfers serialized");
    }
    unlock_bus(); return r;
}
esp_err_t __real_i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *, i2c_master_dev_handle_t *);
esp_err_t __real_i2c_master_bus_rm_device(i2c_master_dev_handle_t);
esp_err_t __wrap_i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *out) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    esp_err_t r = __real_i2c_master_bus_add_device(bus,cfg,out);
    if (r == ESP_OK && bus == shared_bus && cfg->dev_addr_length == I2C_ADDR_BIT_LEN_7 && cfg->device_address == 0x40) {
        unsigned slot=0; while(slot<16 && swapped_devices[slot])slot++;
        if(slot==16) { __real_i2c_master_bus_rm_device(*out); *out=NULL; r=ESP_ERR_NO_MEM; }
        else swapped_devices[slot]=*out;
    }
    unlock_bus(); return r;
}
esp_err_t __wrap_i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    esp_err_t r=__real_i2c_master_bus_rm_device(dev);
    if(r==ESP_OK)for(unsigned i=0;i<16;i++)if(swapped_devices[i]==dev)swapped_devices[i]=NULL;
    unlock_bus();return r;
}
esp_err_t __real_i2c_del_master_bus(i2c_master_bus_handle_t);
esp_err_t __wrap_i2c_del_master_bus(i2c_master_bus_handle_t bus) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    route(false);
    esp_err_t r=__real_i2c_del_master_bus(bus);
    if(r==ESP_OK && bus==shared_bus){shared_bus=NULL;swapped=false;}
    unlock_bus();return r;
}

#define WRAP_TRANSFER(name, params, args) \
    esp_err_t __real_##name params; \
    esp_err_t __wrap_##name params { \
        if (!lock_bus()) return ESP_ERR_NO_MEM; \
        route(is_swapped_device(dev)); \
        esp_err_t result = __real_##name args; \
        route(false); \
        unlock_bus(); \
        return result; \
    }

WRAP_TRANSFER(i2c_master_transmit,
    (i2c_master_dev_handle_t dev,const uint8_t *data,size_t len,int timeout), (dev,data,len,timeout))
WRAP_TRANSFER(i2c_master_receive,
    (i2c_master_dev_handle_t dev,uint8_t *data,size_t len,int timeout), (dev,data,len,timeout))
WRAP_TRANSFER(i2c_master_transmit_receive,
    (i2c_master_dev_handle_t dev,const uint8_t *tx,size_t ntx,uint8_t *rx,size_t nrx,int timeout), (dev,tx,ntx,rx,nrx,timeout))
WRAP_TRANSFER(i2c_master_multi_buffer_transmit,
    (i2c_master_dev_handle_t dev,i2c_master_transmit_multi_buffer_info_t *buffers,size_t count,int timeout), (dev,buffers,count,timeout))
WRAP_TRANSFER(i2c_master_execute_defined_operations,
    (i2c_master_dev_handle_t dev,i2c_operation_job_t *ops,size_t count,int timeout), (dev,ops,count,timeout))

esp_err_t __real_i2c_master_probe(i2c_master_bus_handle_t,uint16_t,int);
esp_err_t __wrap_i2c_master_probe(i2c_master_bus_handle_t bus,uint16_t addr,int timeout) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    route(bus==shared_bus && addr==0x40);
    esp_err_t r=__real_i2c_master_probe(bus,addr,timeout);
    route(false);unlock_bus();return r;
}
esp_err_t __real_i2c_master_bus_reset(i2c_master_bus_handle_t);
esp_err_t __wrap_i2c_master_bus_reset(i2c_master_bus_handle_t bus) {
    if (!lock_bus()) return ESP_ERR_NO_MEM;
    route(false);
    esp_err_t r=__real_i2c_master_bus_reset(bus);
    unlock_bus();return r;
}
