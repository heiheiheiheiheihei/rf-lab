/*
 * led.h — 板载 WS2812 本地指示(硬件文档 §2.7)
 *
 * 载体:开发板自带 WS2812,单线可寻址,**GPIO48**(F-4d-3 已核实)。
 *
 * 本模块**只做指示,不进安全链**。理由(硬件文档 §2.7):
 *   1. WS2812 在数据线停更后会**保持最后颜色**,没有硬件层面的"灭" ——
 *      所以"灯灭"不能作为"不会发射"的证据;唯一证据是硬件门控 RF_KILL。
 *   2. 因此 KILL 态必须**显式写黑**(led_set_state 会真的发一帧全 0),
 *      不能靠"不刷新"来实现。
 *   3. 语义由**闪烁时序**承载,颜色只是可选增强(色觉障碍 / 山寨板可能只焊单色)。
 */
#pragma once

#include <stdbool.h>

typedef enum {
    LED_STATE_KILL = 0, /* 发射门控关闭(安全态):常灭 */
    LED_STATE_IDLE,     /* 就绪、链路已连:慢闪 0.5Hz,绿 */
    LED_STATE_ARMED,    /* 已武装:快闪 4Hz,红(最醒目) */
    LED_STATE_TX,       /* 发射进行中:常亮红 */
    LED_STATE_FAULT,    /* 故障:三连急闪 + 停 1s,紫 */
} led_state_t;

/* 初始化 RMT 通道与 GPIO48,并把灯**立即写黑**。
 * ⚠️ 必须在启动早期调用:上电到首次写黑之间,灯保持复位状态(灭),
 *    这与 KILL 默认态一致,所以这段窗口是安全的,但不可延长。 */
void led_init(void);

void led_set_state(led_state_t st);

/* USB 链路活动叠加:收到一帧就调用一次,内部实现"每 4s 一次 50ms 蓝闪"。 */
void led_note_link_activity(void);

/* 启动指示任务(独立低优先级任务,避免挤占射频时序,见 §2.7 纪律 3)。 */
void led_start(void);
