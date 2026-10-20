/* Device HAL: NVS, Wi-Fi, AXP2101 battery + PWR key, ES8311 audio synth, BOOT button. */
#include "hal.h"
#include "board.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "wifi_secret.h"

static const char *TAG = "hal";
/* ---------- NVS ---------- */
static nvs_handle_t nvs;
int32_t hal_kv_get(const char *k, int32_t d) { int32_t v; return (nvs && nvs_get_i32(nvs, k, &v) == ESP_OK) ? v : d; }
void hal_kv_set(const char *k, int32_t v) { if (nvs) { nvs_set_i32(nvs, k, v); nvs_commit(nvs); } }

/* ---------- I2C / AXP2101 ---------- */
static esp_err_t axp_rd(uint8_t r, uint8_t *v) { return i2c_master_write_read_device(I2C_PORT, ADDR_AXP2101, &r, 1, v, 1, pdMS_TO_TICKS(20)); }
static esp_err_t axp_wr(uint8_t r, uint8_t v) { uint8_t b[2] = { r, v }; return i2c_master_write_to_device(I2C_PORT, ADDR_AXP2101, b, 2, pdMS_TO_TICKS(20)); }
void hal_i2c_init(void) {
    i2c_config_t c = { .mode = I2C_MODE_MASTER, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = 1, .scl_pullup_en = 1, .master.clk_speed = 400000 };
    i2c_param_config(I2C_PORT, &c); i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}
void hal_axp_init(void) {
    uint8_t v;
    if (axp_rd(0x18, &v) == ESP_OK) axp_wr(0x18, v | 0x08);   /* fuel gauge on */
    axp_wr(0x40, 0x00); axp_wr(0x42, 0x00);
    axp_wr(0x41, 0x03);                                        /* IRQ en 2: PKEY positive/negative edge */
    axp_wr(0x48, 0xFF); axp_wr(0x49, 0xFF); axp_wr(0x4A, 0xFF); /* clear */
}
static hal_batt_t batt_cache;
void hal_batt_get(hal_batt_t *o) { *o = batt_cache; }
void hal_batt_poll(void) {
    uint8_t s1 = 0, s2 = 0, p = 0;
    axp_rd(0x00, &s1); axp_rd(0x01, &s2); axp_rd(0xA4, &p);
    batt_cache.usb = s1 & 0x20;
    batt_cache.charging = ((s2 >> 5) & 0x3) == 1;
    batt_cache.percent = p > 100 ? 100 : p;
}
/* returns bitmask: 1 = key pressed (neg edge), 2 = key released (pos edge) */
int hal_pkey_poll(void) {
    uint8_t s = 0; if (axp_rd(0x49, &s) != ESP_OK || !(s & 0x03)) return 0;
    axp_wr(0x49, s & 0x03);
    return ((s & 0x02) ? 1 : 0) | ((s & 0x01) ? 2 : 0);
}
void hal_power_off(void) { uint8_t v = 0; axp_rd(0x10, &v); axp_wr(0x10, v | 0x01); }

bool hal_boot_pressed(void) { return gpio_get_level(PIN_BOOT) == 0; }

/* ---------- Wi-Fi ---------- */
static volatile bool wifi_up;
static void wifi_ev(void *a, esp_event_base_t b, int32_t id, void *d) {
    if (b == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) { wifi_up = false; esp_wifi_connect(); }
    else if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) wifi_up = true;
}
void hal_wifi_init(void) {
    esp_netif_init(); esp_event_loop_create_default(); esp_netif_create_default_wifi_sta();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&ic);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_ev, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_ev, NULL);
    wifi_config_t wc = { 0 };
    strcpy((char *)wc.sta.ssid, "WAGNERNET");
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof wc.sta.password - 1);
    esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_start();
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1); tzset();
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org"); esp_netif_sntp_init(&sc);
}
void hal_wifi_get(hal_wifi_t *o) {
    memset(o, 0, sizeof *o);
    wifi_ap_record_t ap;
    if (wifi_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        o->connected = true; strncpy(o->ssid, (char *)ap.ssid, 32); o->rssi = ap.rssi;
    }
}
bool hal_time_hm(int *h, int *m) {
    time_t t = time(NULL); if (t < 1700000000) return false;
    struct tm tm; localtime_r(&t, &tm); *h = tm.tm_hour; *m = tm.tm_min; return true;
}

/* ---------- Audio: tiny square/triangle synth -> ES8311 ---------- */
#define SR 16000
static i2s_chan_handle_t tx;
static QueueHandle_t sfxq;
static volatile bool music_on;
typedef struct { uint8_t midi, len; } note_t; /* len in 1/16 notes at 150 bpm */
static const note_t THEME[] = { /* Korobeiniki */
 {76,4},{71,2},{72,2},{74,4},{72,2},{71,2},{69,4},{69,2},{72,2},{76,4},{74,2},{72,2},{71,6},{72,2},{74,4},{76,4},
 {72,4},{69,4},{69,8},{0,2},{74,4},{77,2},{81,4},{79,2},{77,2},{76,6},{72,2},{76,4},{74,2},{72,2},{71,4},{71,2},
 {72,2},{74,4},{76,4},{72,4},{69,4},{69,8} };
static const note_t SFX[][4] = {
    [SFX_MOVE] = {{84,1}}, [SFX_ROTATE] = {{88,1}}, [SFX_LOCK] = {{60,1}}, [SFX_HOLD] = {{79,1},{83,1}},
    [SFX_CLEAR] = {{72,1},{76,1},{79,2}}, [SFX_TETRIS] = {{72,1},{76,1},{79,1},{84,4}},
    [SFX_LEVEL] = {{79,2},{84,2},{88,3}}, [SFX_GAMEOVER] = {{67,3},{63,3},{60,6}},
};
static float hz(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }
void hal_sfx(hal_sfx_t s) { if (sfxq) xQueueSend(sfxq, &s, 0); }
void hal_music(bool on) { music_on = on; }
static void audio_task(void *a) {
    int16_t buf[256];
    const int sixteenth = SR * 60 / 150 / 4;
    int mi = 0, m_left = 0; float m_ph = 0, m_f = 0;
    const note_t *sn = NULL; int si = 0, s_left = 0; float s_ph = 0, s_f = 0;
    for (;;) {
        hal_sfx_t s;
        if (xQueueReceive(sfxq, &s, 0)) { sn = SFX[s]; si = 0; s_left = 0; }
        for (int i = 0; i < 256; i++) {
            int32_t v = 0;
            if (music_on) {
                if (m_left <= 0) { mi = (mi + 1) % (sizeof THEME / sizeof THEME[0]); m_f = THEME[mi].midi ? hz(THEME[mi].midi) : 0; m_left = THEME[mi].len * sixteenth; }
                if (m_f) { m_ph += m_f / SR; if (m_ph >= 1) m_ph -= 1; float tri = m_ph < .5f ? 4 * m_ph - 1 : 3 - 4 * m_ph;
                    float env = m_left > 400 ? 1 : m_left / 400.0f; v += (int32_t)(tri * 2500 * env); }
                m_left--;
            }
            if (sn) {
                if (s_left <= 0) { if (si >= 4 || !sn[si].len) { sn = NULL; goto out; } s_f = hz(sn[si].midi); s_left = sn[si].len * SR / 25; si++; }
                s_ph += s_f / SR; if (s_ph >= 1) s_ph -= 1;
                v += (s_ph < .5f ? 3000 : -3000) * (s_left > 200 ? 1 : s_left / 200.0f);
                s_left--;
            }
        out:
            buf[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        }
        size_t w; i2s_channel_write(tx, buf, sizeof buf, &w, portMAX_DELAY);
    }
}
void hal_audio_init(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    i2s_new_channel(&cc, &tx, NULL);
    i2s_std_config_t sc = { .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SR),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_LRCK, .dout = PIN_I2S_DOUT, .din = -1 } };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_channel_init_std_mode(tx, &sc); i2s_channel_enable(tx);
    es8311_handle_t es = es8311_create(I2C_PORT, ADDR_ES8311);
    es8311_clock_config_t ck = { .mclk_from_mclk_pin = true, .mclk_frequency = SR * 256, .sample_frequency = SR };
    if (es8311_init(es, &ck, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) ESP_LOGW(TAG, "es8311 init failed");
    es8311_voice_volume_set(es, 65, NULL);
    gpio_set_direction(PIN_PA_EN, GPIO_MODE_OUTPUT); gpio_set_level(PIN_PA_EN, 1);
    sfxq = xQueueCreate(8, sizeof(hal_sfx_t));
    xTaskCreatePinnedToCore(audio_task, "audio", 3072, NULL, 4, NULL, 1);
}
void hal_nvs_init(void) {
    if (nvs_flash_init() != ESP_OK) { nvs_flash_erase(); nvs_flash_init(); }
    nvs_open("games", NVS_READWRITE, &nvs);
    gpio_config_t g = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 }; gpio_config(&g);
}
