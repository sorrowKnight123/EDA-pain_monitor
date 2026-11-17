#ifndef _SSD1289_FSMC_H_
#define _SSD1289_FSMC_H_
#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_ll_fsmc.h"

#include <stddef.h>

#define CMD  (*(volatile uint16_t *)0x6C000000)   // RS=0，写命令
#define DAT  (*(volatile uint16_t *)0x6C000002)   // RS=1，写/读数据

/* ---- 应用界面布局（逻辑屏 320x240，坐标全部以宏定义，改区域只动这里） ---- */
#define SCREEN_W         320   /* 逻辑屏宽（物理面板 240x320，横屏使用） */
#define SCREEN_H         240   /* 逻辑屏高 */
#define LAYOUT_SPLIT_X   160   /* 左右分隔竖线 x（左=文本数值区，右=波形区） */
#define LAYOUT_SPLIT_Y   180   /* 上下分隔横线 y（上=主显示区，下=状态区） */
#define LAYOUT_VAL_Y1     50   /* 数值行1 y（皮电导1）；行号 0..2 的 y 见 ROW_LABEL_Y */
#define LAYOUT_VAL_PITCH  32   /* 数值行距 */
#define ROW_LABEL_Y(i)    (LAYOUT_VAL_Y1 + (i) * LAYOUT_VAL_PITCH)   /* 第 i 行标签/数值的 y */
#define LAYOUT_STATUS_Y  202   /* 底部麻醉状态文字 y（高 16px） */
#define LAYOUT_DEBUG_Y   220   /* 底部调试行 y（高 16px，紧接状态行下方；ADC_DEBUG_SHOW_RAW 时用） */
/* 量程/阈值等应用参数在 app_config.h（G_RANGE_MAX、G_STATE_ON_PCT / G_STATE_OFF_PCT） */

/* 调色板：只用到的 7 个色（原先还有 HUE_xx 两层间接 + 5 个从未使用的颜色，已删） */
#define BLACK   0x000000 /*   0,   0,   0 */
#define WHITE   0xFFFFFF /* 255, 255, 255 */
#define GRAY    0x7F7F7F /* 128, 128, 128   背景 */
#define RED     0xFF0000 /* 255,   0,   0 */
#define GREEN   0x00FF00 /*   0, 255,   0 */
#define BLUE    0x0000FF /*   0,   0, 255 */
#define YELLOW  0xFFFF00 /* 255, 255,   0 */

/* 波形绘制区（右上，x:WF_X0..WF_X0+WF_W-1，y:0..WF_H-1；随布局分隔线联动） */
#define WF_X0   (LAYOUT_SPLIT_X + 1)
/* 右边界顶到屏幕右缘（159 列 = 原 140 列 + 取消掉的纵轴刻度线与刻度数字所占 19 列），
 * 即波形宽度就由本行决定 */
#define WF_W    (SCREEN_W - WF_X0)
#define WF_H    LAYOUT_SPLIT_Y
#define VAL_W_REF  64               /* 数值列参考宽度（px），仅用于标签水平居中定位。
                                       实测最长输出 5 字符 = 40px（见 LCD_ValStr），
                                       取 64 是为了居中好看并留余量，不是"8 字符"的硬需求 */

void LCD_Init(void);
/* 坐标系约定：所有 LCD_* 绘图函数使用同一 API 坐标，
 * x∈[0,320)、y∈[0,240)，底层经 LCD_Window 交换到 SSD1289 物理坐标（物理屏 240x320，横屏使用） */
void LCD_Rect_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24);
/* ASCII 8x16：字模固定取 XGA_8x16（原 height/size/font 三个参数在全部调用点都是
 * 常量 16/1/XGA_8x16，去掉后函数体内的 size 缩放运算一并消失） */
void LCD_Ascii8x16(uint16_t x, uint16_t y, const char *text, uint32_t color24);
/* 绘制一个 16x16 点阵汉字字形（HZK16 布局：每行 2 字节、高位在前，共 32 字节） */
void LCD_Cn16(uint16_t x, uint16_t y, const uint8_t *glyph32, uint32_t color24);

/* 皮肤电导显示助手（应用布局 320x240）：
 *   左上 160x180：三行文本（"皮肤电导1/2:"、"电导差值:" 标签固定，仅数值 0.5s 更新，不带单位）
 *   右上 159x180：双波形（绿=电导1，蓝=电导2），纵轴 0..G_RANGE_MAX，无刻度线，
 *                右边界顶到屏幕右缘（原刻度线占用的列已并入）
 *   下方 320x60：麻醉状态文字（状态由应用层判定后传入，本文件不做判定；
 *               见 LCD_DrawSkinText 的 anes 参数）仅状态切换时更新；
 *               标定期间该行显示 "CAL..." 提示，结束后自动恢复状态文字
 *   背景灰色，白线分隔；LCD_InitLayout() 一次性绘制灰底/分隔线/标签；
 *   每个 ADC 采样点调用 LCD_WaveformPush() 推进两路 */
/* 必须先调用 LCD_InitLayout()：它画好灰底/分隔线/标签，并算出三行数值列的位置，
 * LCD_DrawSkinText 依赖该位置（顺序颠倒会把数值画到 x=0 处） */
void LCD_InitLayout(void);
/* anes = 应用层已判定好的麻醉状态（0 = 未麻醉完全，1 = 麻醉完全）：判定含迟滞，
 * 记忆留在应用层（main.c 的 anes_state），本文件只把给定状态画出来 */
void LCD_DrawSkinText(float G1, float G2, uint8_t anes);
void LCD_WaveformPush(float G1, float G2);
void LCD_DebugLine(const char *text);
/* 上电零点标定期间的状态行提示（此时尚未取得零点，三行数值保持空白）：
 * failed=0 黄色"还能采"，failed=1 红色"最近一轮均值不合格" */
void LCD_DrawCalText(uint8_t failed);

/* SD 记录指示：状态行右端（告警红块左侧，x=272..303）显示红色 "REC"。
 * on=1 显示、on=0 擦成灰底。必须每轮 500ms 刷新时无条件重绘 ——
 * LCD_DrawSkinText 在状态切换时会整行擦灰，只在变化时画会被擦掉后永久消失 */
void LCD_DrawRecFlag(uint8_t on);

/* 数值格式化：不带单位，小数位随量级自适应（<10 两位、<100 一位、其余整数）。
 * 屏上三行数值都调用本函数；cap 建议 ≥ 8 */
void LCD_ValStr(char *out, size_t cap, float G);


#ifdef __cplusplus
}
#endif

#endif /* _SSD1289_FSMC_H_ */
