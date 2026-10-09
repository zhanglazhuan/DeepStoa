#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "axp_prot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "epaper_port.h"
#include "i2c_bsp.h"


static const char *TAG = "EPD_DRIVER";
static SemaphoreHandle_t epd_spi_mutex = NULL;
// TP62 = module pad 20 = GPIO12; TP61 = module pad 21 = GPIO13.
// These were camera data nets: camera must be disconnected for this rework.
static spi_device_handle_t epd_spi_device = NULL;
static esp_err_t epd_hw_send(const uint8_t *data, size_t length)
{
    if (epd_spi_device == NULL) return ESP_ERR_INVALID_STATE;
    if (data == NULL && length != 0) return ESP_ERR_INVALID_ARG;
    while (length) {
        size_t chunk = length > 4096 ? 4096 : length;
        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = data,
        };
        esp_err_t ret = spi_device_polling_transmit(epd_spi_device, &transaction);
        if (ret != ESP_OK) return ret;
        data += chunk;
        length -= chunk;
    }
    return ESP_OK;
}

static inline void epd_spi_lock(void)
{
    if (epd_spi_mutex == NULL) {
        epd_spi_mutex = xSemaphoreCreateRecursiveMutex();
    }
    xSemaphoreTakeRecursive(epd_spi_mutex, portMAX_DELAY);
}

static inline void epd_spi_unlock(void)
{
    if (epd_spi_mutex != NULL) {
        xSemaphoreGiveRecursive(epd_spi_mutex);
    }
}

int epaper_read_busy(void) {
    uint16_t levels = 0;
    if (aw9523_read(EPD_BUSY_PIN, &levels) != ESP_OK) return 0;
    return levels != 0;
}

esp_err_t epaper_port_init(void)
{
    if (epd_spi_mutex == NULL) {
        epd_spi_mutex = xSemaphoreCreateRecursiveMutex();
    }
    esp_err_t ret = aw9523_init(i2c_bus_handle, AW9523_I2C_ADDR, GPIO_NUM_46);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AW9523 unavailable; e-paper interface disabled: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    ESP_RETURN_ON_ERROR(aw9523_set_direction(
        EPD_CS_PIN | EPD_DC_PIN | EPD_POWER_PIN,
        false), TAG, "e-paper output direction failed");
    ESP_RETURN_ON_ERROR(aw9523_set_direction(EPD_BUSY_PIN, true), TAG,
                        "e-paper BUSY direction failed");
    ESP_RETURN_ON_ERROR(aw9523_write(EPD_CS_PIN | EPD_DC_PIN | EPD_POWER_PIN, true),
                        TAG, "e-paper idle levels failed");
    ESP_RETURN_ON_ERROR(aw9523_set_direction(EPD_MOSI_PIN | EPD_SCLK_PIN, true),
                        TAG, "old AW9523 clock/data outputs must be high impedance");
    if (epd_spi_device == NULL) {
        spi_bus_config_t bus = {
            .mosi_io_num = GPIO_NUM_12,
            .miso_io_num = -1,
            .sclk_io_num = GPIO_NUM_13,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                            TAG, "EPD hardware SPI bus init failed");
        spi_device_interface_config_t dev = {
            .clock_speed_hz = 1000000,
            .mode = 0,
            .spics_io_num = -1, // CS remains controlled by AW9523.
            .queue_size = 1,
        };
        ret = spi_bus_add_device(SPI2_HOST, &dev, &epd_spi_device);
        if (ret != ESP_OK) {
            spi_bus_free(SPI2_HOST);
            return ret;
        }
    }
    ESP_LOGI(TAG, "EPD hardware SPI: MOSI GPIO12/TP62, SCLK GPIO13/TP61, 1MHz mode0; CS/DC/BUSY via AW9523");
    return ESP_OK;
}

esp_err_t spi_send_data(uint8_t *data, size_t data_size) {
    epd_spi_lock();
    esp_err_t ret = epd_hw_send(data, data_size);
    epd_spi_unlock();
    return ret;
}

static void spi_send_byte(uint8_t cmd)
{
    epd_spi_lock();
    esp_err_t ret = epd_hw_send(&cmd, 1);
    epd_spi_unlock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "E-paper command 0x%02x via hardware SPI failed: %s", cmd, esp_err_to_name(ret));
    }
    assert(ret == ESP_OK);
}

/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
static void EPD_Reset(void)
{
    epaper_rst_1;
    vTaskDelay(pdMS_TO_TICKS(50));
    epaper_rst_0;
    vTaskDelay(pdMS_TO_TICKS(2));
    epaper_rst_1;
    vTaskDelay(pdMS_TO_TICKS(50));
}

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_SendCommand(UBYTE Reg)
{
    epd_spi_lock();
    epaper_cs_0;
    epaper_dc_0;
    spi_send_byte(Reg);
    epaper_cs_1;
    epd_spi_unlock();
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_SendData(UBYTE Data)
{
    epd_spi_lock();
    epaper_cs_0;
    epaper_dc_1;
    spi_send_byte(Data);
    epaper_cs_1;
    epd_spi_unlock();
}

static void EPD_SendDataBuffer(const UBYTE* buffer, UDOUBLE length)
{
    epd_spi_lock();
    epaper_cs_0;
    epaper_dc_1;
    const int64_t started = esp_timer_get_time();
    esp_err_t ret = ESP_OK;
    if (length >= 4096) ESP_LOGI(TAG, "Frame transfer start: %lu bytes", (unsigned long)length);
    for (UDOUBLE offset = 0; offset < length && ret == ESP_OK;) {
        UDOUBLE chunk = length - offset;
        if (chunk > 4096) chunk = 4096;
        ret = epd_hw_send(buffer + offset, chunk);
        if (ret != ESP_OK) break;
        offset += chunk;
        if (length >= 4096) ESP_LOGI(TAG, "Frame transfer: %lu/%lu bytes (%lld ms)",
            (unsigned long)offset, (unsigned long)length,
            (long long)((esp_timer_get_time() - started) / 1000));
    }
    epaper_cs_1;
    epd_spi_unlock();
    if (ret != ESP_OK) ESP_LOGE(TAG, "Hardware SPI transmission failed: %s", esp_err_to_name(ret));
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
static void EPD_ReadBusy(void)
{
    // ESP_LOGI(TAG,"e-Paper busy");
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int polls = 0; polls < 1500; ++polls)
    {
        if(!ReadBusy){break;}
        // getstat();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (ReadBusy) ESP_LOGE(TAG, "E-paper BUSY timeout after 30 seconds");
    // ESP_LOGI(TAG,"e-Paper busy release");
}

/******************************************************************************
function :	Power on/off control for e-ink screens
parameter:
******************************************************************************/
static void EPD_Power_ON(void)
{
    enapwrstate(ALDO3);
    // disapwrstate(ALDO3);
}

static void EPD_Power_OFF(void)
{
    // enapwrstate(ALDO3);
    disapwrstate(ALDO3);
}

/******************************************************************************
function :	Turn On Display full
parameter:
******************************************************************************/
static void EPD_TurnOnDisplay(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xF7);
	EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

static void EPD_TurnOnDisplay_Fast(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xD7);
	EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

static void EPD_TurnOnDisplay_4GRAY(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xD7);
	EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

static void EPD_TurnOnDisplay_Part(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xFF);
    EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

/******************************************************************************
function :	Initialize the e-Paper register
parameter:
******************************************************************************/
void EPD_Init(void)
{
    EPD_Power_ON();
    vTaskDelay(pdMS_TO_TICKS(10));
    EPD_Reset();

    EPD_ReadBusy();
    EPD_SendCommand(0x12);  //SWRESET
    EPD_ReadBusy();

    EPD_SendCommand(0x18);
    EPD_SendData(0x80);

    EPD_SendCommand(0x0C);
	EPD_SendData(0xAE);
	EPD_SendData(0xC7);
	EPD_SendData(0xC3);
	EPD_SendData(0xC0);
	EPD_SendData(0x80);

    EPD_SendCommand(0x01); //Driver output control
    EPD_SendData((EPD_HEIGHT-1)%256);
    EPD_SendData((EPD_HEIGHT-1)/256);
    EPD_SendData(0x02);

    EPD_SendCommand(0x3C); //BorderWavefrom
    EPD_SendData(0x01);

    EPD_SendCommand(0x11); //data entry mode       
	EPD_SendData(0x01);

	EPD_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendData((EPD_WIDTH-1)%256);    
	EPD_SendData((EPD_WIDTH-1)/256);

	EPD_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_SendData((EPD_HEIGHT-1)%256);    
	EPD_SendData((EPD_HEIGHT-1)/256);  
	EPD_SendData(0x00);
	EPD_SendData(0x00);

    EPD_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_SendData(0x00);
	EPD_SendData(0x00);
    EPD_ReadBusy();

}
//Fast update initialization
void EPD_Init_Fast(void)
{
    EPD_Power_ON();
    vTaskDelay(pdMS_TO_TICKS(500));
	EPD_Reset(); 
	
	EPD_ReadBusy();   
	EPD_SendCommand(0x12);  //SWRESET
	EPD_ReadBusy();   
	
	EPD_SendCommand(0x0C);
	EPD_SendData(0xAE);
	EPD_SendData(0xC7);
	EPD_SendData(0xC3);
	EPD_SendData(0xC0);
	EPD_SendData(0x80);
	
	EPD_SendCommand(0x01); //Driver output control      
	EPD_SendData((EPD_HEIGHT-1)%256);   
	EPD_SendData((EPD_HEIGHT-1)/256);
	EPD_SendData(0x02);

	EPD_SendCommand(0x11); //data entry mode       
	EPD_SendData(0x01);

	EPD_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendData((EPD_WIDTH-1)%256);    
	EPD_SendData((EPD_WIDTH-1)/256);

	EPD_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_SendData((EPD_HEIGHT-1)%256);    
	EPD_SendData((EPD_HEIGHT-1)/256);  
	EPD_SendData(0x00);
	EPD_SendData(0x00);


	EPD_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_SendData(0x00);
	EPD_SendData(0x00);
    EPD_ReadBusy();

	EPD_SendCommand(0x3C); //BorderWavefrom
	EPD_SendData(0x01);	
	
	EPD_SendCommand(0x18);   
	EPD_SendData(0x80); 
	//Fast(1.5s)
	EPD_SendCommand(0x1A); 
	EPD_SendData(0x6A);

}
//4 Gray update initialization
void EPD_Init_4GRAY(void)
{
    EPD_Power_ON();
    vTaskDelay(pdMS_TO_TICKS(500));
	EPD_Reset();
	
	EPD_ReadBusy();   
	EPD_SendCommand(0x12);  //SWRESET
	EPD_ReadBusy();   
	
	EPD_SendCommand(0x0C);
	EPD_SendData(0xAE);
	EPD_SendData(0xC7);
	EPD_SendData(0xC3);
	EPD_SendData(0xC0);
	EPD_SendData(0x80);
	
	EPD_SendCommand(0x01); //Driver output control      
	EPD_SendData((EPD_HEIGHT-1)%256);   
	EPD_SendData((EPD_HEIGHT-1)/256);
	EPD_SendData(0x02);

	EPD_SendCommand(0x11); //data entry mode       
	EPD_SendData(0x01);

	EPD_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendData((EPD_WIDTH-1)%256);    
	EPD_SendData((EPD_WIDTH-1)/256);

	EPD_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_SendData((EPD_HEIGHT-1)%256);    
	EPD_SendData((EPD_HEIGHT-1)/256);  
	EPD_SendData(0x00);
	EPD_SendData(0x00);


	EPD_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_SendData(0x00);
	EPD_SendData(0x00);
    EPD_ReadBusy();

	EPD_SendCommand(0x3C); //BorderWavefrom
	EPD_SendData(0x01);	
	
	EPD_SendCommand(0x18);   
	EPD_SendData(0x80); 
	//4 Gray
	EPD_SendCommand(0x1A); 
	EPD_SendData(0x5A);

}
/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
void EPD_Clear(void)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    UBYTE* buffer = (UBYTE*)malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for clear operation");
        return;
    }
    memset(buffer, 0xFF, buffer_size);
    
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(buffer, buffer_size);
    
    EPD_SendCommand(0x26);
    EPD_SendDataBuffer(buffer, buffer_size);
    
    free(buffer);
    EPD_TurnOnDisplay();
}

void EPD_Clear_Black(void)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;

    EPD_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_SendData(0X00);
        }
    }
    EPD_SendCommand(0x26);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_SendData(0X00);
        }
    }
    EPD_TurnOnDisplay();
}


/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_Display(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, buffer_size);
    
    EPD_TurnOnDisplay();
}

void EPD_Display_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, buffer_size);

    
    
    EPD_SendCommand(0x26);
    EPD_SendDataBuffer(Image, buffer_size);
    
    ESP_LOGI(TAG, "Base frame sent; triggering panel refresh");
    EPD_TurnOnDisplay();
    ESP_LOGI(TAG, "Base refresh wait returned");
}

void EPD_Display_Fast(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, buffer_size);
    
    EPD_TurnOnDisplay_Fast();
}

void EPD_Display_Fast_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, buffer_size);
    
    EPD_SendCommand(0x26);
    EPD_SendDataBuffer(Image, buffer_size);

    EPD_TurnOnDisplay_Fast();
}

void EPD_Display_OneShot(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;
    
    EPD_SendCommand(0x24);
    
    ESP_LOGI(TAG, "Starting one-shot transmission of %lu bytes", (unsigned long)buffer_size);
    EPD_SendDataBuffer(Image, buffer_size);
    
    ESP_LOGI(TAG, "One-shot transmission completed");
    EPD_TurnOnDisplay();
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    if((Xstart % 8 + Xend % 8 == 8 && Xstart % 8 > Xend % 8) || Xstart % 8 + Xend % 8 == 0 || (Xend - Xstart)%8 == 0)
    {
        Xstart = Xstart / 8 ;
        Xend = Xend / 8;
    }
    else
    {
        Xstart = Xstart / 8 ;
        Xend = Xend % 8 == 0 ? Xend / 8 : Xend / 8 + 1;
    }

    UWORD Width = Xend - Xstart;
    UDOUBLE IMAGE_COUNTER = Width * (Yend - Ystart);

    Xend -= 1;
    Yend -= 1;	

    EPD_Reset();

    EPD_SendCommand(0x18);
    EPD_SendData(0x80);

    EPD_SendCommand(0x3C);
    EPD_SendData(0x80);

    EPD_SendCommand(0x44);
    EPD_SendData((Xstart*8) & 0xFF);
    EPD_SendData(((Xstart*8) >> 8) & 0xFF);
    EPD_SendData((Xend*8) & 0xFF);
    EPD_SendData(((Xend*8) >> 8) & 0xFF);

    EPD_SendCommand(0x45);
    EPD_SendData(Yend & 0xFF);
    EPD_SendData((Yend >> 8) & 0xFF);
    EPD_SendData(Ystart & 0xFF);
    EPD_SendData((Ystart >> 8) & 0xFF);

    EPD_SendCommand(0x4E); 
    EPD_SendData((Xstart*8) & 0xFF);
    EPD_SendData(((Xstart*8) >> 8) & 0xFF);

    EPD_SendCommand(0x4F);
    EPD_SendData(Ystart & 0xFF);
    EPD_SendData((Ystart >> 8) & 0xFF);

    EPD_SendCommand(0x24);   //Write Black and White image to RAM

    EPD_SendDataBuffer(Image, IMAGE_COUNTER);

    EPD_TurnOnDisplay_Part();
}

void EPD_Display_4Gray(const UBYTE *Image)
{
    UDOUBLE i,j,k;
    UBYTE temp1,temp2,temp3;
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE IMAGE_COUNTER = Width * Height;
    // old  data
    EPD_SendCommand(0x24);
    for(i=0; i<IMAGE_COUNTER; i++) { 
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;
                else if(temp2 == 0x00)
                    temp3 |= 0x01; 
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else //0x40
                    temp3 |= 0x00; 
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0) 
                    temp3 |= 0x00;
                else if(temp2 == 0x00) 
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x01; 
                else    //0x40
                    temp3 |= 0x00;	
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }
        }
        EPD_SendData(temp3);
        // printf("%x",temp3);
    }

    EPD_SendCommand(0x26); 
    for(i=0; i<IMAGE_COUNTER; i++) {
        temp3=0;
        for(j=0; j<2; j++) {
            temp1 = Image[i*2+j];
            for(k=0; k<2; k++) {
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)
                    temp3 |= 0x00;//white
                else if(temp2 == 0x00)
                    temp3 |= 0x01;  //black
                else if(temp2 == 0x80)
                    temp3 |= 0x00;  //gray1
                else //0x40
                    temp3 |= 0x01; //gray2
                temp3 <<= 1;

                temp1 <<= 2;
                temp2 = temp1&0xC0 ;
                if(temp2 == 0xC0)  //white
                    temp3 |= 0x00;
                else if(temp2 == 0x00) //black
                    temp3 |= 0x01;
                else if(temp2 == 0x80)
                    temp3 |= 0x00; //gray1
                else    //0x40
                    temp3 |= 0x01;	//gray2
                if(j!=1 || k!=1)
                    temp3 <<= 1;

                temp1 <<= 2;
            }
        }
        EPD_SendData(temp3);
        // printf("%x",temp3);
    }
    EPD_TurnOnDisplay_4GRAY();
}

/******************************************************************************
function :	Enter sleep mode
parameter:
******************************************************************************/
void EPD_Sleep(void)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    // Reworked board has no controllable panel RES: epaper_rst maps to
    // strapped EN_POWER, and the discrete-power backend cannot cycle VCI.
    // Deep sleep (0x10/0x01) cannot be undone by the next software reset.
    // Keep controller awake until a real reset/power-switch path is wired.
    epd_spi_lock();
    epaper_cs_1;
    epaper_dc_1;
    epd_spi_unlock();
    ESP_LOGI(TAG, "Panel deep sleep skipped: no controllable RES/power cycle; controller kept awake");
    return;
#else
    EPD_SendCommand(0x10); //enter deep sleep
    EPD_SendData(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
    epaper_rst_0;
    epaper_cs_0;
    epaper_dc_0;
    EPD_Power_OFF();
    vTaskDelay(pdMS_TO_TICKS(10));
#endif
}
