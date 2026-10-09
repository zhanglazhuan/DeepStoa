#include "status_bar.h"

#include "sdkconfig.h"
#include "ImageData.h"
#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "page_network.h"

extern bool wifi_enable;

void DrawWifiStatusIcon(int x, int y)
{
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    // Same portrait hit rectangle as TouchGestures::IsBackButton.
    Paint_DrawRectangle(4, 4, 79, 39, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(4, 4, 79, 39, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(12, 12, "后退", &Font16_UTF8, BLACK, WHITE);
    // Navigation replaces the original top-left clock slot on this board.
#endif
    if (!wifi_enable) {
        return;
    }

#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    const unsigned char* icon = wifi_is_connected() ? gImage_WIFI : gImage_WIFI_OFF;
    Paint_ReadBmp(icon, x, y, 32, 32);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    const char* icon_path = wifi_is_connected() ? BMP_WIFI_PATH : BMP_WIFI_OFF_PATH;
    GUI_ReadBmp(icon_path, x, y);
#endif
}
