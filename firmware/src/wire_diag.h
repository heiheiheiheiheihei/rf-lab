/*
 * wire_diag.h — 现场接线诊断(**仅 bring-up 诊断构建启用,默认关闭**)
 *
 * 目的:在没有万用表/示波器的现场,把"模块不应答"拆成可判读的电气证据:
 *   - 各相关 GPIO 的空闲电平(强上拉 / 强下拉 / 无上下拉)
 *   - 选中某槽位后 GPIO13(MOSI)/GPIO14(MISO) 是否被模块驱动(可发现 MOSI/MISO 反接/短路)
 *   - 用 GPIO 位操作直接按 Si446x SPI 时序发 POWER_UP + READ_CMD_BUFF + GET_PART_INFO
 *     (绕开 SPI 外设,验证时序/接线)
 *   - SPI2 外设回环(需 GPIO13–14 短接):区分"SPI 外设坏"与"模块/接线坏"
 *
 * ⚠️ 安全与副作用(必读):
 *   - **不发射**:不发任何 TX/CE 动作,不碰 RF_KILL(GPIO8)与 2.4G 总闸(GPIO7);
 *   - 但它会**给 M1 发 POWER_UP**、并把三路 EN 提前拉高 ⇒ 会改变上电时序,
 *     与正式探测(rf_probe)混用时 M1 会被 POWER_UP 两次(AN625 未定义运行期
 *     重复 POWER_UP 的语义),排查时需知道这一点;
 *   - 已**删除**旧的 GPIO1/GPIO2 "模块 VCC/SDN 探点"功能:M-09 —— 它们是
 *     电池分压(ADC1_CH0)/ NTC(ADC1_CH1),借用会给出错误结论。
 *
 * 输出全部是 '#' 开头的文本行,与启动横幅同一条 UART0(921600)。
 */
#pragma once

/* 在 rf_spi_init() 之前调用。会临时把 SCK/MOSI/MISO/CS/EN 配成 GPIO 使用;
 * 结束时把它恢复为确定状态(CS 高、EN 高保持供电、SCK/MOSI 低、MISO 高阻),
 * 随后 rf_spi_init() 重新接管。
 *
 * 开启方式(默认构建没有定义,函数不会被调用,但源码仍参与编译):
 *   在 firmware/platformio.ini 的 build_flags 里临时加一行
 *     -DRFLAB_WIRE_DEBUG=1
 *   再 `rm -f sdkconfig.esp32s3 && pio run -e esp32s3`。定位完成后**删掉**。 */
void wire_diag_run(void);
