#ifndef _DISPLAY_EPD_W21_SPI_
#define _DISPLAY_EPD_W21_SPI_
#include "Arduino.h"

// 你的实际引脚配置
#define EPD_MOSI_PIN 11
#define EPD_SCK_PIN  12
#define EPD_CS_PIN   15
#define EPD_DC_PIN   6
#define EPD_RST_PIN  7
#define EPD_BUSY_PIN 8

// 将原来的 A14-A17 替换为你定义的引脚宏
#define isEPD_W21_BUSY digitalRead(EPD_BUSY_PIN)     // BUSY
#define EPD_W21_RST_0  digitalWrite(EPD_RST_PIN,LOW) // RES 0
#define EPD_W21_RST_1  digitalWrite(EPD_RST_PIN,HIGH)// RES 1
#define EPD_W21_DC_0   digitalWrite(EPD_DC_PIN,LOW)  // DC 0
#define EPD_W21_DC_1   digitalWrite(EPD_DC_PIN,HIGH) // DC 1
#define EPD_W21_CS_0   digitalWrite(EPD_CS_PIN,LOW)  // CS 0
#define EPD_W21_CS_1   digitalWrite(EPD_CS_PIN,HIGH) // CS 1

void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_W21_WriteCMD(unsigned char command);


#endif 
