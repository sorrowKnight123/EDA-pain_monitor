#include "ssd1289_fsmc.h"
#include "XGA_8x16.h"
#include "cn_font_16.h"
#include "app_config.h"

#include <stdio.h>

/* 24 位色(0xRRGGBB) -> 面板 65K 色字。
 * 注意这里 R 落在低位、B 落在高位，看着像"交换了 R/B"，其实是必需的：
 * 入口模式 R0x11=0x6838 打开了 BGR 位，面板按 B5G6R5 解释这个字。
 * 实测依据：报警块用 RED(0xFF0000) 显示出来是红的 —— 别把这里"修正"回去，
 * 改了所有颜色都会反（红蓝互换、黄色变青）。 */
inline static uint16_t H24_RGB565(uint32_t color24)
{
	uint8_t r = (color24 >> 16) & 0xFF;
	uint8_t g = (color24 >> 8) & 0xFF;
	uint8_t b = color24 & 0xFF;
	return (uint16_t)(((b / 8) << 11) | ((g / 4) << 5) | (r / 8));
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

inline static void LCD_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{	
	LCD_Send_Reg(0x0044, ((x2 << 8) | x1));
	LCD_Send_Reg(0x0045, y1);
	LCD_Send_Reg(0x0046, y2);
	LCD_Send_Reg(0x004E, x1);
	LCD_Send_Reg(0x004F, y1);
	LCD_Send_Cmd(0x0022);   /* 准备写 GRAM（原为 LCD_RAM_PREPARE 宏，只此一处用，去掉间接层） */
}

void LCD_Rect_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24)
{
	uint32_t i = 0; 
	uint32_t j = (uint32_t) w * (uint32_t) h;
	LCD_Window(y, x, y + h - 1, x + w - 1);
	for (i = 0; i < j; i++) LCD_Send_Dat(H24_RGB565(color24));
}

/* 以下两个字符绘制均改为"整格一次开窗流式写"：每个字符只做 1 次窗口设置
 * （原为每个亮点 1 次 LCD_Rect_Fill）。格内灰底直接写入，故仅适用于灰色背景。
 * 写入顺序依据入口模式 R0x11=0x6838（AM=1，ADY 即调用者 x 先递增）：
 * 流式写按调用者坐标逐行填充（y 外层、x 从左到右），与字模行序一致 */
void LCD_Ascii8x16(uint16_t x, uint16_t y, const char *text, uint32_t color24)
{
	uint16_t cx = x;
	for (uint16_t i = 0; text[i] != '\0'; i++)
	{
		const uint8_t *glyph = &XGA_8x16[(uint8_t)text[i] * 16];
		LCD_Window(y, cx, (uint16_t)(y + 15), (uint16_t)(cx + 7));
		for (uint8_t row = 0; row < 16; row++)
		{
			uint8_t bits = glyph[row];
			for (uint8_t col = 0; col < 8; col++)
			{
				LCD_Send_Dat(H24_RGB565((bits & (0x80 >> col)) ? color24 : GRAY));
			}
		}
		cx += 8;
	}
}

void LCD_Cn16(uint16_t x, uint16_t y, const uint8_t *glyph32, uint32_t color24)
{
	LCD_Window(y, x, (uint16_t)(y + 15), (uint16_t)(x + 15));
	for (uint8_t row = 0; row < 16; row++)
	{
		const uint8_t *line = &glyph32[row * 2];
		for (uint8_t col = 0; col < 16; col++)
		{
			LCD_Send_Dat(H24_RGB565((line[col >> 3] & (0x80 >> (col & 7))) ? color24 : GRAY));
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
	LCD_Rect_Fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
}

/* ================= 皮肤电导显示助手（应用布局 320x240，坐标见 ssd1289_fsmc.h 的 LAYOUT_*） =================
 * 左上（0..LAYOUT_SPLIT_X-1）：三行文本（电导1 / 电导2 / 差值），0.5s 刷新，不带单位
 * 右上（LAYOUT_SPLIT_X..319）：双波形（绿=电导1，蓝=电导2），纵轴 0..G_RANGE_MAX，无刻度线
 * 下方（y>LAYOUT_SPLIT_Y）：麻醉状态（相对差 |G1-G2|/max(G1,G2) 过阈值 → 麻醉完全，带迟滞）
 * 背景灰色，区域间白色分隔线（LAYOUT_SPLIT_X 竖线、LAYOUT_SPLIT_Y 横线） */

typedef struct {
	uint8_t old_y[WF_W];    /* 当前状态每列点 y（0xFF = 未填充） */
	uint8_t drawn_a[WF_W];  /* 每列已绘制区间 [a,b]（0xFF/0xFF = 未绘制） */
	uint8_t drawn_b[WF_W];
} WaveBuf;

static WaveBuf wf1, wf2;   /* 电导1（绿）、电导2（蓝） */
static uint8_t wf_inited = 0;

/* 状态行当前显示的内容：0 = 未麻醉完全，1 = 麻醉完全，0xFF = 该行被别的文字占用
 * （CAL 提示），内容与状态无关，下次刷新必须无条件重绘。
 * LCD_DrawCalText() 负责把它置回 0xFF —— 少了这一步，标定结束后若判出的状态
 * 恰好等于缓存值就会跳过重绘，CAL 提示永久留在屏上（曾出现"一直在校准"） */
static uint8_t status_anes = 0xFF;

/* 文本（GB 码序列，0 结尾；<0x100 为 ASCII 走 XGA 8x16）。
 * 三行标签按屏上顺序放进数组，行号即索引（y 见 ROW_LABEL_Y） */
static const uint16_t LABEL_G1[]   = { GB_PI, GB_FU, GB_DIAN, GB_DAO, '1', ':', 0 };
static const uint16_t LABEL_G2[]   = { GB_PI, GB_FU, GB_DIAN, GB_DAO, '2', ':', 0 };
static const uint16_t LABEL_DIFF[] = { GB_DIAN, GB_DAO, GB_CHA, GB_ZHI, ':', 0 };
static const uint16_t *const ROW_LABEL[3] = { LABEL_G1, LABEL_G2, LABEL_DIFF };
static const uint16_t TEXT_NOT_ANES[] = { GB_WEI, GB_MA, GB_ZUI, GB_WAN, GB_QUAN, 0 };  /* 未麻醉完全 */
static const uint16_t TEXT_ANES[]     = { GB_MA, GB_ZUI, GB_WAN, GB_QUAN, 0 };          /* 麻醉完全 */
static uint16_t row_vx[3];   /* 三行数值列起点 x：LCD_InitLayout 算一次，DrawSkinText 复用 */

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
			LCD_Ascii8x16(cx, y, s, color24);
			cx += 8;
		}
		else                 /* 中文：16x16 */
		{
			uint8_t idx = cn_glyph_idx(gb[i]);
			LCD_Cn16(cx, y, CN_GLYPHS[idx == 0xFF ? 0 : idx], color24);
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

/* 数值格式化：不带单位，小数位随量级自适应（G<0 → 0，G>1999.99 限幅）。
 * 屏上三行数值与串口上报共用本函数，两处字面必然一致。
 *   v < 1000   （< 10）    → 2 位小数（小信号保留细节）
 *   v < 10000  （10~100）  → 1 位小数
 *   其余       （>= 100）  → 整数
 * 依据：ADC 一个码在显示值上约造成 ±0.1~0.6 单位波动，固定 0.01 位只是在显示噪声；
 * 自适应后末位仍有意义，且信息量不损失。定点整数运算，避免浮点 printf。
 * 全域枚举 G=0.00..1999.99（步进 0.01）实测最长输出 5 字符 = 40px（"100.0"），
 * 远小于数值列宽 VAL_W_REF —— 不会溢出（旧注释写的"7 字符 1999.99"是错的，
 * 第 3 档根本不出小数）。
 * 下界截零在这里只是兜底，正常调用方（adc_to_g）已保证非负 —— 两处都截零才能
 * 保证"两路显示 0"时差值行也必然是 0 */
void LCD_ValStr(char *out, size_t cap, float G)
{
	uint32_t v;      /* 四舍五入到 0.01 的定点值 */

	if (!(G >= 0.0f)) G = 0.0f;       /* 同时挡住负数和 NaN：NaN 与 0 比较为假，
	                                   * 若写成 G < 0.0f 两个 clamp 都拦不住它，
	                                   * 后面 float→uint32 就是未定义行为 */
	if (G > 1999.99f) G = 1999.99f;   /* 限幅：保证数值不超出固定列宽 */
	v = (uint32_t)(G * 100.0f + 0.5f);

	if (v < 1000u)        /* < 10.00：两位小数 */
	{
		snprintf(out, cap, "%lu.%02lu", (unsigned long)(v / 100u), (unsigned long)(v % 100u));
	}
	else if (v < 10000u)  /* 10.00 ~ 99.99：一位小数，对 v 先四舍五入到 0.1 */
	{
		uint32_t r = (v + 5u) / 10u;   /* 0.1 单位；上限 1000 < 65536，不会溢出 */
		snprintf(out, cap, "%lu.%01lu", (unsigned long)(r / 10u), (unsigned long)(r % 10u));
	}
	else                  /* >= 100：取整 */
	{
		snprintf(out, cap, "%lu", (unsigned long)((v + 50u) / 100u));
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
		LCD_Send_Dat(H24_RGB565(c));
	}
}

/* 双波形差分推进：任一轨迹在某列区间变化时，整列一次开窗重写（灰底+两曲线） */
static void wave_push_pair(float G1, float G2)
{
	uint8_t new_y1[WF_W], new_y2[WF_W];
	uint8_t ny1, ny2;
	int i;

	/* 非有限值/负值/超量程一律夹到 [0, G_RANGE_MAX]。
	 * 越界的 y 不是"画高一点"而是画到别的区域去：y 上限 179 而物理 x 只有 240 列，
	 * 负值映射出的 y>179 会落到分隔线以下，再大些还会回绕到波形区顶部 */
	if (!(G1 >= 0.0f)) G1 = 0.0f;
	if (G1 > G_RANGE_MAX) G1 = G_RANGE_MAX;
	if (!(G2 >= 0.0f)) G2 = 0.0f;
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
		/* 哨兵必须是 0xFF（"从未绘制"），不能是 0 —— 0 是合法 y，
		 * 用 0 会把"没画过"误当成"画在 y=0"，把重写区间无谓地扩到顶部 */
		uint8_t da1 = 0xFF, db1 = 0xFF, da2 = 0xFF, db2 = 0xFF;
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

/* 三行固定标签（绿色，只画一次）：按"标签+参考数值宽度"水平居中定位，
 * 之后数值在标签右侧固定列位置更新，标签永不动。
 * 定位结果存入 row_vx 供 LCD_DrawSkinText 复用（标签与数值共用同一套定位） */
void LCD_InitLayout(void)
{
	uint8_t i;

	LCD_Rect_Fill(0, 0, SCREEN_W, SCREEN_H, GRAY);
	LCD_Rect_Fill(LAYOUT_SPLIT_X, 0, 1, LAYOUT_SPLIT_Y, WHITE);   /* 左右分隔 */
	LCD_Rect_Fill(0, LAYOUT_SPLIT_Y, SCREEN_W, 1, WHITE);         /* 上下分隔 */

	for (i = 0; i < 3; i++)
	{
		uint16_t w = LCD_TextGBWidth(ROW_LABEL[i]);
		uint16_t lx = (uint16_t)((LAYOUT_SPLIT_X - (w + VAL_W_REF)) / 2);
		row_vx[i] = (uint16_t)(lx + w);
		LCD_TextGB(lx, ROW_LABEL_Y(i), ROW_LABEL[i], GREEN);
	}
}

/* 第 i 行数值：固定矩形擦除 + 重绘（标签不动，只更新右侧数值列） */
static void draw_row_value(uint8_t i, float g, uint32_t color24)
{
	char vbuf[12];
	uint16_t vx = row_vx[i];
	uint16_t y = ROW_LABEL_Y(i);

	LCD_ValStr(vbuf, sizeof vbuf, g);
	LCD_Rect_Fill(vx, y, (uint16_t)(LAYOUT_SPLIT_X - vx), 16, GRAY);
	LCD_Ascii8x16(vx, y, vbuf, color24);
}

/* 数值刷新（0.5s 一次）：只更新三行数值与底部状态；
 * 标签/分隔线在 LCD_InitLayout 中已画好，无需重绘。
 * G1/G2 由 adc_to_g 出来时已是非负值，故三行数值互相自洽 */
void LCD_DrawSkinText(float G1, float G2)
{
	float diff = (G1 > G2) ? (G1 - G2) : (G2 - G1);   /* 差值取绝对值 */

	draw_row_value(0, G1,   GREEN);
	draw_row_value(1, G2,   BLUE);
	draw_row_value(2, diff, WHITE);

	/* 底部状态：相对判据 + 迟滞，仅在状态切换时重绘
	 *   相对差 = |G1-G2| / max(G1,G2) × 100%
	 *   占比小说明两部位接近（麻醉分离不明显），占比大说明差异显著
	 * 相对差用定点放大比较，避免浮点除法：diff × 100 与 mx × 阈值 比较 */
	{
		float mx = (G1 > G2) ? G1 : G2;
		float d100 = diff * 100.0f;
		uint8_t now;

		/* 已是"麻醉完全"时改用退出阈值，其余情况（含首次）用进入阈值 → 构成迟滞。
		 * mx <= 0 的守卫不可省：两路都在零点上时 0 >= 0 会误判为"麻醉完全" */
		if (mx <= 0.0f)
		{
			now = 0;
		}
		else if (status_anes == 1)
		{
			now = (d100 <= mx * G_STATE_OFF_PCT) ? 0 : 1;
		}
		else
		{
			now = (d100 >= mx * G_STATE_ON_PCT) ? 1 : 0;
		}

		if (now != status_anes)
		{
			const uint16_t *msg = (now == 0) ? TEXT_NOT_ANES : TEXT_ANES;
			uint16_t mx2 = (SCREEN_W - LCD_TextGBWidth(msg)) / 2;
			LCD_Rect_Fill(0, LAYOUT_STATUS_Y, SCREEN_W, 16, GRAY);
			LCD_TextGB(mx2, LAYOUT_STATUS_Y, msg, GREEN);
			status_anes = now;
		}
	}
}

/* 底部调试行（白色 ASCII，见 LAYOUT_DEBUG_Y）：诊断采集链路用，与正常显示无关 */
void LCD_DebugLine(const char *text)
{
	LCD_Rect_Fill(0, LAYOUT_DEBUG_Y, SCREEN_W, 16, GRAY);
	LCD_Ascii8x16(0, LAYOUT_DEBUG_Y, text, WHITE);
}

/* 上电零点标定期间的状态行提示：此时尚未取得零点，三行数值保持空白，
 * 用一行提示代替"未麻醉完全/麻醉完全"，避免误读。
 * failed=1 表示最近一个 1s 窗口的均值被标定判据拒绝（量程外/不稳定），
 * 提示转红 —— 用户据此能区分"还在采"和"信号不合格，接了也白等"。
 * 用 ASCII 而非汉字，避免为此往字库里加字形。
 * 必须把状态缓存置为"该行已被占用"：否则标定结束后若状态恰好等于
 * 上电时应有的状态，就会跳过重绘、提示永久留在屏上（曾出现"一直在校准"） */
void LCD_DrawCalText(uint8_t failed)
{
	static const uint16_t TEXT_CAL[] = { 'C','A','L','.','.','.', 0 };
	uint16_t mx = (SCREEN_W - LCD_TextGBWidth(TEXT_CAL)) / 2;
	LCD_Rect_Fill(0, LAYOUT_STATUS_Y, SCREEN_W, 16, GRAY);
	LCD_TextGB(mx, LAYOUT_STATUS_Y, TEXT_CAL, failed ? RED : YELLOW);
	status_anes = 0xFF;
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
