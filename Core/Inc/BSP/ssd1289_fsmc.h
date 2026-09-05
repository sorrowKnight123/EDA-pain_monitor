#ifndef _SSD1289_FSMC_H_
#define _SSD1289_FSMC_H_
#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_ll_fsmc.h"

#include <stdlib.h>
#include <string.h>

#define CMD  (*(volatile uint16_t *)0x6C000000)   // RS=0，写命令
#define DAT  (*(volatile uint16_t *)0x6C000002)   // RS=1，写/读数据

#define MIRROR_V 0
#define MIRROR_H 0

#define LSHIFT 0

/* ---- 应用界面布局（逻辑屏 320x240，坐标全部以宏定义，改区域只动这里） ---- */
#define LAYOUT_SPLIT_X   160   /* 左右分隔竖线 x（左=文本数值区，右=波形区） */
#define LAYOUT_SPLIT_Y   180   /* 上下分隔横线 y（上=主显示区，下=状态区） */
#define LAYOUT_VAL_Y1     50   /* 数值行1 y（皮电导1） */
#define LAYOUT_VAL_Y2     82   /* 数值行2 y（皮电导2） */
#define LAYOUT_VAL_Y3    114   /* 数值行3 y（电导差值） */
#define LAYOUT_VAL_PITCH  32   /* 数值行距 */
#define LAYOUT_STATUS_Y  202   /* 底部麻醉状态文字 y（高 16px） */
/* 量程/阈值等应用参数在 app_config.h（G_RANGE_MAX、G_DIFF_THRESH） */

#define BLACK   0x000000
#define WHITE   0xFFFFFF
#define RED     HUE_17
#define BLUE    HUE_01
#define BLUE_D  0x0000A0 /*   0,   0, 160 */
#define CYAN    HUE_05
#define YELLOW  HUE_13 /* 255, 255,   0 */
#define MAGENTA HUE_21 /* 255,   0, 255 */
#define GREEN   HUE_09 /*   0, 255,   0 */
#define GREEN_D 0x007F00 /*   0, 128,   0 */
#define PURPLE  0x7F007F /* 128,   0, 128 */
#define TEAL    0x007F7F /*   0, 128, 128 */
#define NAVY    0x00007F /*   0,   0, 128 */
#define SILVER  0xBFBFBF /* 191, 191, 191 */
#define GRAY    0x7F7F7F /* 128, 128, 128 */
#define ORANGE  0xFFA500 /* 255, 165,   0 */
#define BROWN   0xA52A2A /* 165, 255,  42 */
#define MAROON  0x7F0000 /* 128,   0,   0 */
#define OLIVE   0x7F7F00 /* 128, 128,   0 */
#define LIME    HUE_12

#define HUE_01 0x0000FF // 000, 000, 255 - BLUE
#define HUE_02 0x003FFF // 000, 063, 255 - 
#define HUE_03 0x007FFF // 000, 127, 255 - 
#define HUE_04 0x00BFFF // 000, 191, 255 - 
#define HUE_05 0x00FFFF // 000, 255, 255 - CYAN
#define HUE_06 0x00FFBF // 000, 255, 191 - 
#define HUE_07 0x00FF7F // 000, 255, 127 - 
#define HUE_08 0x00FF00 // 000, 255, 063 - 
#define HUE_09 0x00FF00 // 000, 255, 000 - GREEN
#define HUE_10 0x3FFF00 // 063, 255, 000 - 
#define HUE_11 0x7FFF7F // 127, 255, 000 - 
#define HUE_12 0xBFFF00 // 191, 255, 000 - LIME
#define HUE_13 0xFFFF00 // 255, 255, 000 - YELLOW
#define HUE_14 0xFFBF00 // 255, 191, 000 - 
#define HUE_15 0xFF7F00 // 255, 127, 000 - 
#define HUE_16 0xFF3F00 // 255, 063, 000 - 
#define HUE_17 0xFF0000 // 255, 000, 000 - RED
#define HUE_18 0xFF003F // 255, 000, 063 - 
#define HUE_19 0xFF007F // 255, 000, 127 - 
#define HUE_20 0xFF00BF // 255, 000, 191 - 
#define HUE_21 0xFF00FF // 255, 000, 255 - MAGENTA
#define HUE_22 0xBF00FF // 191, 000, 255 - 
#define HUE_23 0x7F00FF // 127, 000, 255 - 
#define HUE_24 0x3F00FF // 063, 000, 255 - 

 /* 波形绘制区（右上区域内，x:WF_X0..WF_X0+WF_W-1，y:0..WF_H-1；
  * WF_X0/WF_H 随布局分隔线联动） */
 #define WF_X0   (LAYOUT_SPLIT_X + 1)
 #define WF_W    140
 #define WF_Y    0
 #define WF_H    LAYOUT_SPLIT_Y
 #define SCALE_X_TICK 301            /* 纵轴刻度短线起始列 */
 #define VAL_W_REF  64               /* 数值列参考宽度（"xx.xx μS" 8 字符），用于标签定位 */

typedef struct { // Data stored PER GLYPH
	uint16_t bitmapOffset;     // Pointer into GFXfont->bitmap
	uint8_t  width, height;    // Bitmap dimensions in pixels
	uint8_t  xAdvance;         // Distance to advance cursor (x axis)
	int8_t   xOffset, yOffset; // Dist from cursor position to UL corner
} GFXglyph;

typedef struct { // Data stored for FONT AS A WHOLE:
	uint8_t  *bitmap;      // Glyph bitmaps, concatenated
	GFXglyph *glyph;       // Glyph array
	uint8_t   first, last; // ASCII extents
	uint8_t   yAdvance;    // Newline distance (y axis)
} GFXfont;

uint32_t RGB(uint8_t r, uint8_t g, uint8_t b);

void LCD_Init(void);
uint16_t LCD_Read_Reg(uint16_t reg);
/* 坐标系约定：所有 LCD_* 绘图函数（Pixel/Rect_Fill/Line/字体等）使用同一 API 坐标，
 * x∈[0,320)、y∈[0,240)，底层经 LCD_Window 交换到 SSD1289 物理坐标（物理屏 240x320，横屏使用） */
void LCD_Pixel(uint16_t x, uint16_t y, uint32_t color24);
void LCD_Rect_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24);
void LCD_Line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t size, uint32_t color24);
void LCD_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t size, uint32_t color24);
void LCD_Triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t size, uint32_t color24);
void LCD_Triangle_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color24);
void LCD_Ellipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint8_t fill, uint8_t size, uint32_t color24);
void LCD_Circle(uint16_t x, uint16_t y, uint8_t radius, uint8_t fill, uint8_t size, uint32_t color24);
void LCD_Rect_Round(uint16_t x, uint16_t y, uint16_t length, uint16_t width, uint16_t r, uint8_t size, uint32_t color24);
void LCD_Rect_Round_Fill(uint16_t x, uint16_t y, uint16_t length, uint16_t width, uint16_t r, uint32_t color24);
void LCD_Font(uint16_t x, uint16_t y, const char *text, const GFXfont *p_font, uint8_t size, uint32_t color24);
void LCD_BitmapFont(uint16_t x, uint16_t y, const char *text, const uint8_t *font, uint8_t height, uint8_t size, uint32_t color24);
/* 绘制一个 16x16 点阵汉字字形（HZK16 布局：每行 2 字节、高位在前，共 32 字节） */
void LCD_CN16Char(uint16_t x, uint16_t y, const uint8_t *glyph32, uint8_t size, uint32_t color24);

/* 皮肤电导显示助手（应用布局 320x240）：
 *   左上 160x180：三行文本（"皮肤电导1/2:"、"电导差值:" 标签固定，仅数值 0.5s 更新）
 *   右上 160x180：双波形（绿=电导1，蓝=电导2），右缘白色纵轴刻度 0..50 μS
 *   下方 320x60：麻醉状态（|G1-G2|<10 → 未麻醉完全，否则 → 麻醉完全，仅状态切换时更新）
 *   背景灰色，白线分隔；LCD_InitLayout() 一次性绘制灰底/分隔线/刻度/标签；
 *   每个 ADC 采样点调用 LCD_WaveformPush() 推进两路 */
void LCD_InitLayout(void);
void LCD_DrawSkinText(float G1, float G2);
void LCD_WaveformPush(float G1, float G2);

#define LCD_ON 					LCD_Send_Reg(0x0007, 0x0033);
#define LCD_OFF					LCD_Send_Reg(0x0007, 0x0000);
#define LCD_RAM_PREPARE LCD_Send_Cmd(0x0022);

#ifdef __cplusplus
}
#endif

#endif /* _SSD1289_FSMC_H_ */
