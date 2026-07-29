// Display_EPD_W21.c
// Driver for GDEM0397T81P e-ink display (480x800)
// Ported from Zephyr RTOS to ESP-IDF v5.5.3
// Display init sequences and command logic are preserved verbatim.
// SPI is bit-banged through AW9523BTQR I2C GPIO expander.

#include <string.h>
#include "Display_EPD_W21.h"
#include "boards/esp32s3/deepstoa_v1.h"
#include "aw9523.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "epd";

// P0/P1 output register caches — mirror of AW9523 output registers.
// Initialized by EPD_GPIO_Config() before any display operations.
static uint8_t p0_cache;
static uint8_t p1_cache;

// ─── Bit-bang SPI helpers ───────────────────────────────────────────

// SPI Mode 0 (CPOL=0, CPHA=0), MSB first.
// SDI = P0.6, SCLK = P0.7 — both on AW9523 port 0.
static void spi_bb_write_byte(uint8_t data)
{
    for (int i = 7; i >= 0; i--) {
        // Phase 1: set SDI, SCLK = 0
        p0_cache &= ~((1 << 6) | (1 << 7));
        if (data & (1 << i)) {
            p0_cache |= (1 << 6);   // SDI = 1
        }
        aw9523_write_port(0, p0_cache);

        // Phase 2: SCLK = 1 (rising edge latches data)
        p0_cache |= (1 << 7);
        aw9523_write_port(0, p0_cache);
    }
    // SCLK returns to 0
    p0_cache &= ~(1 << 7);
    aw9523_write_port(0, p0_cache);
}

// Send multiple bytes. Yields periodically (~every 64 bytes / 77ms)
// to prevent task watchdog timeout during long transfers.
static void spi_bb_write_bytes(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        spi_bb_write_byte(data[i]);
        if ((i & 0x3F) == 0x3F) {
            vTaskDelay(1);  // yield and reset watchdog
        }
    }
}

// ─── GPIO / Delay functions ─────────────────────────────────────────

void EPD_GPIO_Config(void)
{
    ESP_LOGI(TAG, "Configuring GPIO via AW9523");

    // Initialize output register caches
    p0_cache = 0x00;  // SCLK=0, SDI=0 initially
    aw9523_write_port(0, p0_cache);

    p1_cache = (1 << 4);  // CS=1 (inactive), DC=0
    aw9523_write_port(1, p1_cache);

    ESP_LOGI(TAG, "GPIO config done. CS=1, DC=0, SCLK=0, SDI=0");
}

void delay_xms(unsigned int xms)
{
    vTaskDelay(pdMS_TO_TICKS(xms));
}

// ─── SPI write functions ────────────────────────────────────────────

void EPD_W21_WriteCMD(UBYTE Reg)
{
    // DC = 0 (command mode), CS = 0 (assert)
    p1_cache &= ~((1 << 5) | (1 << 4));  // DC=0, CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_byte(Reg);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA(UBYTE Data)
{
    // DC = 1 (data mode), CS = 0 (assert)
    p1_cache |= (1 << 5);    // DC=1
    p1_cache &= ~(1 << 4);   // CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_byte(Data);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA_Package(uint8_t *DataArray, uint32_t DataLen)
{
    // DC = 1 (data mode), CS = 0 (assert)
    p1_cache |= (1 << 5);    // DC=1
    p1_cache &= ~(1 << 4);   // CS=0
    aw9523_write_port(1, p1_cache);

    spi_bb_write_bytes(DataArray, DataLen);

    // CS = 1 (de-assert)
    p1_cache |= (1 << 4);
    aw9523_write_port(1, p1_cache);
}

void EPD_W21_WriteDATA_Batch(uint8_t *DataArray, uint32_t DataLen)
{
    // Same as _Package in bit-bang mode (no DMA hardware to leverage)
    EPD_W21_WriteDATA_Package(DataArray, DataLen);
}

// ─── Busy / Status functions ────────────────────────────────────────

bool EPD_IsBusy(void)
{
    return (aw9523_get_pin(DEEPV1_PIN_LCD_BUSY) == 1);
}

void EPD_Read_Busy(void)
{
    while (1) {
        if (aw9523_get_pin(DEEPV1_PIN_LCD_BUSY) == 0)
            break;
        delay_xms(10);
    }
}

// ─── Hardware Reset ─────────────────────────────────────────────────
// NOTE: Current PCB has no dedicated LCD RST pin. Hardware GPIO toggle
// is skipped. Display reset relies on 0x12 SWRESET in init sequences.

void EPD_Reset(void)
{
    // Hardware reset pin not available on current PCB.
    // Display will be reset via 0x12 SWRESET command in init sequences.
    delay_xms(200);
}

// ═══════════════════════════════════════════════════════════════════════
// BELOW: All init sequences and display functions preserved VERBATIM
// from the original Zephyr driver. Only the low-level primitives
// (WriteCMD, WriteDATA, WriteDATA_Package, WriteDATA_Batch, Read_Busy,
// IsBusy, delay_xms, Reset) were changed above.
// ═══════════════════════════════════════════════════════════════════════

// Full screen update initialization
void EPD_HW_Init(void)
{
	EPD_Reset();

	EPD_Read_Busy();
	EPD_W21_WriteCMD(0x12); // SWRESET
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x01); // Driver output control
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x01);

	EPD_W21_WriteCMD(0x11); // data entry mode
	EPD_W21_WriteDATA(0x03);

	EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

	EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);

	EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0X199;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_Read_Busy();
}

// Fast update initialization
void EPD_HW_Init_Fast(void)
{
	EPD_Reset();

	EPD_Read_Busy();
	EPD_W21_WriteCMD(0x12); // SWRESET
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x01); // Driver output control
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0x11); // data entry mode
	EPD_W21_WriteDATA(0x03);

	EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

	EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);

	EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0X199;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x01);

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);
	// Fast(1.5s)
	EPD_W21_WriteCMD(0x1A);
	EPD_W21_WriteDATA(0x6A);
}

// 4 Gray update initialization
void EPD_HW_Init_4G(void)
{
	EPD_Reset();

	EPD_Read_Busy();
	EPD_W21_WriteCMD(0x12); // SWRESET
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x01); // Driver output control
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0x11); // data entry mode
	EPD_W21_WriteDATA(0x03);

	EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

	EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);

	EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0X199;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x01);

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);
	// 4 Gray
	EPD_W21_WriteCMD(0x1A);
	EPD_W21_WriteDATA(0x5A);
}

// Full screen update function
void EPD_Update(void)
{
	EPD_W21_WriteCMD(0x22); // Display Update Control
	EPD_W21_WriteDATA(0xF7);
	EPD_W21_WriteCMD(0x20); // Activate Display Update Sequence
	EPD_Read_Busy();
}

// Fast update function
void EPD_Update_Fast(void)
{
	EPD_W21_WriteCMD(0x22); // Display Update Control
	EPD_W21_WriteDATA(0xD7);
	EPD_W21_WriteCMD(0x20); // Activate Display Update Sequence
	EPD_Read_Busy();
}

// 4 Gray update function
void EPD_Update_4G(void)
{
	EPD_W21_WriteCMD(0x22); // Display Update Control
	EPD_W21_WriteDATA(0xD7);
	EPD_W21_WriteCMD(0x20); // Activate Display Update Sequence
	EPD_Read_Busy();
}

// Partial update function
void EPD_Part_Update(void)
{
	EPD_W21_WriteCMD(0x22); // Display Update Control
	EPD_W21_WriteDATA(0xFF);
	EPD_W21_WriteCMD(0x20); // Activate Display Update Sequence
	EPD_Read_Busy();
}

// Async partial update function
void EPD_Part_Update_Async(void)
{
	EPD_W21_WriteCMD(0x22); // Display Update Control
	EPD_W21_WriteDATA(0xFF);
	EPD_W21_WriteCMD(0x20); // Activate Display Update Sequence
	// Do not wait for BUSY
}
//////////////////////////////Display Data Transfer Function////////////////////////////////////////////
// Full screen update display function
void EPD_WhiteScreen_ALL(const unsigned char *datas)
{
	unsigned int i;
	EPD_W21_WriteCMD(0x24); // write RAM for black(0)/white (1)
	EPD_W21_WriteDATA_Package(datas, EPD_ARRAY);
	// for (i = 0; i < EPD_ARRAY; i++)
	// {
	// 	EPD_W21_WriteDATA(datas[i]);
	// }

	// EPD_W21_WriteCMD(0x26); // write RAM for black(0)/white (1)
	// EPD_W21_WriteDATA_Package(datas, EPD_ARRAY);
	// for (i = 0; i < EPD_ARRAY; i++)
	// {
	// 	EPD_W21_WriteDATA(0xff);
	// }
	EPD_Update();
}

// Fast update display function
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas)
{
	unsigned int i;
	EPD_W21_WriteCMD(0x24); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(datas[i]);
	}

	EPD_W21_WriteCMD(0x26); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(0xff);
	}
	EPD_Update_Fast();
}

// Clear screen display
void EPD_WhiteScreen_White(void)
{
	unsigned int i;
	EPD_W21_WriteCMD(0x24); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(0xff);
	}
	EPD_W21_WriteCMD(0x26); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(0xff);
	}
	EPD_Update();
}

// Display all black
void EPD_WhiteScreen_Black(void)
{
	unsigned int i;
	EPD_W21_WriteCMD(0x24); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(0x00);
	}
	EPD_Update();
}

// Partial update of background display, this function is necessary, please do not delete it!!!
void EPD_SetRAMValue_BaseMap(const unsigned char *datas)
{
	unsigned int i;
	EPD_W21_WriteCMD(0x24); // Write Black and White image to RAM
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(datas[i]);
	}
	EPD_W21_WriteCMD(0x26); // Write Black and White image to RAM
	for (i = 0; i < EPD_ARRAY; i++)
	{
		EPD_W21_WriteDATA(datas[i]);
	}
	EPD_Update();
}

// 新增：重置硬件 RAM 窗口为全屏（用于在局部小窗口刷新后恢复状态）
static void EPD_Restore_Full_Window(void)
{
    EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
    EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

    EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
    EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);

    EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0;
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
}


// Partial update display
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE)
{
	unsigned int i;
	unsigned int x_end, y_end;

	x_start = x_start - x_start % 8;   // x address start
	x_end = x_start + PART_LINE - 1;   // x address end
	y_start = y_start;				   // Y address start
	y_end = y_start + PART_COLUMN - 1; // Y address end

	// Reset
	EPD_Reset();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x80);
	//

	EPD_W21_WriteCMD(0x44);			  // set RAM x address start/end
	EPD_W21_WriteDATA(x_start % 256); // x address start2
	EPD_W21_WriteDATA(x_start / 256); // x address start1
	EPD_W21_WriteDATA(x_end % 256);	  // x address end2
	EPD_W21_WriteDATA(x_end / 256);	  // x address end1
	EPD_W21_WriteCMD(0x45);			  // set RAM y address start/end
	EPD_W21_WriteDATA(y_start % 256); // y address start2
	EPD_W21_WriteDATA(y_start / 256); // y address start1
	EPD_W21_WriteDATA(y_end % 256);	  // y address end2
	EPD_W21_WriteDATA(y_end / 256);	  // y address end1

	EPD_W21_WriteCMD(0x4E);			  // set RAM x address count to 0;
	EPD_W21_WriteDATA(x_start % 256); // x address start2
	EPD_W21_WriteDATA(x_start / 256); // x address start1
	EPD_W21_WriteCMD(0x4F);			  // set RAM y address count to 0X127;
	EPD_W21_WriteDATA(y_start % 256); // y address start2
	EPD_W21_WriteDATA(y_start / 256); // y address start1

	EPD_W21_WriteCMD(0x24); // Write Black and White image to RAM
	for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++)
	{
		EPD_W21_WriteDATA(datas[i]);
	}
	EPD_Part_Update();
}

// Full screen partial update display
void EPD_Dis_PartAll(const unsigned char *datas)
{
	unsigned int i;
	unsigned int PART_COLUMN, PART_LINE;
	PART_COLUMN = EPD_HEIGHT, PART_LINE = EPD_WIDTH;

	// Reset
	EPD_Reset();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x80);
	EPD_W21_WriteCMD(0x24); // Write Black and White image to RAM

	EPD_W21_WriteDATA_Package(datas, PART_COLUMN * PART_LINE / 8);
	// for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++)
	// {
	// 	EPD_W21_WriteDATA(datas[i]);
	// }
	EPD_Part_Update();
}

// Async full screen partial update display
void EPD_Dis_PartAll_Async(const unsigned char *datas)
{
	unsigned int PART_COLUMN, PART_LINE;
	PART_COLUMN = EPD_HEIGHT, PART_LINE = EPD_WIDTH;

	// Skip Reset for partial update to save 410ms and avoid blocking
	// EPD_Reset();

	EPD_Restore_Full_Window();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x80);
	EPD_W21_WriteCMD(0x24); // Write Black and White image to RAM

	EPD_W21_WriteDATA_Package(datas, PART_COLUMN * PART_LINE / 8);

	EPD_Part_Update_Async();
}

// Deep sleep function
void EPD_DeepSleep(void)
{
	EPD_W21_WriteCMD(0x10); // Enter deep sleep
	EPD_W21_WriteDATA(0x01);
	delay_xms(100);
}

// Partial update write address and data
void EPD_Dis_Part_RAM(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE)
{
	unsigned int i;
	unsigned int x_end, y_end;

	x_start = x_start - x_start % 8;   // x address start
	x_end = x_start + PART_LINE - 1;   // x address end
	y_start = y_start;				   // Y address start
	y_end = y_start + PART_COLUMN - 1; // Y address end

	// Reset
	EPD_Reset();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x80);
	//

	EPD_W21_WriteCMD(0x44);			  // set RAM x address start/end
	EPD_W21_WriteDATA(x_start % 256); // x address start2
	EPD_W21_WriteDATA(x_start / 256); // x address start1
	EPD_W21_WriteDATA(x_end % 256);	  // x address end2
	EPD_W21_WriteDATA(x_end / 256);	  // x address end1
	EPD_W21_WriteCMD(0x45);			  // set RAM y address start/end
	EPD_W21_WriteDATA(y_start % 256); // y address start2
	EPD_W21_WriteDATA(y_start / 256); // y address start1
	EPD_W21_WriteDATA(y_end % 256);	  // y address end2
	EPD_W21_WriteDATA(y_end / 256);	  // y address end1

	EPD_W21_WriteCMD(0x4E);			  // set RAM x address count to 0;
	EPD_W21_WriteDATA(x_start % 256); // x address start2
	EPD_W21_WriteDATA(x_start / 256); // x address start1
	EPD_W21_WriteCMD(0x4F);			  // set RAM y address count to 0X127;
	EPD_W21_WriteDATA(y_start % 256); // y address start2
	EPD_W21_WriteDATA(y_start / 256); // y address start1

	EPD_W21_WriteCMD(0x24); // Write Black and White image to RAM
	for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++)
	{
		EPD_W21_WriteDATA(datas[i]);
	}
}

// Clock display
void EPD_Dis_Part_Time(unsigned int x_startA, unsigned int y_startA, const unsigned char *datasA,
					   unsigned int x_startB, unsigned int y_startB, const unsigned char *datasB,
					   unsigned int x_startC, unsigned int y_startC, const unsigned char *datasC,
					   unsigned int x_startD, unsigned int y_startD, const unsigned char *datasD,
					   unsigned int x_startE, unsigned int y_startE, const unsigned char *datasE,
					   unsigned int PART_COLUMN, unsigned int PART_LINE)
{
	EPD_Dis_Part_RAM(x_startA, y_startA, datasA, PART_COLUMN, PART_LINE);
	EPD_Dis_Part_RAM(x_startB, y_startB, datasB, PART_COLUMN, PART_LINE);
	EPD_Dis_Part_RAM(x_startC, y_startC, datasC, PART_COLUMN, PART_LINE);
	EPD_Dis_Part_RAM(x_startD, y_startD, datasD, PART_COLUMN, PART_LINE);
	EPD_Dis_Part_RAM(x_startE, y_startE, datasE, PART_COLUMN, PART_LINE);
	EPD_Part_Update();
}

////////////////////////////////Other newly added functions////////////////////////////////////////////
// Display rotation 180 degrees initialization
void EPD_HW_Init_180(void)
{
	EPD_Reset();

	EPD_Read_Busy();
	EPD_W21_WriteCMD(0x12); // SWRESET
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x01); // Driver output control
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x01);

	EPD_W21_WriteCMD(0x11);	 // data entry mode
	EPD_W21_WriteDATA(0x00); // 180

	EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position

	EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);

	EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);

	EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0X199;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_Read_Busy();
}

// GUI initialization
void EPD_HW_Init_GUI(void)
{
	EPD_Reset();

	EPD_Read_Busy();
	EPD_W21_WriteCMD(0x12); // SWRESET
	EPD_Read_Busy();

	EPD_W21_WriteCMD(0x18);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x80);

	EPD_W21_WriteCMD(0x01); // Driver output control
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0x3C); // BorderWavefrom
	EPD_W21_WriteDATA(0x01);

	EPD_W21_WriteCMD(0x11);	 // data entry mode
	EPD_W21_WriteDATA(0x02); // mirror

	EPD_W21_WriteCMD(0x44); // set Ram-X address start/end position
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
	EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);

	EPD_W21_WriteCMD(0x45); // set Ram-Y address start/end position
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
	EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);

	EPD_W21_WriteCMD(0x4E); // set RAM x address count to 0;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); // set RAM y address count to 0X199;
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_Read_Busy();
}

// GUI display
void EPD_Display(unsigned char *Image)
{
	unsigned int Width, Height, i, j;
	Width = (EPD_WIDTH % 8 == 0) ? (EPD_WIDTH / 8) : (EPD_WIDTH / 8 + 1);
	Height = EPD_HEIGHT;

	EPD_W21_WriteCMD(0x24);
	for (j = 0; j < Height; j++)
	{
		for (i = 0; i < Width; i++)
		{
			EPD_W21_WriteDATA(Image[i + j * Width]);
		}
	}
	EPD_Update();
}

/*********************4 Gray****************************/
// 4 Gray display
unsigned char In2bytes_Out1byte_RAM1(unsigned char data1, unsigned char data2)
{
	unsigned int i;
	unsigned char TempData1, TempData2;
	unsigned char outdata = 0x00;
	TempData1 = data1;
	TempData2 = data2;

	for (i = 0; i < 4; i++)
	{
		outdata = outdata << 1;
		if (((TempData1 & 0xC0) == 0xC0) || ((TempData1 & 0xC0) == 0x40))
			outdata = outdata | 0x01;
		else
			outdata = outdata | 0x00;

		TempData1 = TempData1 << 2;
	}

	for (i = 0; i < 4; i++)
	{
		outdata = outdata << 1;
		if ((TempData2 & 0xC0) == 0xC0 || (TempData2 & 0xC0) == 0x40)
			outdata = outdata | 0x01;
		else
			outdata = outdata | 0x00;

		TempData2 = TempData2 << 2;

		// delay_us(5) ;
	}
	return outdata;
}

unsigned char In2bytes_Out1byte_RAM2(unsigned char data1, unsigned char data2)
{
	unsigned int i;
	unsigned char TempData1, TempData2;
	unsigned char outdata = 0x00;
	TempData1 = data1;
	TempData2 = data2;

	for (i = 0; i < 4; i++)
	{
		outdata = outdata << 1;
		if (((TempData1 & 0xC0) == 0xC0) || ((TempData1 & 0xC0) == 0x80))
			outdata = outdata | 0x01;
		else
			outdata = outdata | 0x00;

		TempData1 = TempData1 << 2;
	}

	for (i = 0; i < 4; i++)
	{
		outdata = outdata << 1;
		if ((TempData2 & 0xC0) == 0xC0 || (TempData2 & 0xC0) == 0x80)
			outdata = outdata | 0x01;
		else
			outdata = outdata | 0x00;

		TempData2 = TempData2 << 2;
	}
	return outdata;
}

void EPD_WhiteScreen_ALL_4G(const unsigned char *datas)
{
	unsigned int i;
	unsigned char tempOriginal;

	EPD_W21_WriteCMD(0x24); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY * 2; i += 2)
	{
		tempOriginal = In2bytes_Out1byte_RAM1(*(datas + i), *(datas + i + 1));
		EPD_W21_WriteDATA(~tempOriginal);
	}

	EPD_W21_WriteCMD(0x26); // write RAM for black(0)/white (1)
	for (i = 0; i < EPD_ARRAY * 2; i += 2)
	{
		tempOriginal = In2bytes_Out1byte_RAM2(*(datas + i), *(datas + i + 1));
		EPD_W21_WriteDATA(~tempOriginal);
	}
	EPD_Update_4G();
}

// 这是一个新的刷新函数，专门用于处理已经分好平面的数据
void EPD_Update_4Gray_WithBuffers(const uint8_t *ram1_data, const uint8_t *ram2_data)
{
    // 写入 RAM1 (对应 0x24 寄存器，通常是 "Current" 或 "Black/White" 通道)
    EPD_W21_WriteCMD(0x24);
    EPD_W21_WriteDATA_Package((uint8_t *)ram1_data, EPD_ARRAY);

    // 写入 RAM2 (对应 0x26 寄存器，通常是 "Previous" 或 "Red" 通道，用于实现灰阶)
    EPD_W21_WriteCMD(0x26);
    EPD_W21_WriteDATA_Package((uint8_t *)ram2_data, EPD_ARRAY);

    // 触发 4灰度 更新波形
    EPD_Update_4G();
}

// 静态基准同步：将当前缓冲区内容写入两个显存平面，锁定背景不闪烁
void EPD_Sync_Base_Map(const unsigned char *datas)
{
	// 在写入全屏数据前，解开小窗口的封印！
	EPD_Restore_Full_Window();

    // 1. 写入 Current RAM
    EPD_W21_WriteCMD(0x24);
    EPD_W21_WriteDATA_Package((uint8_t *)datas, EPD_ARRAY);

    // 2. 写入 Previous RAM (关键！让硬件认为当前画面是基准)
    EPD_W21_WriteCMD(0x26);
    EPD_W21_WriteDATA_Package((uint8_t *)datas, EPD_ARRAY);
}

// 异步窗口局部刷新 (支持从全局显存按跨度截取数据)
void EPD_Dis_Part_Window_Activate(void)
{
	EPD_W21_WriteCMD(0x18);  // 温度传感器复位
    EPD_W21_WriteDATA(0x80);

    EPD_W21_WriteCMD(0x3C);  // BorderWavefrom
    EPD_W21_WriteDATA(0x80);
}

// 优化异步窗口函数：移除会导致全局闪烁的 Border(0x3C) 和 Sensor(0x18) 命令
void EPD_Dis_Part_Window_Async_(unsigned int x_start, unsigned int y_start,
                               unsigned int x_end, unsigned int y_end,
                               const unsigned char *frame_buffer, unsigned int stride_bytes)
{
    // 只对 RAM X（垂直方向，像素坐标）做 8 像素对齐
    unsigned int x_start_align = x_start & ~0x07;
    unsigned int x_end_align   = x_end | 0x07;

    // 仅设置窗口和地址，不再重置波形（0x18/0x3C），彻底消除闪烁
    EPD_W21_WriteCMD(0x44);
    EPD_W21_WriteDATA(x_start_align % 256);
    EPD_W21_WriteDATA(x_start_align / 256);
    EPD_W21_WriteDATA(x_end_align % 256);
    EPD_W21_WriteDATA(x_end_align / 256);

    EPD_W21_WriteCMD(0x45);
    EPD_W21_WriteDATA(y_start % 256);
    EPD_W21_WriteDATA(y_start / 256);
    EPD_W21_WriteDATA(y_end % 256);
    EPD_W21_WriteDATA(y_end / 256);

    EPD_W21_WriteCMD(0x4E);
    EPD_W21_WriteDATA(x_start_align % 256);
    EPD_W21_WriteDATA(x_start_align / 256);
    EPD_W21_WriteCMD(0x4F);
    EPD_W21_WriteDATA(y_start % 256);
    EPD_W21_WriteDATA(y_start / 256);

    EPD_W21_WriteCMD(0x24);

    unsigned int bytes_per_row = (x_end_align - x_start_align + 1) / 8;

    for (unsigned int y = y_start; y <= y_end; y++) {
        unsigned int offset = y * stride_bytes + (x_start_align / 8);
        EPD_W21_WriteDATA_Batch((uint8_t*)&frame_buffer[offset], bytes_per_row);
    }
    EPD_Part_Update_Async();
}

void EPD_Dis_Part_Window_Async(unsigned int x_start, unsigned int y_start,
                               unsigned int x_end, unsigned int y_end,
                               const unsigned char *frame_buffer, unsigned int stride_bytes)
{
    unsigned int x_start_align = x_start & ~0x07;
    unsigned int x_end_align   = x_end | 0x07;
    unsigned int bytes_per_row = (x_end_align - x_start_align + 1) / 8;

    // 分配内部 SRAM 行缓冲，规避 PSRAM DMA 对齐 Bug
    uint8_t line_buffer[128] __attribute__((aligned(4)));

    for (unsigned int y = y_start; y <= y_end; y++) {
        // =====================================================================
        // ✅ 核心修复：为每一行单独重置绝对物理坐标！
        // 彻底断绝 SPI 时序断裂导致的级联错位和指针乱飞
        // =====================================================================
        EPD_W21_WriteCMD(0x44); // 设置当前行的 X 范围
        EPD_W21_WriteDATA(x_start_align % 256);
        EPD_W21_WriteDATA(x_start_align / 256);
        EPD_W21_WriteDATA(x_end_align % 256);
        EPD_W21_WriteDATA(x_end_align / 256);

        EPD_W21_WriteCMD(0x45); // 设置当前行的 Y 范围（起点和终点都是当前 y）
        EPD_W21_WriteDATA(y % 256);
        EPD_W21_WriteDATA(y / 256);
        EPD_W21_WriteDATA(y % 256);
        EPD_W21_WriteDATA(y / 256);

        EPD_W21_WriteCMD(0x4E); // 将显存写入指针的 X 移到起点
        EPD_W21_WriteDATA(x_start_align % 256);
        EPD_W21_WriteDATA(x_start_align / 256);

        EPD_W21_WriteCMD(0x4F); // 将显存写入指针的 Y 移到当前行
        EPD_W21_WriteDATA(y % 256);
        EPD_W21_WriteDATA(y / 256);

        EPD_W21_WriteCMD(0x24); // 开启写入模式

        // 从 PSRAM 复制这单行的数据到内部 SRAM
        unsigned int offset = y * stride_bytes + (x_start_align / 8);
        memcpy(line_buffer, &frame_buffer[offset], bytes_per_row);

        // 使用最原生的发送函数，无需操心 CS 状态
        EPD_W21_WriteDATA_Package(line_buffer, bytes_per_row);
    }

    EPD_Part_Update_Async();
}
