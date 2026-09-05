// debug/t_display/main/epd_display.h
// Display API for GDEM0397T81P (480x800 e-ink)
// Init sequences preserved from drivers/gdem0397t81p/Display_EPD_W21.c
// Low-level transport: ESP-IDF hardware SPI + direct GPIO

#ifndef EPD_DISPLAY_H
#define EPD_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t UBYTE;

#define EPD_WIDTH   480
#define EPD_HEIGHT  800
#define EPD_ARRAY   (EPD_WIDTH * EPD_HEIGHT / 8)

// GPIO and SPI setup
void epd_gpio_config(void);
bool epd_is_busy(void);

// Full screen update display
void EPD_HW_Init(void);
void EPD_HW_Init_180(void);
void EPD_WhiteScreen_ALL(const unsigned char *datas);
void EPD_WhiteScreen_White(void);
void EPD_WhiteScreen_Black(void);
void EPD_DeepSleep(void);

// Partial update display
void EPD_SetRAMValue_BaseMap(const unsigned char *datas);
void EPD_Dis_PartAll(const unsigned char *datas);
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start,
                  const unsigned char *datas,
                  unsigned int PART_COLUMN, unsigned int PART_LINE);
void EPD_Dis_Part_Time(unsigned int x_startA, unsigned int y_startA, const unsigned char *datasA,
                       unsigned int x_startB, unsigned int y_startB, const unsigned char *datasB,
                       unsigned int x_startC, unsigned int y_startC, const unsigned char *datasC,
                       unsigned int x_startD, unsigned int y_startD, const unsigned char *datasD,
                       unsigned int x_startE, unsigned int y_startE, const unsigned char *datasE,
                       unsigned int PART_COLUMN, unsigned int PART_LINE);

// Fast update display
void EPD_HW_Init_Fast(void);
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas);

// 4 Gray
void EPD_HW_Init_4G(void);
void EPD_WhiteScreen_ALL_4G(const unsigned char *datas);
void EPD_Update_4Gray_WithBuffers(const uint8_t *ram1_data, const uint8_t *ram2_data);

#endif // EPD_DISPLAY_H
