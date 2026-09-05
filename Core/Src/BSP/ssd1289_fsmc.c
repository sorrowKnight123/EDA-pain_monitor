#include "ssd1289_fsmc.h"
#include "XGA_8x16.h"
#include "cn_font_16.h"
#include "app_config.h"

#include <stdio.h>

uint32_t RGB(uint8_t r, uint8_t g, uint8_t b)
{   
    return ((r & 0xFF) << 16) + ((g & 0xFF) << 8) + (b & 0xFF);
}

inline static uint16_t H24_RGB565(uint8_t reverse, uint32_t color24)
{
	uint8_t b = (color24 >> 16) & 0xFF;
	uint8_t g = (color24 >> 8) & 0xFF;
	uint8_t r = color24 & 0xFF;
	if (reverse) return ((b / 8) << 11) | ((g / 4) << 5) | (r / 8);
	else return ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
}

inline static void LCD_Send_Cmd(uint16_t cmd)
{
	CMD = cmd;
}

inline static void LCD_Send_Dat(uint16_t dat)
{
	DAT = dat;
}

inline static void LCD_Send_Reg(uint16_t cmd, uint16_t dat)
{	
	CMD = cmd;	
	DAT = dat;
}

uint16_t LCD_Read_Reg(uint16_t reg)
{
    CMD = reg;                      // 写寄存器地址
    for(volatile int i=0; i<10; i++); // 等待地址锁存
    volatile uint16_t dummy = DAT;  // 假读一次（丢弃）
    (void)dummy;
    return DAT;                     // 读真实数据
}

inline static void LCD_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{	
	LCD_Send_Reg(0x0044, ((x2 << 8) | x1));
	LCD_Send_Reg(0x0045, y1);
	LCD_Send_Reg(0x0046, y2);
	LCD_Send_Reg(0x004E, x1);
	LCD_Send_Reg(0x004F, y1);
	LCD_RAM_PREPARE
}

void LCD_Pixel(uint16_t x, uint16_t y, uint32_t color24)
{
	LCD_Window(y, x, y, x);   /* 与 LCD_Rect_Fill 保持同一坐标系（x/y 交换到底层物理坐标） */
	LCD_Send_Dat(H24_RGB565(0, color24));
}

void LCD_Rect_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24)
{
	uint32_t i = 0; 
	uint32_t j = (uint32_t) w * (uint32_t) h;
	LCD_Window(y, x, y + h - 1, x + w - 1);
	for (i = 0; i < j; i++) LCD_Send_Dat(H24_RGB565(0, color24));
}

void LCD_Line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t size, uint32_t color24)
{
	int deltaX = abs(x2 - x1);
	int deltaY = abs(y2 - y1);
	int signX = x1 < x2 ? 1 : -1;
	int signY = y1 < y2 ? 1 : -1;
	int error = deltaX - deltaY;
	int error2 = 0;
	for (;;)
	{
		LCD_Rect_Fill(x1, y1, size, size, color24);
		if (x1 == x2 && y1 == y2)
		break;
		error2 = error * 2;
		if (error2 > -deltaY)
		{
			error -= deltaY;
			x1 += signX;
		}
		if (error2 < deltaX)
		{
			error += deltaX;
			y1 += signY;
		}
	}
}

void LCD_Triangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint8_t size, uint32_t color24)
{
	LCD_Line(x1, y1, x2, y2, size, color24);
	LCD_Line(x2, y2, x3, y3, size, color24);
	LCD_Line(x3, y3, x1, y1, size, color24);
}

#define ABS(x) ((x) > 0 ? (x) : -(x))

void LCD_Triangle_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color24)
{
	int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0, 
	yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0, 
	curpixel = 0;
	
	deltax = ABS(x2 - x1);
	deltay = ABS(y2 - y1);
	x = x1;
	y = y1;

	if (x2 >= x1)
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	if (y2 >= y1)
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else
	{
		yinc1 = -1;
		yinc2 = -1;
	}

	if (deltax >= deltay)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = deltax;
		num = deltax / 2;
		numadd = deltay;
		numpixels = deltax;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = deltay;
		num = deltay / 2;
		numadd = deltax;
		numpixels = deltay;
	}

	for (curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		LCD_Line(x, y, x3, y3, 1, color24);

		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
		}
		x += xinc2;
		y += yinc2;
	}
}

void LCD_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t size, uint32_t color24)
{
	LCD_Line(x, y, x + w, y, size, color24);
	LCD_Line(x, y + h, x + w, y + h, size, color24);
	LCD_Line(x, y, x, y + h, size, color24);
	LCD_Line(x + w, y, x + w, y + h, size, color24);
}

void LCD_Ellipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint8_t fill, uint8_t size, uint32_t color24)
{
	int16_t x, y;
	int32_t rx2 = rx * rx;
	int32_t ry2 = ry * ry;
	int32_t fx2 = 4 * rx2;
	int32_t fy2 = 4 * ry2;
	int32_t s;
	if (fill)
	{
		for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++)
		{
			LCD_Line(x0 - x, y0 - y, x0 + x + 1 - size, y0 - y, size, color24);
			LCD_Line(x0 - x, y0 + y, x0 + x + 1 - size, y0 + y, size, color24);
			if (s >= 0)
			{
				s += fx2 * (1 - y);
				y--;
			}
			s += ry2 * ((4 * x) + 6);
		}
		for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1-2 * rx); rx2 * y <= ry2 * x; y++)
		{
			LCD_Line(x0 - x, y0 - y, x0 + x + 1 - size, y0 - y, size, color24);
			LCD_Line(x0 - x, y0 + y, x0 + x + 1 - size, y0 + y, size, color24);
			if (s >= 0)
			{
				s += fy2 * (1 - x);
				x--;
			}
			s += rx2 * ((4 * y) + 6);
		}
	}
	else
	{
		for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++)
		{
			LCD_Rect_Fill(x0 + x, y0 + y, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 + y, size, size, color24);
			LCD_Rect_Fill(x0 + x, y0 - y, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 - y, size, size, color24);
			if (s >= 0)
			{
				s += fx2 * (1 - y);
				y--;
			}
			s += ry2 * ((4 * x) + 6);
		}
		for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++)
		{
			LCD_Rect_Fill(x0 + x, y0 + y, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 + y, size, size, color24);
			LCD_Rect_Fill(x0 + x, y0 - y, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 - y, size, size, color24);
			if (s >= 0)
			{
				s += fy2 * (1 - x);
				x--;
			}
			s += rx2 * ((4 * y) + 6);
		}
	}
}

void LCD_Circle(uint16_t x, uint16_t y, uint8_t radius, uint8_t fill, uint8_t size, uint32_t color24)
{
	int a_, b_, P;
	a_ = 0;
	b_ = radius;
	P = 1 - radius;
	while (a_ <= b_)
	{
		if (fill == 1)
		{
			LCD_Rect_Fill(x - a_, y - b_, 2 * a_ + 1, 2 * b_ + 1, color24);
			LCD_Rect_Fill(x - b_, y - a_, 2 * b_ + 1, 2 * a_ + 1, color24);
		}
		else
		{
			LCD_Rect_Fill(a_ + x, b_ + y, size, size, color24);
			LCD_Rect_Fill(b_ + x, a_ + y, size, size, color24);
			LCD_Rect_Fill(x - a_, b_ + y, size, size, color24);
			LCD_Rect_Fill(x - b_, a_ + y, size, size, color24);
			LCD_Rect_Fill(b_ + x, y - a_, size, size, color24);
			LCD_Rect_Fill(a_ + x, y - b_, size, size, color24);
			LCD_Rect_Fill(x - a_, y - b_, size, size, color24);
			LCD_Rect_Fill(x - b_, y - a_, size, size, color24);
		}
		if (P < 0)
		{
			P = (P + 3) + (2 * a_);
			a_++;
		}
		else
		{
			P = (P + 5) + (2 * (a_ - b_));
			a_++;
			b_--;
		}
	}
}

void LCD_Circle_Helper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint8_t size, uint32_t color24)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;
		if (cornername & 0x4) {
			LCD_Rect_Fill(x0 + x, y0 + y, size, size, color24);
			LCD_Rect_Fill(x0 + y, y0 + x, size, size, color24);
		}
		if (cornername & 0x2) {
			LCD_Rect_Fill(x0 + x, y0 - y, size, size, color24);
			LCD_Rect_Fill(x0 + y, y0 - x, size, size, color24);
		}
		if (cornername & 0x8) {
			LCD_Rect_Fill(x0 - y, y0 + x, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 + y, size, size, color24);
		}
		if (cornername & 0x1) {
			LCD_Rect_Fill(x0 - y, y0 - x, size, size, color24);
			LCD_Rect_Fill(x0 - x, y0 - y, size, size, color24);
		}
	}
}

void LCD_Rect_Round(uint16_t x, uint16_t y, uint16_t length, uint16_t width, uint16_t r, uint8_t size, uint32_t color24)
{
	LCD_Line(x + (r + 2), y, x + length + size - (r + 2), y, size, color24);
	LCD_Line(x + (r + 2), y + width - 1, x + length + size - (r + 2), y + width - 1, size, color24);
	LCD_Line(x, y + (r + 2), x, y + width - size - (r + 2), size, color24);
	LCD_Line(x + length - 1, y + (r + 2), x + length - 1, y + width - size - (r + 2), size, color24);

	LCD_Circle_Helper(x + (r + 2), y + (r + 2), (r + 2), 1, size, color24);
	LCD_Circle_Helper(x + length - (r + 2) - 1, y + (r + 2), (r + 2), 2, size, color24);
	LCD_Circle_Helper(x + length - (r + 2) - 1, y + width - (r + 2) - 1, (r + 2), 4, size, color24);
	LCD_Circle_Helper(x + (r + 2), y + width - (r + 2) - 1, (r + 2), 8, size, color24);
}

void LCD_Circle_Fill_Helper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, uint32_t color24)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		if (cornername & 0x1) {
			LCD_Line(x0 + x, y0 - y, x0 + x, y0 - y + 2 * y + delta, 1, color24);
			LCD_Line(x0 + y, y0 - x, x0 + y, y0 - x + 2 * x + delta, 1, color24);
		}
		if (cornername & 0x2) {
			LCD_Line(x0 - x, y0 - y, x0 - x, y0 - y + 2 * y + delta, 1, color24);
			LCD_Line(x0 - y, y0 - x, x0 - y, y0 - x + 2 * x + delta, 1, color24);
		}
	}
}

void LCD_Rect_Round_Fill(uint16_t x, uint16_t y, uint16_t length, uint16_t width, uint16_t r, uint32_t color24)
{
	LCD_Rect_Fill(x + r, y, length - 2 * r, width, color24);
	LCD_Circle_Fill_Helper(x + length - r - 1, y + r, r, 1, width - 2 * r - 1, color24);
	LCD_Circle_Fill_Helper(x + r, y + r, r, 2, width - 2 * r - 1, color24);
}

static void LCD_Char(int16_t x, int16_t y, const GFXglyph *glyph, const GFXfont *font, uint8_t size, uint32_t color24)
{
	uint8_t  *bitmap = font -> bitmap;
	uint16_t bo = glyph -> bitmapOffset;
	uint8_t bits = 0, bit = 0;
	uint16_t set_pixels = 0;
	uint8_t  cur_x, cur_y;
	for(cur_y = 0; cur_y < glyph -> height; cur_y++)
	{
		for(cur_x = 0; cur_x < glyph -> width; cur_x++)
		{
			if(bit == 0)
			{
				bits = (*(const unsigned char *)(&bitmap[bo++]));
				bit  = 0x80;
			}
			if(bits & bit)
			{
				set_pixels++;
			}
			else if (set_pixels > 0)
			{
				LCD_Rect_Fill(x + (glyph -> xOffset + cur_x - set_pixels) * size, y + (glyph -> yOffset + cur_y) * size, size * set_pixels, size, color24);
				set_pixels = 0;
			}
			bit >>= 1;
		}
		if (set_pixels > 0)
		{
			LCD_Rect_Fill(x + (glyph -> xOffset + cur_x-set_pixels) * size, y + (glyph -> yOffset + cur_y) * size, size * set_pixels, size, color24);
			set_pixels = 0;
		}
	}
}

void LCD_Font(uint16_t x, uint16_t y, const char *text, const GFXfont *p_font, uint8_t size, uint32_t color24)
{
	int16_t cursor_x = x;
	int16_t cursor_y = y;
	GFXfont font;
	memcpy((&font), (p_font), (sizeof(GFXfont)));
	for(uint16_t text_pos = 0; text_pos < strlen(text); text_pos++)
	{
		char c = text[text_pos];
		if(c == '\n')
		{
			cursor_x = x;
			cursor_y += font.yAdvance * size;
		}
		else if(c >= font.first && c <= font.last && c != '\r')
		{
			GFXglyph glyph;
			memcpy((&glyph), (&font.glyph[c - font.first]), (sizeof(GFXglyph)));
			LCD_Char(cursor_x, cursor_y, &glyph, &font, size, color24);
			cursor_x += glyph.xAdvance * size;
		}
	}
}

/* 以下两个字符绘制均改为"整格一次开窗流式写"：每个字符只做 1 次窗口设置
 * （原为每个亮点 1 次 LCD_Rect_Fill）。格内灰底直接写入，故仅适用于灰色背景。
 * 写入顺序依据入口模式 R0x11=0x6838（AM=1，ADY 即调用者 x 先递增）：
 * 流式写按调用者坐标逐行填充（y 外层、x 从左到右），与字模行序一致 */
void LCD_BitmapFont(uint16_t x, uint16_t y, const char *text, const uint8_t *font, uint8_t height, uint8_t size, uint32_t color24)
{
	uint16_t cx = x;
	for (uint16_t i = 0; text[i] != '\0'; i++)
	{
		uint8_t c = (uint8_t)text[i];
		const uint8_t *glyph = &font[(uint16_t)c * height];
		LCD_Window(y, cx, (uint16_t)(y + height * size - 1), (uint16_t)(cx + 8 * size - 1));
		for (uint8_t row = 0; row < height * size; row++)
		{
			uint8_t bits = glyph[row / size];
			for (uint8_t col = 0; col < 8 * size; col++)
			{
				LCD_Send_Dat(H24_RGB565(0, (bits & (0x80 >> (col / size))) ? color24 : GRAY));
			}
		}
		cx += 8 * size;
	}
}
void LCD_CN16Char(uint16_t x, uint16_t y, const uint8_t *glyph32, uint8_t size, uint32_t color24)
{
	LCD_Window(y, x, (uint16_t)(y + 16 * size - 1), (uint16_t)(x + 16 * size - 1));
	for (uint8_t row = 0; row < 16 * size; row++)
	{
		const uint8_t *line = &glyph32[(row / size) * 2];
		for (uint8_t col = 0; col < 16 * size; col++)
		{
			uint8_t cb = (uint8_t)(col / size);
			LCD_Send_Dat(H24_RGB565(0, (line[cb >> 3] & (0x80 >> (cb & 7))) ? color24 : GRAY));
		}
	}
}

void LCD_Init(void)
{
	LCD_Send_Reg(0x0007, 0x0021);
	LCD_Send_Reg(0x0000, 0x0001);
	LCD_Send_Reg(0x0007, 0x0023);
	LCD_Send_Reg(0x0010, 0x0000);
	HAL_Delay(10);
	LCD_Send_Reg(0x0007, 0x0033);
	LCD_Send_Reg(0x0011, 0x6838); //	0x6838 0x6058
	LCD_Send_Reg(0x0002, 0x0600);
	HAL_Delay(10);
	LCD_Rect_Fill(0, 0, 320, 240, BLACK);
}

/* ================= 皮肤电导显示助手（应用布局 320x240，坐标见 ssd1289_fsmc.h 的 LAYOUT_*） =================
 * 左上（0..LAYOUT_SPLIT_X-1）：三行文本（电导1 / 电导2 / 差值），0.5s 刷新
 * 右上（LAYOUT_SPLIT_X..319）：双波形（绿=电导1，蓝=电导2），右缘白色纵轴刻度 0..G_RANGE_MAX μS
 * 下方（y>LAYOUT_SPLIT_Y）：麻醉状态（|G1-G2|<G_DIFF_THRESH → 麻醉完全，否则未麻醉完全，绿色居中）
 * 背景灰色，区域间白色分隔线（LAYOUT_SPLIT_X 竖线、LAYOUT_SPLIT_Y 横线） */

typedef struct {
	uint8_t old_y[WF_W];    /* 当前状态每列点 y（0xFF = 未填充） */
	uint8_t drawn_a[WF_W];  /* 每列已绘制区间 [a,b]（0xFF/0xFF = 未绘制） */
	uint8_t drawn_b[WF_W];
} WaveBuf;

static WaveBuf wf1, wf2;   /* 电导1（绿）、电导2（蓝） */
static uint8_t wf_inited = 0;

/* 布局缓存：标签/数值列 x 在 LCD_InitLayout 算一次，DrawSkinText 复用同一套定位 */
static uint16_t layout_lx1, layout_vx1;
static uint16_t layout_lx2, layout_vx2;
static uint16_t layout_lx3, layout_vx3;

/* 文本（GB 码序列，0 结尾；<0x100 为 ASCII 走 XGA 8x16） */
static const uint16_t LABEL_G1[]   = { GB_PI, GB_FU, GB_DIAN, GB_DAO, '1', ':', 0 };
static const uint16_t LABEL_G2[]   = { GB_PI, GB_FU, GB_DIAN, GB_DAO, '2', ':', 0 };
static const uint16_t LABEL_DIFF[] = { GB_DIAN, GB_DAO, GB_CHA, GB_ZHI, ':', 0 };
static const uint16_t TEXT_NOT_ANES[] = { GB_WEI, GB_MA, GB_ZUI, GB_WAN, GB_QUAN, 0 };  /* 未麻醉完全 */
static const uint16_t TEXT_ANES[]     = { GB_MA, GB_ZUI, GB_WAN, GB_QUAN, 0 };          /* 麻醉完全 */

/* 汉字查找：GB2312 码 -> 字形索引，未找到返回 0xFF */
static uint8_t cn_glyph_idx(uint16_t gb)
{
	uint8_t i;
	for (i = 0; i < (uint8_t)(sizeof(CN_GB) / sizeof(CN_GB[0])); i++)
	{
		if (CN_GB[i] == gb) return i;
	}
	return 0xFF;
}

/* 以 GB 码序列绘制一行（中文 16x16、ASCII 8x16），返回绘制宽度 */
static uint16_t LCD_TextGB(uint16_t x, uint16_t y, const uint16_t *gb, uint32_t color24)
{
	uint16_t cx = x;
	uint16_t i;
	for (i = 0; gb[i] != 0; i++)
	{
		if (gb[i] < 0x100)   /* ASCII：XGA 8x16 */
		{
			char s[2];
			s[0] = (char)(gb[i] & 0xFF);
			s[1] = '\0';
			LCD_BitmapFont(cx, y, s, XGA_8x16, 16, 1, color24);
			cx += 8;
		}
		else                 /* 中文：16x16 */
		{
			uint8_t idx = cn_glyph_idx(gb[i]);
			LCD_CN16Char(cx, y, CN_GLYPHS[idx == 0xFF ? 0 : idx], 1, color24);
			cx += 16;
		}
	}
	return (uint16_t)(cx - x);
}

static uint16_t LCD_TextGBWidth(const uint16_t *gb)
{
	uint16_t w = 0, i;
	for (i = 0; gb[i] != 0; i++) w += (gb[i] < 0x100) ? 8 : 16;
	return w;
}

/* 格式化 "xx.xx μS"（μ 用 XGA 0xB5 字形）：定点整数格式化，避免浮点 printf */
static void lcd_val_str(char *out, size_t cap, float G)
{
	size_t n;
	uint32_t v;
	if (G < 0.0f) G = 0.0f;
	if (G > 999.99f) G = 999.99f;   /* 限幅：保证数值不超出固定列宽（"999.99 μS"=72px） */
	v = (uint32_t)(G * 100.0f + 0.5f);   /* 定点到 0.01 μS */
	snprintf(out, cap, "%lu.%02lu", (unsigned long)(v / 100), (unsigned long)(v % 100));
	n = strlen(out);
	if (n + 3 < cap)
	{
		out[n]     = ' ';
		out[n + 1] = (char)0xB5;
		out[n + 2] = 'S';
		out[n + 3] = '\0';
	}
}

/* 波形区间：点 y 与视觉右邻点 yn 的连线区间 [a,b]；yn 无效时取单点 */
static void wave_span(uint8_t y, uint8_t yn, uint8_t *a, uint8_t *b)
{
	if (yn == 0xFF) yn = y;
	if (y < yn) { *a = y; *b = yn; }
	else        { *a = yn; *b = y; }
}

/* 单列一次开窗流式写：[lo,hi] 内按 蓝>绿>灰 叠加（与原"先绿后蓝"覆盖次序一致）。
 * 窗口 ADY 宽度为 1，写入顺序与 GRAM 扫描方向无关，列内必然 y 递增填充 */
static void wave_draw_col(uint16_t x, uint8_t lo, uint8_t hi,
                          uint8_t a1, uint8_t b1, uint8_t a2, uint8_t b2)
{
	LCD_Window((uint16_t)lo, x, (uint16_t)hi, x);
	for (uint8_t yy = lo; yy <= hi; yy++)
	{
		uint32_t c = GRAY;
		if (a1 != 0xFF && yy >= a1 && yy <= b1) c = GREEN;
		if (a2 != 0xFF && yy >= a2 && yy <= b2) c = BLUE;
		LCD_Send_Dat(H24_RGB565(0, c));
	}
}

/* 双波形差分推进：任一轨迹在某列区间变化时，整列一次开窗重写（灰底+两曲线） */
static void wave_push_pair(float G1, float G2)
{
	uint8_t new_y1[WF_W], new_y2[WF_W];
	uint8_t ny1, ny2;
	int i;

	if (G1 > G_RANGE_MAX) G1 = G_RANGE_MAX;
	if (G2 > G_RANGE_MAX) G2 = G_RANGE_MAX;
	ny1 = (uint8_t)((uint16_t)(WF_H - 1) - G1 * (WF_H - 1) / G_RANGE_MAX);
	ny2 = (uint8_t)((uint16_t)(WF_H - 1) - G2 * (WF_H - 1) / G_RANGE_MAX);

	for (i = 0; i < WF_W - 1; i++)
	{
		new_y1[i] = wf1.old_y[i + 1];
		new_y2[i] = wf2.old_y[i + 1];
	}
	new_y1[WF_W - 1] = ny1;
	new_y2[WF_W - 1] = ny2;

	for (i = 0; i < WF_W; i++)
	{
		uint8_t yy1 = new_y1[i], yy2 = new_y2[i];
		uint8_t a1 = 0xFF, b1 = 0xFF, a2 = 0xFF, b2 = 0xFF;
		uint8_t da1 = 0, db1 = 0, da2 = 0, db2 = 0;
		uint8_t lo, hi;
		uint8_t changed = 0;

		if (yy1 != 0xFF)
		{
			uint8_t yn = (i < WF_W - 1) ? new_y1[i + 1] : yy1;
			wave_span(yy1, yn, &a1, &b1);
			da1 = wf1.drawn_a[i]; db1 = wf1.drawn_b[i];
			if (da1 != a1 || db1 != b1) changed = 1;
		}
		if (yy2 != 0xFF)
		{
			uint8_t yn = (i < WF_W - 1) ? new_y2[i + 1] : yy2;
			wave_span(yy2, yn, &a2, &b2);
			da2 = wf2.drawn_a[i]; db2 = wf2.drawn_b[i];
			if (da2 != a2 || db2 != b2) changed = 1;
		}

		if (changed)
		{
			lo = 0xFF; hi = 0;
			/* 重写范围须同时覆盖"旧已绘区间"与"新目标区间"，否则旧轨迹残留 */
			if (da1 != 0xFF) { if (da1 < lo) lo = da1; if (db1 > hi) hi = db1; }
			if (da2 != 0xFF) { if (da2 < lo) lo = da2; if (db2 > hi) hi = db2; }
			if (a1 != 0xFF)  { if (a1 < lo) lo = a1;  if (b1 > hi) hi = b1; }
			if (a2 != 0xFF)  { if (a2 < lo) lo = a2;  if (b2 > hi) hi = b2; }
			/* 单列单窗口完成：擦除+绿+蓝一次流式写完 */
			wave_draw_col(WF_X0 + i, lo, hi, a1, b1, a2, b2);
			wf1.drawn_a[i] = a1; wf1.drawn_b[i] = b1;
			wf2.drawn_a[i] = a2; wf2.drawn_b[i] = b2;
		}
		wf1.old_y[i] = yy1;
		wf2.old_y[i] = yy2;
	}
}

/* 初始化布局：灰底、白色分隔线、右缘纵轴刻度 0/10/20/30/40/50 */
void LCD_InitLayout(void)
{
	static const uint8_t tick_v[6] = { 0, 10, 20, 30, 40, 50 };  /* 刻度值需与 G_RANGE_MAX 量程一致 */
	uint8_t i;
	LCD_Rect_Fill(0, 0, 320, 240, GRAY);
	LCD_Rect_Fill(LAYOUT_SPLIT_X, 0, 1, LAYOUT_SPLIT_Y, WHITE);   /* 左右分隔 */
	LCD_Rect_Fill(0, LAYOUT_SPLIT_Y, 320, 1, WHITE);              /* 上下分隔 */
	for (i = 0; i < 6; i++)
	{
		uint16_t yt = (uint16_t)((uint16_t)(WF_H - 1) - tick_v[i] * (WF_H - 1) / G_RANGE_MAX);
		uint16_t tp;
		char s[4];
		LCD_Rect_Fill(SCALE_X_TICK, yt, 3, 1, WHITE);          /* 刻度短线 */
		snprintf(s, sizeof s, "%u", (unsigned)tick_v[i]);
		tp = (yt < 8) ? 0 : ((yt > 163) ? 163 : (uint16_t)(yt - 8));
		LCD_BitmapFont((uint16_t)(320 - strlen(s) * 8), tp, s, XGA_8x16, 16, 1, WHITE);
	}
	/* 三行固定标签（绿色，只画一次）：按"标签+参考数值宽度"水平居中定位，
	 * 之后数值在标签右侧固定列位置更新，标签永不动。
	 * 定位结果存入 layout_* 供 LCD_DrawSkinText 复用（两处共用同一套数值） */
	layout_lx1 = (LAYOUT_SPLIT_X - (LCD_TextGBWidth(LABEL_G1) + VAL_W_REF)) / 2;
	layout_vx1 = layout_lx1 + LCD_TextGBWidth(LABEL_G1);
	layout_lx2 = (LAYOUT_SPLIT_X - (LCD_TextGBWidth(LABEL_G2) + VAL_W_REF)) / 2;
	layout_vx2 = layout_lx2 + LCD_TextGBWidth(LABEL_G2);
	layout_lx3 = (LAYOUT_SPLIT_X - (LCD_TextGBWidth(LABEL_DIFF) + VAL_W_REF)) / 2;
	layout_vx3 = layout_lx3 + LCD_TextGBWidth(LABEL_DIFF);
	LCD_TextGB(layout_lx1, 50, LABEL_G1, GREEN);
	LCD_TextGB(layout_lx2, 82, LABEL_G2, GREEN);
	LCD_TextGB(layout_lx3, 114, LABEL_DIFF, GREEN);
}

/* 数值刷新（0.5s 一次）：只更新三行数值与底部状态；
 * 标签/分隔线/刻度在 LCD_InitLayout 中已画好，无需重绘 */
void LCD_DrawSkinText(float G1, float G2)
{
	float diff = (G1 > G2) ? (G1 - G2) : (G2 - G1);   /* 差值取绝对值 */
	char vbuf[12];
	uint16_t y;
	uint16_t vx1 = layout_vx1, vx2 = layout_vx2, vx3 = layout_vx3;

	/* 第 1 行数值（绿）：固定矩形擦除 + 重绘 */
	y = LAYOUT_VAL_Y1;
	lcd_val_str(vbuf, sizeof vbuf, G1);
	LCD_Rect_Fill(vx1, y, (uint16_t)(LAYOUT_SPLIT_X - vx1), 16, GRAY);
	LCD_BitmapFont(vx1, y, vbuf, XGA_8x16, 16, 1, GREEN);

	/* 第 2 行数值（蓝） */
	y += LAYOUT_VAL_PITCH;
	lcd_val_str(vbuf, sizeof vbuf, G2);
	LCD_Rect_Fill(vx2, y, (uint16_t)(LAYOUT_SPLIT_X - vx2), 16, GRAY);
	LCD_BitmapFont(vx2, y, vbuf, XGA_8x16, 16, 1, BLUE);

	/* 第 3 行数值（白） */
	y += LAYOUT_VAL_PITCH;
	lcd_val_str(vbuf, sizeof vbuf, diff);
	LCD_Rect_Fill(vx3, y, (uint16_t)(LAYOUT_SPLIT_X - vx3), 16, GRAY);
	LCD_BitmapFont(vx3, y, vbuf, XGA_8x16, 16, 1, WHITE);

	/* 底部状态：仅状态切换时重绘（未麻醉完全 / 麻醉完全，绿色居中） */
	{
		static uint8_t last_anes = 0xFF;
		uint8_t now = (diff < G_DIFF_THRESH) ? 0 : 1;
		if (now != last_anes)
		{
			const uint16_t *msg = (now == 0) ? TEXT_NOT_ANES : TEXT_ANES;
			uint16_t mx = (320 - LCD_TextGBWidth(msg)) / 2;
			LCD_Rect_Fill(0, LAYOUT_STATUS_Y, 320, 16, GRAY);
			LCD_TextGB(mx, LAYOUT_STATUS_Y, msg, GREEN);
			last_anes = now;
		}
	}
}

/* 双波形实时更新：每个 ADC 采样点推进两路电导曲线 */
void LCD_WaveformPush(float G1, float G2)
{
	int i;

	if (!wf_inited)
	{
		for (i = 0; i < WF_W; i++)
		{
			wf1.old_y[i] = 0xFF; wf1.drawn_a[i] = 0xFF; wf1.drawn_b[i] = 0xFF;
			wf2.old_y[i] = 0xFF; wf2.drawn_a[i] = 0xFF; wf2.drawn_b[i] = 0xFF;
		}
		wf_inited = 1;
	}

	wave_push_pair(G1, G2);
}
