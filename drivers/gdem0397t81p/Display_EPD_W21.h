#ifndef _DISPLAY_EPD_W21_H_
#define _DISPLAY_EPD_W21_H_

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t UBYTE;

#define EPD_WIDTH 480
#define EPD_HEIGHT 800
#define EPD_ARRAY (EPD_WIDTH * EPD_HEIGHT / 8)

#define SPI_CHUNK_SIZE 512

void EPD_GPIO_Config(void);
bool EPD_IsBusy(void);
void EPD_Read_Busy(void);

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
void EPD_Dis_PartAll_Async(const unsigned char *datas);
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE);
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

// epos add: 局部重绘窗口区域异步
void EPD_Sync_Base_Map(const unsigned char *datas);
void EPD_Dis_Part_Window_Activate(void);
void EPD_Dis_Part_Window_Async(unsigned int x_start, unsigned int y_start, unsigned int x_end, unsigned int y_end, const unsigned char *frame_buffer, unsigned int stride_bytes);

#endif  // _DISPLAY_EPD_W21_H_
