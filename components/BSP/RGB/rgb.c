#include "rgb.h"


// 引脚定义
#define WS2812_PIN 42
#define NUM_LEDS   1   // 单个 LED 测试，可改为更多

// 时序参数（单位：ns）
#define T0H_DUR  350   // 0 码高电平 350ns
#define T0L_DUR  800   // 0 码低电平 800ns
#define T1H_DUR  700   // 1 码高电平 700ns
#define T1L_DUR  600   // 1 码低电平 600ns

// RMT 配置
#define RMT_CHANNEL RMT_CHANNEL_0
#define RMT_CLK_DIV 1   // 80MHz / 1 = 80MHz，精度 12.5ns

// 计算 RMT 占用的 tick 数（1 tick = 12.5ns）
static inline uint32_t ns_to_ticks(uint32_t ns) {
  return (ns + 12.5/2) / 12.5;  // 四舍五入
}

rmt_item32_t led_bit_zero;
rmt_item32_t led_bit_one;

// 初始化 RMT
void ws2812_rmt_init() {
  rmt_config_t config = RMT_DEFAULT_CONFIG_TX(WS2812_PIN, RMT_CHANNEL);
  config.clk_div = RMT_CLK_DIV;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;  // 复位电平为低
  
  rmt_config(&config);
  rmt_driver_install(RMT_CHANNEL, 0, 0);
  
  // 预计算 bit0 和 bit1 的 RMT 波形
  led_bit_zero = (rmt_item32_t) {
    .duration0 = ns_to_ticks(T0H_DUR),
    .level0 = 1,
    .duration1 = ns_to_ticks(T0L_DUR),
    .level1 = 0
  };
  led_bit_one = (rmt_item32_t) {
    .duration0 = ns_to_ticks(T1H_DUR),
    .level0 = 1,
    .duration1 = ns_to_ticks(T1L_DUR),
    .level1 = 0
  };
}

// 将一个字节转换为 8 个 RMT 波形单元
void byte_to_rmt_items(uint8_t byte, rmt_item32_t *items) {
  for (int i = 0; i < 8; i++) {
    bool bit = (byte >> (7 - i)) & 0x01;  // 高位先发
    items[i] = bit ? led_bit_one : led_bit_zero;
  }
}

// 发送颜色数据 (GRB 顺序)
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
  rmt_item32_t items[NUM_LEDS * 24];
  
  for (int led = 0; led < NUM_LEDS; led++) {
    // 注意：WS2812B 顺序是 GRB
    byte_to_rmt_items(g, &items[led * 24]);
    byte_to_rmt_items(r, &items[led * 24 + 8]);
    byte_to_rmt_items(b, &items[led * 24 + 16]);
  }
  
  // 发送数据
  rmt_write_items(RMT_CHANNEL, items, NUM_LEDS * 24, true);
  
  // 发送复位脉冲：至少 280µs 低电平
  // RMT 空闲电平已经是低，只需等待足够长的时间
  // 但更好做法是主动加一个长低电平波形
  rmt_item32_t reset_item = {
    .duration0 = ns_to_ticks(300000),  // 300µs
    .level0 = 0,
    .duration1 = 0,
    .level1 = 0
  };
  rmt_write_items(RMT_CHANNEL, &reset_item, 1, true);
  rmt_wait_tx_done(RMT_CHANNEL, portMAX_DELAY);
}

void rgb_init(void) {
  ws2812_rmt_init();
  ws2812_set_color(0, 0, 0);
}

// void loop() {
//   // 红色
//   ws2812_set_color(255, 0, 0);
//   delay(1000);
//   // 绿色
//   ws2812_set_color(0, 255, 0);
//   delay(1000);
//   // 蓝色
//   ws2812_set_color(0, 0, 255);
//   delay(1000);
// }