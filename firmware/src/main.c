/*
 * main.c — RF-Lab 固件入口(本批:USB-only,阶段 1 骨架 + 只读探测)
 *
 * 本文件做五件事:
 *   1. 上电即把 GPIO7(2.4G 总闸)与 GPIO8(RF_KILL)拉低(**硬件发射门控**)
 *   2. 初始化板载 WS2812 并按硬件文档 §4 的指示语显示
 *   3. 在 **UART0(CH343P)** 上跑契约 §2 的帧协议
 *   4. 实现 §3.1 的四条消息:HELLO / HELLO_ACK / GET_CAPS / CAPS / PING / PONG / ERROR
 *   5. 启动时做一次**不发射**的三模块只读探测(阶段 2 第一步)
 *
 * 🔴 2026-10-21 载体变更(实测驱动):协议**不再**跑在原生 USB Serial/JTAG 上。
 *   本板的原生 USB 从未枚举成功(2026-10-18:435 次 connect / 0 次成功;
 *   2026-10-21 换口复测 150s 零事件),唯一可用通路是 CH343P → UART0。
 *   因此 UART0 由协议独占、控制台与日志关闭(见 sdkconfig.defaults 注释)。
 *   帧层(frame.c)与载体无关。
 *
 * 🔴 2026-10-31 审计修复(本版):帧发送改原始 ROM 字节接口(C-01)、
 *   CC1101 状态寄存器 0xC0 读头(H-01)、POR 时序 EN≥20ms + SDN 脉冲(H-02)、
 *   GPIO7 显式拉低与 M3 CE=AND(7,8) 门控模型(H-03)、HELLO 前置状态机与
 *   client_id 校验、帧超时重同步、LED KILL 语义等(M-01…M-11/L-01…L-17)。
 *
 * 明确**没做**(写出来是为了避免"看起来做了"):
 *   - 射频模块驱动与发射
 *   - RF_KILL / 2.4G 总闸的软件释放:本批两者恒为 0,协议里还没有对应字段
 *     (已知归属缺口,见接线文档 §6)。⚠️ 不要为此自创消息类型。
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_rom_uart.h"
#include "hal/uart_ll.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* 控制通道:CH343P ↔ UART0(GPIO43=TX / GPIO44=RX)。
 * ⚠️ 921600 不是随手选的:本板这条通路已在 921600 上跑过连续 I/Q 传输
 *    (esp-sdr 实测),是唯一被验证过的可用速率。 */
#define RFLAB_UART_PORT    UART_NUM_0
#ifndef RFLAB_UART_BAUD
#define RFLAB_UART_BAUD    921600
#endif
#define PIN_UART0_TX       43
#define PIN_UART0_RX       44

#include "frame.h"
#include "led.h"
#include "pairing.h"
#include "rf_probe.h"
#include "rf_spi.h"
#include "wire_diag.h"

/* 版本后备值必须与 platformio.ini 的 -DRFLAB_FW_VERSION 一致(L-13)。
 * 它只在 build_flags 丢失时生效;不一致会让横幅/HELLO_ACK 报错版本。 */
#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.2.0"
#endif

/* --- 引脚(docs/02-hardware.md §3)------------------------------------ */
/* GPIO7(2.4G 总闸)与 M1 SDN(GPIO9)的宏在 rf_spi.h(与 SPI 引脚同源)。 */
#define PIN_RF_KILL   8  /* 低有效:0 = KILL(禁止发射),1 = 允许。上电必须为 0 */
#define PIN_PAIR_BTN  38 /* 配对按键,低有效(内部上拉)。阶段 1 不消费 */

/* 帧间/帧内字节超时(L-04):超过该时间没有新字节 ⇒ 复位接收状态机。
 * 用于从"截断帧吞掉后续字节"中恢复;契约 §2 未定义超时,这是实现层兜底。 */
#define RFLAB_RX_BYTE_GAP_MS 200

static const char *TAG = "rflab";

/* 临时打点(定位用):直接走 ROM 的 UART0 输出,不依赖任何驱动/控制台配置。
 * ⚠️ 文本可以走 putc;帧字节**不行**(见 tx_raw)。 */
static void mark0(const char *m)
{
    while (*m) {
        esp_rom_output_putc(*m++);
    }
}

static SemaphoreHandle_t s_tx_lock;
static uart_dev_t       *s_uart; /* 轮询直驱用;transport_init 失败时保持 NULL */

static rflab_frame_t s_rx_frame;
static char          s_json[RFLAB_USB_FRAME_MAX + 1];
static uint8_t       s_tx_buf[RFLAB_HDR_LEN + RFLAB_USB_FRAME_MAX];

static bool s_hello_done;
static char s_device_id[16];
static char s_client_id[PAIRING_CLIENT_ID_MAX + 1];

/* 三模块探测结果(阶段 2 第一步)。CAPS.modules[].present 由此派生。 */
static rf_probe_t s_probe;
static rf_wire_t  s_wire;      /* MISO 空闲电平诊断,见 rf_spi.h */
static bool       s_probe_ran; /* false = SPI 初始化失败,present 语义为"未探测" */

/* ------------------------------------------------------------------ */
/* 输出:UART0(CH343P)上**只有帧**。没有日志混流 —— 控制台已关闭。      */
/* ------------------------------------------------------------------ */

static void tx_raw(const void *buf, size_t n)
{
    if (n == 0) {
        return;
    }
    /* 走 ROM 的 UART0 输出:这条路径在本板**已实测可用**(启动横幅就是它打的),
     * 而 IDF uart 驱动在本板当前状态下 RX/TX 都不动(见 transport_init 注释)。
     *
     * ⚠️ C-01:帧字节必须原样发送。`esp_rom_output_putc` == ets_write_char_uart,
     *    它会把 '\n'(0x0A) 扩成 0x0D 0x0A、并把 '\r'(0x0D) **直接丢弃** ——
     *    帧头/载荷全是二进制,任何 0x0A/0x0D 都会让 App 侧错位。
     *    `esp_rom_output_tx_one_char` == uart_tx_one_char,不做任何转换。 */
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < n; i++) {
        (void)esp_rom_output_tx_one_char(p[i]);
    }
    xSemaphoreGive(s_tx_lock);
}

static void send_json(uint8_t type, uint8_t seq, const cJSON *root)
{
    char *txt = cJSON_PrintUnformatted((cJSON *)root);
    if (txt == NULL) {
        return;
    }
    size_t n = rflab_frame_encode(s_tx_buf, sizeof(s_tx_buf), type, seq, txt,
                                  strlen(txt));
    cJSON_free(txt);
    if (n == 0) {
        /* 编码失败 = 载荷超过 usb_frame_max。禁止截断(§2.2.2)。 */
        ESP_LOGE(TAG, "帧编码失败(超长)");
        return;
    }
    tx_raw(s_tx_buf, n);
}

static void send_error(uint8_t seq, const char *code, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", msg);
    cJSON_AddNumberToObject(root, "seq_ref", seq);
    send_json(MSG_ERROR, seq, root);
    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */

static void copy_payload_json(const rflab_frame_t *f)
{
    size_t n = (f->len < RFLAB_USB_FRAME_MAX) ? f->len : RFLAB_USB_FRAME_MAX;
    memcpy(s_json, f->payload, n);
    s_json[n] = '\0';
}

static void handle_hello(const rflab_frame_t *f)
{
    copy_payload_json(f);
    cJSON *root = cJSON_Parse(s_json);
    if (root == NULL) {
        /* ⚠️ 契约 §6 没有"载荷格式错"这一错误码,这里用 unsupported 并说明原因。
         *    这是一个已知缺口,已在报告中记录,不要据此发明新 code。 */
        send_error(f->seq, "unsupported", "HELLO 载荷不是合法 JSON");
        return;
    }

    cJSON *j_min  = cJSON_GetObjectItemCaseSensitive(root, "proto_min");
    cJSON *j_max  = cJSON_GetObjectItemCaseSensitive(root, "proto_max");
    cJSON *j_cid  = cJSON_GetObjectItemCaseSensitive(root, "client_id");
    cJSON *j_pair = cJSON_GetObjectItemCaseSensitive(root, "pairing");

    if (!cJSON_IsNumber(j_min) || !cJSON_IsNumber(j_max)) {
        send_error(f->seq, "version_mismatch", "缺少 proto_min/proto_max");
        cJSON_Delete(root);
        return;
    }
    if (j_min->valueint > (int)RFLAB_VER || j_max->valueint < (int)RFLAB_VER) {
        send_error(f->seq, "version_mismatch", "协议版本区间不含 1");
        cJSON_Delete(root);
        return;
    }

    bool want_pair = cJSON_IsTrue(j_pair);

    /* M-06(2026-10-31 契约同步):`client_id` 是 HELLO schema 的 **required** 字段,
     * 且必须非空 —— 对 **所有** HELLO 生效,不只是 pairing:true。
     * 缺失/空串一律回 unsupported,既**不建立配对**、也**不置 hello_done**
     * (旧实现会用空 client 建 NVS 绑定并下发 secret,之后设备永久 already_bound,
     *   而按键改绑尚未实现 ⇒ 一次畸形 HELLO 就能锁死设备)。 */
    const char *cid = cJSON_IsString(j_cid) ? j_cid->valuestring : NULL;
    if (cid == NULL || cid[0] == '\0') {
        send_error(f->seq, "unsupported", "HELLO 缺少 client_id");
        cJSON_Delete(root);
        return;
    }
    if (strlen(cid) > PAIRING_CLIENT_ID_MAX) {
        send_error(f->seq, "unsupported", "client_id 超长");
        cJSON_Delete(root);
        return;
    }
    strncpy(s_client_id, cid, PAIRING_CLIENT_ID_MAX);
    s_client_id[PAIRING_CLIENT_ID_MAX] = '\0';

    bool pair_ok   = true;
    bool first_bond = false;
    const char *pair_error = NULL;

    if (want_pair) {
        if (!pairing_is_bonded()) {
            /* 首次配对。USB 是可信链路,故不看 pair_code(契约 §7.1(1))。 */
            esp_err_t perr = pairing_create(s_client_id);
            if (perr == ESP_OK) {
                first_bond = true;
            } else {
                pair_ok = false;
                /* L-12:只有随机源失败才回 rng_failed;NVS 错误单独记录,
                 * 不冒充 RNG 问题(契约 pairError 枚举里没有 NVS 码,
                 * 故 NVS 错误时不带 pair_error,只置 pair_ok=false)。 */
                if (perr == ESP_FAIL) {
                    pair_error = "rng_failed";
                } else {
                    ESP_LOGE(TAG, "配对写 NVS 失败: %s", esp_err_to_name(perr));
                }
            }
        } else if (pairing_client_matches(s_client_id)) {
            pair_ok = true; /* 已绑定且同一 client:不重发 secret */
        } else {
            pair_ok = false;
            /* 换绑需要设备端物理按键(§7.1(1))。阶段 1 尚未实现按键消费,
             * 所以此处如实返回 already_bound,而不是"静默改绑"。 */
            pair_error = "already_bound";
        }
    }

    cJSON *ack = cJSON_CreateObject();
    cJSON_AddStringToObject(ack, "fw_version", RFLAB_FW_VERSION);
    cJSON_AddNumberToObject(ack, "proto", RFLAB_VER);
    cJSON_AddStringToObject(ack, "device_id", s_device_id);
    cJSON_AddStringToObject(ack, "chip", "esp32s3");
    cJSON_AddNumberToObject(ack, "uptime_ms",
                            (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(ack, "last_accepted_seq", pairing_last_seq());

    if (want_pair) {
        cJSON_AddBoolToObject(ack, "pair_ok", pair_ok);
        if (pair_ok) {
            cJSON_AddBoolToObject(ack, "first_bond", first_bond);
            if (first_bond) {
                char b64[PAIRING_SECRET_B64_LEN + 1];
                if (pairing_secret_b64url(b64) == ESP_OK) {
                    /* ⚠️ 仅 USB 可信链路上出现,且只出现这一次。 */
                    cJSON_AddStringToObject(ack, "pairing_secret", b64);
                }
            }
        } else if (pair_error != NULL) {
            cJSON_AddStringToObject(ack, "pair_error", pair_error);
        }
    }

    send_json(MSG_HELLO_ACK, f->seq, ack);
    cJSON_Delete(ack);
    cJSON_Delete(root);

    if (!want_pair || pair_ok) {
        s_hello_done = true;
        /* ⚠️ 本批 RF_KILL 恒为 0(2.4G 总闸也恒为 0),所以**不得**切 IDLE
         *    慢闪:那会让"灯在闪 = 已就绪可用"被误解成"可以发射"。
         *    LED 保持 KILL(常灭),直到阶段 4 实现门控释放后再改语义。 */
        ESP_LOGI(TAG, "HELLO 完成,pairing=%d bond=%d",
                 (int)want_pair, (int)first_bond);
    }
}

static void add_band(cJSON *bands, double lo, double hi)
{
    cJSON *band = cJSON_CreateArray();
    cJSON_AddItemToArray(band, cJSON_CreateNumber(lo));
    cJSON_AddItemToArray(band, cJSON_CreateNumber(hi));
    cJSON_AddItemToArray(bands, band);
}

static cJSON *make_module(int slot, const char *chip, double lo, double hi, int tx_max)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddNumberToObject(m, "slot", slot);
    cJSON_AddStringToObject(m, "chip", chip);
    /* ✅ 阶段 2 起 present 来自**真实探测**(读芯片 ID,见 rf_probe.c):
     *    M1 = Si4463 GET_PART_INFO(PART=0x4463)
     *    M2 = CC1101 PARTNUM=0x00 且 VERSION=0x14/0x04
     *    M3 = nRF24L01 CONFIG 复位值 0x08
     *    ⚠️ 若 SPI 初始化失败(横幅 `# probe ran=0`),present=false 表示
     *       **未探测**,不是"探过但没有"(L-08)。 */
    cJSON_AddBoolToObject(m, "present", s_probe.m[slot - 1].present);

    cJSON *bands = cJSON_CreateArray();
    add_band(bands, lo, hi); /* 模块级频段(非芯片级,§4 警示) */
    cJSON_AddItemToObject(m, "bands", bands);

    cJSON_AddNumberToObject(m, "tx_max_dbm", tx_max);
    cJSON_AddBoolToObject(m, "tx_allowed", false); /* 白名单为空 = 禁止一切发射 */
    return m;
}

static void handle_get_caps(const rflab_frame_t *f)
{
    if (!s_hello_done) {
        send_error(f->seq, "not_ready", "尚未完成 HELLO");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "proto", RFLAB_VER);
    cJSON_AddStringToObject(root, "fw", RFLAB_FW_VERSION);
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddNumberToObject(root, "active_slot", 1);

    cJSON *transports = cJSON_CreateArray();
    cJSON_AddItemToArray(transports, cJSON_CreateString("usb"));
    cJSON_AddItemToObject(root, "transports", transports);
    cJSON_AddStringToObject(root, "active_transport", "usb");

    cJSON *modules = cJSON_CreateArray();
    cJSON_AddItemToArray(modules, make_module(1, "si4463",  425000000.0,  525000000.0,   20));
    cJSON_AddItemToArray(modules, make_module(2, "cc1101",  387000000.0,  464000000.0,   10));
    cJSON_AddItemToArray(modules, make_module(3, "nrf24l01p", 2400000000.0, 2525000000.0, 20));
    cJSON_AddItemToObject(root, "modules", modules);

    cJSON *policy = cJSON_CreateObject();
    cJSON_AddItemToObject(policy, "tx_whitelist", cJSON_CreateArray());
    cJSON_AddNumberToObject(policy, "tx_power_cap_dbm", 20);
    cJSON_AddBoolToObject(policy, "tx_requires_token", true);
    cJSON_AddItemToObject(root, "policy", policy);

    cJSON *limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(limits, "ble_frag_total_max", 15);
    cJSON_AddNumberToObject(limits, "ble_frame_max", 3600);
    cJSON_AddNumberToObject(limits, "usb_frame_max", RFLAB_USB_FRAME_MAX);
    cJSON_AddNumberToObject(limits, "max_edges", 16384);
    cJSON_AddNumberToObject(limits, "stream_rate_hz", 50);
    cJSON_AddNumberToObject(limits, "arm_timeout_ms", 5000);
    cJSON_AddItemToObject(root, "limits", limits);

    send_json(MSG_CAPS, f->seq, root);
    cJSON_Delete(root);
}

static void handle_ping(const rflab_frame_t *f)
{
    copy_payload_json(f);
    cJSON *root = cJSON_Parse(s_json);
    cJSON *pong = cJSON_CreateObject();

    if (cJSON_IsObject(root)) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t_ms");
        if (cJSON_IsNumber(t)) {
            cJSON_AddNumberToObject(pong, "t_ms", t->valuedouble);
        }
    }
    if (cJSON_GetObjectItemCaseSensitive(pong, "t_ms") == NULL) {
        /* 没带 t_ms 也回一个,让 App 至少能拿到 RTT 的下半段。 */
        cJSON_AddNumberToObject(pong, "t_ms", 0);
    }
    send_json(MSG_PONG, f->seq, pong);
    cJSON_Delete(pong);
    if (root != NULL) {
        cJSON_Delete(root);
    }
}

static void dispatch(const rflab_frame_t *f)
{
    led_note_link_activity();

    /* 契约 §5 规则 1:IDLE(尚未完成 HELLO)下除 HELLO 外一切命令回 not_ready。
     * 旧实现在 IDLE 下仍接受 PING、未知类型回 unsupported,与契约不符(M-08)。
     * ⚠️ GET_STATUS(0x52) 本批仍未实现,HELLO 之后会落进 default ⇒
     *    unsupported。这是已知契约-实现缺口,记在 docs/03 §9。 */
    if (!s_hello_done && f->type != MSG_HELLO) {
        send_error(f->seq, "not_ready", "尚未完成 HELLO");
        return;
    }

    if (f->type & 0x80) {
        /* 二进制数据帧(DATA_CHUNK 等)本批未实现。 */
        send_error(f->seq, "unsupported", "二进制帧本批未实现");
        return;
    }

    switch (f->type) {
    case MSG_HELLO:
        handle_hello(f);
        break;
    case MSG_GET_CAPS:
        handle_get_caps(f);
        break;
    case MSG_PING:
        handle_ping(f);
        break;
    default:
        send_error(f->seq, "unsupported", "未知消息类型");
        break;
    }
}

/* ------------------------------------------------------------------ */

static void rf_kill_assert_at_boot(void)
{
    /* 安全默认 = 两个引脚输出 0:
     *   GPIO7 2.4G 总闸 = 0(软件不允许 2.4G)
     *   GPIO8 RF_KILL   = 0(硬件 KILL 有效)
     * M3 CE = AND(GPIO7, GPIO8) ⇒ CE=0 ⇒ nRF24 power-down、PA 不工作。
     * ⚠️ 先写输出锁存(此时脚还是输入,无输出)再切输出方向,消掉
     *    "配置成输出 → set_level"之间的抖动窗口(L-03 同类问题)。
     *    复位到本语句之间引脚是输入态,靠载板外部下拉保证 KILL(接线图 §6)。 */
    gpio_set_level(PIN_RF_GATE, 0);
    gpio_set_level(PIN_RF_KILL, 0);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_RF_GATE) | (1ULL << PIN_RF_KILL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_RF_GATE, 0);
    gpio_set_level(PIN_RF_KILL, 0);
    /* ⚠️ 本文件没有任何把 GPIO7/GPIO8 驱动为 1 的代码。发射门控本批只有"关"这一态。 */

    /* 配对按键:阶段 1 只做输入配置,不消费事件(避免造出永不执行的分支)。 */
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << PIN_PAIR_BTN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);
}

/* M1 Si446x 的 SDN 由 rf_spi_init() 在 EN 拉高 ≥20ms 后做 POR 脉冲:
 * 开漏 GPIO9 **释放**(置 1,靠外部 100k 上拉为高)≥100µs → 主动拉低(置 0)→
 * 等 ≥10ms(见 rf_spi.c)。开漏是安全关键:现场旧接线把 SDN 直接接 GND,推挽
 * 输出高会与 GND 短路;开漏在旧接线下来只是高阻,不会损坏引脚。
 * 上电到那之前 SDN 靠外部 100k 上拉保持 shutdown 态 —— 这是安全默认,
 * 不要在 main 里提前把它拉低。 */

static void make_device_id(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "RFLAB-%02X%02X", mac[4], mac[5]);
}

/* 控制通道初始化:UART0(CH343P)@921600 8N1。
 * 返回 esp_err_t 而不是 ESP_ERROR_CHECK:装配失败时设备仍要能点灯/进故障态,
 * 不能因为一条链路的问题让"安全默认"也变得不可观测。
 * ⚠️ 失败时 s_uart 保持 NULL —— 主循环必须判 NULL 后再用(M-07)。 */
static esp_err_t transport_init(void)
{
    /* 控制通道 = CH343P ↔ UART0。
     *
     * ⚠️ 为什么不用 IDF 的 uart 驱动(2026-10-22 实测):
     *    `uart_driver_install` 返回 0、`uart_write_bytes` 返回字节数、`uart_wait_tx_done` 返回 0,
     *    但**线上一个字节都没有**,RX 也收不到 —— 现象指向"驱动 ISR 没被服务",
     *    而 ROM 的 UART 输出始终正常。为避免在根因未明时把产品堵死,本版走 ROM 路径:
     *      TX = esp_rom_output_tx_one_char()(帧字节,原样)/ esp_rom_output_putc()(文本)
     *      RX = esp_rom_output_rx_one_char() 轮询
     *    波特率:2026-10-25 实测 ROM 路径同样能跑 **921600**(此前"只能 115200"的判断是错的
     *    —— 波特率由 uart_param_config 决定,ROM 输出不限制它)。注意复位后 ROM bootloader
     *    自己的横幅仍是 115200,进 App 后才切到 921600。 */
    const uart_config_t cfg = {
        .baud_rate  = RFLAB_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(RFLAB_UART_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(RFLAB_UART_PORT, PIN_UART0_TX, PIN_UART0_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    uart_dev_t *hw = UART_LL_GET_HW(RFLAB_UART_PORT);
    /* 清掉复位阶段残留的字节,保证首帧从干净边界开始。 */
    uint8_t drop;
    for (int i = 0; i < 256; i++) {
        if (esp_rom_output_rx_one_char(&drop) != 0) {
            break;
        }
    }
    s_uart = hw;
    return ESP_OK;
}

/* 启动横幅。⚠️ 为什么必须存在:控制台与 IDF 日志在本版是关闭的
 * (协议独占 UART0),若再没有这几行,设备出问题时就是"插上毫无反应",
 * 无法区分"没启动 / 启动失败 / 链路没起来"。
 *
 * ⚠️ M-04:横幅是**未成帧的 ASCII 文本**,里面含 'R'(0x52)= 帧 MAGIC。
 *    任何严格按契约 §2 实现的解析器都会在横幅里看到"假帧头"(0x52 后面跟着
 *    ASCII 'F' 等)。正确做法是:**校验 VER,失配时把当前字节重新处理**,
 *    不能简单丢弃(tools/bringup_test.py 已这样处理)。本版不改横幅文本,
 *    只把它写成"协议通道卫生"的已知事实,见 docs/03 §3。
 * ⚠️ 只允许出现在启动阶段。运行期**不得**再往这条流里写文本。 */

/* 线检结论:只陈述事实,不做"接线坏了"的因果推断。
 * cs_pu/cs_pd 是"CS 选中后,内部上拉/下拉各读一次"的两个电平:
 *   相等 → 读数被外部驱动(值即 cs_pu)
 *   不等 → 读数跟随内部上下拉 ⇒ 该时刻线上没有外部驱动源。
 * 不能据此断言 CS/MISO 没接:芯片未上电/未 POWER_UP 时 SDO 本来就是高阻。 */
static const char *drive_verdict(const rf_mod_t *m)
{
    if (m->cs_pu != m->cs_pd) {
        return "选中后 MISO 读数随上下拉变(线上无外部驱动)";
    }
    return m->cs_pu ? "选中后 MISO 被驱动为 HIGH" : "选中后 MISO 被驱动为 LOW";
}

/* 往横幅缓冲追加一行;放不下就整行丢弃,绝不发送半行(L-06)。 */
static bool banner_append(char *buf, size_t cap, size_t *off, const char *fmt, ...)
{
    if (*off >= cap) {
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= cap - *off) {
        return false;
    }
    *off += (size_t)k;
    return true;
}

static void send_banner(bool pairing_ok)
{
    /* 静态缓冲:主任务栈只有 8192 B,横幅不再占栈(L-06)。 */
    static char buf[2048];
    size_t off = 0;

    const char *wire_v = (s_wire.miso_pd == 0 && s_wire.miso_pu == 1)
                             ? "floating(MISO 未接到任何驱动)"
                             : (s_wire.miso_pd ? "driven-HIGH(被强拉高)" : "driven-LOW(被拉低)");
    const int gate = gpio_get_level(PIN_RF_GATE);   /* 0 = 2.4G 不允许 */
    const int kill = gpio_get_level(PIN_RF_KILL);   /* 0 = KILL 有效 */
    const int sdn  = gpio_get_level(PIN_M1_SDN);    /* 0 = 已释放(工作),1 = shutdown */
    bool ok = true;

    ok &= banner_append(buf, sizeof(buf), &off,
                        "\r\n# RF-Lab fw=%s proto=%d device=%s boot\r\n",
                        RFLAB_FW_VERSION, RFLAB_VER, s_device_id);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# carrier=CH343P->UART0 baud=%d (native USB: never enumerated)\r\n",
                        (int)RFLAB_UART_BAUD);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# wire MISO 14: pd=%d pu=%d -> %s\r\n",
                        s_wire.miso_pd, s_wire.miso_pu, wire_v);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# RF_KILL=%s pairing:%s\r\n",
                        kill ? "RELEASED(!)" : "asserted(禁止发射)",
                        pairing_ok ? "ok" : "FAILED");
    /* 门控链事实:CE 由 AND(GPIO7, GPIO8) 驱动,固件没有 CE GPIO。 */
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# gate 2.4G(GPIO7)=%d KILL(GPIO8)=%d -> M3 CE=AND(7,8)=%d\r\n",
                        gate, kill, gate & kill);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# sdn  M1(GPIO9)=%d (0=已释放/工作, 1=shutdown 或未配置)\r\n",
                        sdn);
    /* L-08:present=false 只有在 probe 真的跑过时才是"探过但没探到"。 */
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# probe ran=%d (0=SPI 初始化失败:present=false 表示**未探测**)\r\n",
                        s_probe_ran ? 1 : 0);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# probe M1 si4463   cts=0x%02X part=0x%04X present=%d\r\n",
                        s_probe.m[0].cts,
                        (unsigned)(s_probe.m[0].id1 | (s_probe.m[0].id2 << 8)),
                        (int)s_probe.m[0].present);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# line  M1 cs_pu=%d cs_pd=%d -> %s\r\n",
                        s_probe.m[0].cs_pu, s_probe.m[0].cs_pd,
                        drive_verdict(&s_probe.m[0]));
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# line  M2 cs_pu=%d cs_pd=%d -> %s\r\n",
                        s_probe.m[1].cs_pu, s_probe.m[1].cs_pd,
                        drive_verdict(&s_probe.m[1]));
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# line  M3 cs_pu=%d cs_pd=%d -> %s\r\n",
                        s_probe.m[2].cs_pu, s_probe.m[2].cs_pd,
                        drive_verdict(&s_probe.m[2]));
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# spi   M1 pullup=0x%02X pulldown=0x%02X -> %s\r\n",
                        s_probe.m[0].spi_up, s_probe.m[0].spi_down,
                        (s_probe.m[0].spi_up != s_probe.m[0].spi_down)
                            ? "读回随上下拉变" : "读回不随上下拉变");
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# spi   M2 pullup=0x%02X pulldown=0x%02X -> %s\r\n",
                        s_probe.m[1].spi_up, s_probe.m[1].spi_down,
                        (s_probe.m[1].spi_up != s_probe.m[1].spi_down)
                            ? "读回随上下拉变" : "读回不随上下拉变");
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# spi   M3 pullup=0x%02X pulldown=0x%02X -> %s\r\n",
                        s_probe.m[2].spi_up, s_probe.m[2].spi_down,
                        (s_probe.m[2].spi_up != s_probe.m[2].spi_down)
                            ? "读回随上下拉变" : "读回不随上下拉变");
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# probe M2 cc1101   partnum=0x%02X version=0x%02X present=%d\r\n",
                        s_probe.m[1].id0, s_probe.m[1].id1,
                        (int)s_probe.m[1].present);
    ok &= banner_append(buf, sizeof(buf), &off,
                        "# probe M3 nrf24l01 config=0x%02X status=0x%02X present=%d\r\n",
                        s_probe.m[2].id0, s_probe.m[2].id1,
                        (int)s_probe.m[2].present);

    /* M2 的三个 RSSI 采样点:证明"调谐 + 接收链路"真的在工作,不只是 SPI 通。 */
    for (int i = 0; ok && i < RF_PROBE_POINTS; i++) {
        if (!s_probe.m[1].rssi_ok[i]) {
            continue;
        }
        ok &= banner_append(buf, sizeof(buf), &off,
                            "# rssi %.3fMHz = %d dBm (raw=0x%02X)\r\n",
                            (double)s_probe.m[1].freq_hz[i] / 1e6,
                            s_probe.m[1].rssi_dbm[i], s_probe.m[1].rssi_raw[i]);
    }

    ok &= banner_append(buf, sizeof(buf), &off,
                        "# loop  MOSI13->MISO14 hi=%d lo=%d -> %s\r\n",
                        s_probe.loop_hi, s_probe.loop_lo,
                        (s_probe.loop_hi == 1 && s_probe.loop_lo == 0)
                            ? "PASS(13-14 已短接,ESP32 侧 SPI 正常)"
                            : "FAIL(未短接 13-14,或 GPIO13/14 损坏)");

    if (off > 0) {
        ok &= banner_append(buf, sizeof(buf), &off, "# ready\r\n");
        (void)ok;
        tx_raw(buf, off);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot: fw=%s proto=%d", RFLAB_FW_VERSION, RFLAB_VER);

    /* 1. 先立安全默认:GPIO7(2.4G 总闸)与 GPIO8(RF_KILL)都拉低。
     *    放在任何可能耗时/阻塞的初始化之前。 */
    rf_kill_assert_at_boot();
    ESP_LOGI(TAG, "step1: gate(GPIO7)=0 RF_KILL(GPIO8)=0 -> M3 CE=0");

    s_tx_lock = xSemaphoreCreateMutex();

    /* 2. 本地指示:先写黑(KILL 态),再按状态切换。 */
    led_init();
    ESP_LOGI(TAG, "step2a: led_init ok");
#ifdef RFLAB_LED_SELFTEST
    /* 载板正品检查(TC-106 / R-18):能看见红→绿→蓝 = 2812 信号已焊。
     * ⚠️ 默认构建**不打开**(见 platformio.ini 的手动开启说明)。 */
    led_selftest();
    ESP_LOGI(TAG, "step2b: led_selftest ok");
#endif
    led_set_state(LED_STATE_KILL); /* 函数内部会立即显式写黑 */
    led_start();
    ESP_LOGI(TAG, "step2c: led task started");

    make_device_id();
    ESP_LOGI(TAG, "step2d: device_id=%s", s_device_id);

    /* 3. 控制通道(本批唯一链路):CH343P → UART0。
     *    RX 空闲电平必须在 uart_set_pin 之前读(之后 GPIO44 归 UART 外设);
     *    文本标记则在 transport_init 之后打,这样它们才是 921600 可读的(L-07)。 */
    int rx_idle = 0;
    {
        /* 诊断:UART0 RX(GPIO44)空闲时应为高电平。若被外部拉低/拉高,
         * 说明有东西接在这条线上(接线冲突)。 */
        gpio_config_t in = {
            .pin_bit_mask = (1ULL << PIN_UART0_RX),
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&in);
        vTaskDelay(pdMS_TO_TICKS(2));
        rx_idle = gpio_get_level(PIN_UART0_RX);
    }
    esp_err_t err = transport_init();
    /* 从这里开始的 ROM 标记才是 921600;复位后 ROM bootloader 自己那几行是 115200。 */
    mark0("[0]start\r\n");
    mark0(rx_idle ? "[rx-idle-high]\r\n" : "[rx-idle-LOW]\r\n");
    if (err != ESP_OK) {
        /* M-07:链路配置失败只降级为故障指示,主循环会走 NULL 保护的空转分支。 */
        led_set_state(LED_STATE_FAULT);
        ESP_LOGE(TAG, "transport_init 失败: %s", esp_err_to_name(err));
    }

    /* 4. 配对状态(内部含 NVS 初始化)。 */
    mark0("[1]transport\r\n");
    bool pairing_ok = (pairing_init() == ESP_OK);
    if (!pairing_ok) {
        led_set_state(LED_STATE_FAULT);
    }

    /* 5. 射频总线 + 三模块探测(阶段 2 第一步:先证明接线通,再谈驱动)。
     *    探测全程只读、不发射;M3 的 CE=AND(GPIO7,GPIO8)=0 ⇒ power-down。
     *    ⚠️ 未连接的模块报 present=false 是预期。 */
    mark0("[2]pairing\r\n");
    /* 先判 MISO 有没有接到东西,再做 SPI —— 否则三块全 0xFF 时无从分辨原因。 */
    rf_wire_check(&s_wire);
#ifdef RFLAB_WIRE_DEBUG
    /* 现场接线诊断(仅 bring-up 诊断构建,默认关闭):不发射,但**会**给 M1 发
     * POWER_UP、并把三路 EN 提前拉高 —— 会改变上电时序,所以不能在正式构建里混用。
     * 必须在 rf_spi_init() 之前 —— 它要临时把 SCK/MOSI/MISO/CS 当普通 GPIO 用。 */
    wire_diag_run();
#endif
    esp_err_t rf_err = rf_spi_init();
    mark0("[3]spi\r\n");
    if (rf_err == ESP_OK) {
        rf_probe_run(&s_probe);
        s_probe_ran = true;
    } else {
        led_set_state(LED_STATE_FAULT);
        s_probe_ran = false;
        ESP_LOGE(TAG, "rf_spi_init 失败: %s", esp_err_to_name(rf_err));
    }

    mark0("[4]probe done\r\n");
    /* 6. 启动横幅(此时的状态就是真实初始化结果)。链路没配好就不发。 */
    if (err == ESP_OK) {
        send_banner(pairing_ok);
    }

    mark0("[5]banner sent\r\n");
    rflab_rx_t rx;
    rflab_rx_init(&rx);
    int64_t last_rx_us = esp_timer_get_time();

    for (;;) {
        /* M-07:transport_init 失败时 s_uart == NULL,绝不能解引用。
         * 这里保持"无害空转 + FAULT 灯",不重启、不崩溃。 */
        if (s_uart == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bool got = false;
        for (int guard = 0; guard < 256; guard++) {
            /* 直接读 UART0 的 RX FIFO:不依赖驱动 ISR,也不依赖 ROM 控制台的 RX 通道。
             * ⚠️ L-14:这是纯轮询、且硬件 RX FIFO 只有 128 B;主机在长应答期间
             *    持续发帧时可能溢出丢字节(靠 MAGIC 重同步恢复)。彻底解决要装
             *    UART 驱动/中断,需先解决 docs/03 §3 记录的驱动异常。 */
            uint8_t c;
            if (uart_ll_get_rxfifo_len(s_uart) == 0) {
                break;
            }
            uart_ll_read_rxfifo(s_uart, &c, 1);
            got = true;
            last_rx_us = esp_timer_get_time();
            int rc = rflab_rx_feed(&rx, c, &s_rx_frame);
            if (rc == RFLAB_RX_FRAME) {
                dispatch(&s_rx_frame);
            } else if (rc == RFLAB_RX_BAD_VER) {
                send_error(0, "version_mismatch", "帧头 VER != 1");
            } else if (rc == RFLAB_RX_TOO_BIG) {
                /* L-05:TOO_BIG 时 SEQ 已经收到(rx.seq),不要固定回 seq_ref=0;
                 * 并在 msg 里给出当前承载上限,避免 App 只能猜。 */
                char msg[64];
                snprintf(msg, sizeof(msg), "LEN 超过 usb_frame_max(%u)",
                         (unsigned)RFLAB_USB_FRAME_MAX);
                send_error(rx.seq, "frame_too_large", msg);
            }
        }
        if (got) {
            /* L-14:持续有数据时也主动让出 CPU,避免饿死 IDLE/LED 任务。 */
            vTaskDelay(1);
            continue;
        }

        /* L-04:字节间超时重同步。截断帧会让状态机停在"收载荷",把后续新帧
         * 当成载荷吞掉;超过 200ms 没有新字节就复位到等 MAGIC。 */
        if (rflab_rx_in_progress(&rx) &&
            (esp_timer_get_time() - last_rx_us) > (int64_t)RFLAB_RX_BYTE_GAP_MS * 1000) {
            rflab_rx_reset(&rx);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
