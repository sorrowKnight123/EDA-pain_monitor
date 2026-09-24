/**
  ******************************************************************************
  * @file    bsp_anes.c
  * @brief   麻醉状态判定：相对差判据 + 双阈值迟滞
  ******************************************************************************
  */
#include "bsp_anes.h"
#include "app_config.h"

/* 迟滞记忆：上一次判定的状态 */
static uint8_t anes_done = 0;

/* 判定属应用层而非显示驱动：状态因此能在别处复用（上报/记录/告警），
 * 显示驱动退化成纯渲染 —— 它只负责把"该显示哪个状态"画出来。
 * 用相对差 |G1-G2|/max(G1,G2) 而不是绝对差：绝对差会随电极/部位基线漂移，
 * 同一数值在不同基线下含义完全不同；相对差把不同基线归一到同一尺度。
 * 进出用两个阈值构成迟滞，避免数值在阈值附近漂动时状态反复跳变。
 * 阈值与迟滞带宽的依据见 app_config.h 的 G_STATE_* 说明 */
uint8_t anes_update(float g1, float g2)
{
	float diff = (g1 > g2) ? (g1 - g2) : (g2 - g1);
	float mx = (g1 > g2) ? g1 : g2;
	float d100 = diff * 100.0f;

	if (mx <= 0.0f)     { anes_done = 0; }   /* 两路都在零点上：0 >= 0 会误判为"麻醉完全" */
	else if (anes_done) { anes_done = (d100 <= mx * G_STATE_OFF_PCT) ? 0 : 1; }
	else                { anes_done = (d100 >= mx * G_STATE_ON_PCT) ? 1 : 0; }

	return anes_done;
}