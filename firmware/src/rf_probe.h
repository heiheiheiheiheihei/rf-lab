/*
 * rf_probe.h — 三模块在线探测(阶段 2 第一步)
 *
 * 目的只有一个:**在没有驱动之前,先证明"接线 + 供电 + SPI 通路"是通的**。
 * 做法是读每个芯片"出厂就有、不需要配置"的标识寄存器:
 *   M1 Si4463   : GET_PART_INFO 命令(0x01)→ CTS 应 0xFF,PART 应 0x4463
 *   M2 CC1101   : PARTNUM(状态寄存器 0x30,读头 0xC0|0x30)= 0x00,
 *                 VERSION(0x31)= 0x14(CC1100 为 0x04)
 *   M3 nRF24L01 : CONFIG(0x00) 复位值 0x08,STATUS(0x07) 常见 0x0E
 *
 * M2 额外做一次**单点 RSSI 测量**(387 / 433.92 / 464 MHz):
 * 它证明的不只是 SPI,而是"调谐 + 接收链路"真的在工作。读数为噪声底也正常,
 * 因为测量时并没有要求空中有信号。
 *
 * ⚠️ 安全边界(每一条都是硬约束):
 *   - **不发射**:不写 TX FIFO、不发 STX/TX 选通、不拉 CE。
 *   - M3 的 CE = AND(GPIO7, GPIO8);GPIO7/RF_KILL 由 main.c 保持低 ⇒ CE=0 ⇒
 *     nRF24 power-down,探测安全。固件不增加 CE GPIO。
 *   - 线自证命令按槽位选择**只读**命令(M1 0x44 READ_CMD_BUFF、M2 0xC0|0x30
 *     状态读、M3 0xFF NOP);禁止再用 0xB1(对 nRF24 是 W_TX_PAYLOAD_NOACK)。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RF_PROBE_POINTS 3

typedef struct {
    bool     present;
    /* M1 专用:最近一次 READ_CMD_BUFF 的**实际 CTS 字节**(0xFF = 命令就绪)。
     * ⚠️ 不能再用常量 0xFF 冒充:悬空 MISO 读回同样是 0xFF,双方无法区分。 */
    uint8_t  cts;
    /* 芯片原始标识(按模块含义不同,probe 后打印出来便于人工判读)
     *   M1: id0 = CHIPREV(id0 现在是真实响应字节),id1/id2 = PART[7:0]/PART[15:8]
     *   M2: id0 = PARTNUM,id1 = VERSION
     *   M3: id0 = CONFIG,id1 = STATUS */
    uint8_t  id0;
    uint8_t  id1;
    uint8_t  id2;
    /* 仅 M2:三个频点的 RSSI */
    uint32_t freq_hz[RF_PROBE_POINTS];
    int      rssi_dbm[RF_PROBE_POINTS]; /* 仅在 rssi_ok 为真时有效 */
    uint8_t  rssi_raw[RF_PROBE_POINTS];
    bool     rssi_ok[RF_PROBE_POINTS];
    /* 线检:CS 拉低时,MISO 用上拉/下拉各读一次。**只记录事实**:
     *   cs_pu != cs_pd ⇒ 读数随内部上下拉变 ⇒ 该线上没有外部驱动源;
     *   cs_pu == cs_pd ⇒ 读数被外部驱动(值见 cs_pu)。
     * 不据此推断"CS/MISO 一定没接"——芯片未被选中/未上电/未接到时钟时
     * SDO 本来就是高阻,结论必须结合上电与供电证据一起看。 */
    int      cs_pu;
    int      cs_pd;
    /* SPI 自证:MISO 加上拉 / 下拉各读一次,使用该槽位的**只读**命令。
     * 事实判读:两者不同 ⇒ 读回随上下拉变(线上无人驱动);
     *           两者相同 ⇒ 读回不随上下拉变(线上有驱动或采样被固定)。 */
    uint8_t  spi_up;
    uint8_t  spi_down;
} rf_mod_t;

typedef struct {
    rf_mod_t m[3]; /* 0=M1(Si4463) 1=M2(CC1101) 2=M3(nRF24) */
    /* 收尾自检:把 MOSI(13) 当输出、MISO(14) 当输入(内部下拉)。
     * 若现场用一根短接线把 GPIO13–GPIO14 短接:hi=1 且 lo=0 ⇒ PASS,
     * 说明 ESP32 侧 MOSI 输出 + MISO 输入 + GPIO 矩阵都正常,问题在模块/线上。 */
    int loop_hi;
    int loop_lo;
} rf_probe_t;

/* 跑一遍探测。典型 <20ms(M1 CTS 就绪 + CC1101 复位/校准),最坏约 250ms
 * (M1/M2 各 100ms CTS 轮询上限 + CC1101 三点 RSSI 每点 50ms 上限)。
 * 不含可选的 wire_diag(默认构建不编译进去)。可重复调用。 */
void rf_probe_run(rf_probe_t *out);
