#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Moonraker 轮询解析模块（零第三方依赖）。
 *
 * 设计目标：
 * - 后台线程轮询 Moonraker HTTP API，解析并缓存关键字段
 * - LVGL/UI 线程只读缓存（getter），不做网络 I/O
 *
 * base_url 示例：
 * - "http://127.0.0.1:7125"
 * - "http://klipper.local:7125"
 *
 * api_key 可为 nullptr 或空串（Moonraker 未启用鉴权时）。
 *
 * 自动发现：
 * - 当 base_url 为 nullptr/空串/"auto" 时，模块会尝试探测常见 Moonraker 地址并自动选择可用项
 * - 典型场景：WSL2 下 Moonraker 跑在 Windows 宿主机，自动发现会优先尝试 WSL2 的 nameserver IP
 */
void mrker_configure(const char* base_url, const char* api_key);

/** 启动后台轮询线程（重复调用安全）。 */
void mrker_start(void);

/** 停止后台轮询线程（重复调用安全）。 */
void mrker_stop(void);

/** 当前使用的 base_url（用于调试显示）。 */
const char* mrker_base_url(void);

/** 获取最近一次轮询是否成功（网络可达且解析到有效响应）。 */
bool mrker_is_connected(void);

/** 最近一次 HTTP 状态码；未请求/失败为 -1。 */
int mrker_last_http_code(void);

/** 最近一次成功更新时间（`lv_tick_get()` 的值）；从未成功则为 0。 */
uint32_t mrker_last_ok_tick(void);

/** 打印进度 0..1；未知则为 -1。 */
float mrker_print_progress01(void);

/** 当前打印状态（Moonraker print_stats.state），如 "printing"/"paused"/"complete"；未知为空串。 */
const char* mrker_print_state(void);

/** 当前文件名（Moonraker print_stats.filename）；未知为空串。 */
const char* mrker_print_filename(void);

/** 屏显消息（Moonraker display_status.message）；未知为空串。 */
const char* mrker_display_message(void);

/** 挤出头温度/目标温度（extruder.temperature/target）；未知为 NAN。 */
float mrker_extruder_temp(void);
float mrker_extruder_target(void);

/** 热床温度/目标温度（heater_bed.temperature/target）；未知为 NAN。 */
float mrker_bed_temp(void);
float mrker_bed_target(void);

#ifdef __cplusplus
} // extern "C"
#endif

