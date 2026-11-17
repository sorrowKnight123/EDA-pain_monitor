/**
  ******************************************************************************
  * @file    bsp_anes.c
  * @brief   麻醉状态判定：相对差判据 + 双阈值迟滞 + 持续时间确认
  ******************************************************************************
  */
#include "bsp_anes.h"
#include "app_config.h"

/* 迟滞与计时状态。判定属应用层而非显示驱动：状态因此能在别处复用
 * （上报/记录/告警），显示驱动退化成纯渲染 —— 它只负责把"该显示哪个状态"画出来。 */
static uint8_t  anes_done  = 0;    /* 当前状态（带迟滞记忆） */
static uint8_t  on_active  = 0;    /* 1 = 已越进入阈，正在累计持续时间 */
static uint8_t  off_active = 0;    /* 1 = 已低于退出阈，正在累计持续时间 */
static uint8_t  idle_active = 0;   /* 1 = 两路都贴近零点，正在累计"无有效信号"时长 */
static uint32_t on_since = 0, off_since = 0, idle_since = 0;

/* 相对差 |G1-G2|/max(G1,G2) 而不是绝对差：绝对差会随电极/部位基线漂移，
 * 同一数值在不同基线下含义完全不同；相对差把不同基线归一到同一尺度。
 *
 * 为什么需要三层保护（迟滞 / 持续时间 / 无信号复位）：
 *   迟滞只管幅度，前提是"信号连续变化"。而静电、电极抖动是瞬时的 —— 一次毛刺
 *   越过阈值，状态立刻翻转，下一个刷新周期又翻回来，迟滞挡不住。
 *   因此与 SDLOG_STOP_HOLD_MS 同一思路：越阈必须持续够久才承认。
 *   第三层针对"两路都贴近零点"：此时 max 趋近 0，微小差值就能做出很大的相对差，
 *   判据失去意义。持续满足就清空全部记忆，避免陈旧计时把状态推过阈值
 *   （参考 PulseSensor 的做法：2.5s 没有心跳就把整组判定变量重置） */
uint8_t anes_update(float g1, float g2)
{
	float diff = (g1 > g2) ? (g1 - g2) : (g2 - g1);
	float mx   = (g1 > g2) ? g1 : g2;
	float d100 = diff * 100.0f;
	uint32_t now = HAL_GetTick();

	/* 第一层之外的前提：无有效信号时不做判定，只累计"多久没有信号" */
	if (mx < G_STATE_IDLE_UNITS)
	{
		if (!idle_active) { idle_active = 1; idle_since = now; }
		else if ((now - idle_since) >= G_STATE_IDLE_MS)
		{
			anes_done  = 0;
			on_active  = 0;
			off_active = 0;
			idle_active = 0;
		}
		return anes_done;
	}
	idle_active = 0;

	if (!anes_done)
	{
		if (d100 >= mx * G_STATE_ON_PCT)
		{
			if (!on_active) { on_active = 1; on_since = now; }
			else if ((now - on_since) >= G_STATE_ON_HOLD_MS)
			{
				anes_done = 1;
				on_active = 0;
				off_active = 0;
			}
		}
		else { on_active = 0; }   /* 掉回阈值内：重新计时 */
	}
	else
	{
		if (d100 <= mx * G_STATE_OFF_PCT)
		{
			if (!off_active) { off_active = 1; off_since = now; }
			else if ((now - off_since) >= G_STATE_OFF_HOLD_MS)
			{
				anes_done = 0;
				off_active = 0;
				on_active = 0;
			}
		}
		else { off_active = 0; }
	}

	return anes_done;
}
