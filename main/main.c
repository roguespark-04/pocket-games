/* Pocket Games: display + touch + LVGL loop + power key handling. */
#include "board.h"
#include "hal.h"
#include "app.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"

void hal_i2c_init(void); void hal_axp_init(void); void hal_batt_poll(void); int hal_pkey_poll(void);
void hal_power_off(void); void hal_ota_boot(void); void hal_ota_mark_valid(void); void hal_wifi_init(void); void hal_audio_init(void); void hal_nvs_init(void);

#define BUF_LINES 12  /* even: CO5300 needs even-aligned windows */
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static lv_display_t *disp;
static bool sleeping;

static bool trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    lv_display_flush_ready((lv_display_t *)ctx); return false;
}
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));
    esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}
static void rounder(lv_event_t *e) {  /* CO5300: start even, size even */
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1; a->x2 |= 1; a->y2 |= 1;
}
static void touch_read(lv_indev_t *in, lv_indev_data_t *d) {
    uint16_t x, y; uint8_t n = 0;
    esp_lcd_touch_read_data(touch);
    if (!sleeping && esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &n, 1) && n) {
        d->point.x = x; d->point.y = y; d->state = LV_INDEV_STATE_PRESSED;
    } else d->state = LV_INDEV_STATE_RELEASED;
}
static uint32_t tick_cb(void) { return esp_timer_get_time() / 1000; }

static void display_init(void) {
    spi_bus_config_t bc = CO5300_PANEL_BUS_QSPI_CONFIG(PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
                                                       LCD_RES * BUF_LINES * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ioc = CO5300_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, trans_done, NULL);
    disp = lv_display_create(LCD_RES, LCD_RES);
    ioc.user_ctx = disp;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));
    co5300_vendor_config_t vc = { .flags.use_qspi_interface = 1 };
    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = PIN_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                      .bits_per_pixel = 16, .vendor_config = &vc };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io, &pc, &panel));
    esp_lcd_panel_reset(panel); esp_lcd_panel_init(panel);
    esp_lcd_panel_set_gap(panel, LCD_X_GAP, 0);
    esp_lcd_panel_disp_on_off(panel, true);
    esp_lcd_panel_co5300_set_brightness(panel, 80);

    size_t sz = LCD_RES * BUF_LINES * 2;   /* small internal DMA strips (2 x 12 lines) */
    void *b1 = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, b1, b2, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    lv_display_add_event_cb(disp, rounder, LV_EVENT_INVALIDATE_AREA, NULL);

    esp_lcd_panel_io_handle_t tio;
    esp_lcd_panel_io_i2c_config_t tc = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_PORT, &tc, &tio));
    esp_lcd_touch_config_t tcfg = { .x_max = LCD_RES, .y_max = LCD_RES, .rst_gpio_num = PIN_TP_RST, .int_gpio_num = PIN_TP_INT };
    if (esp_lcd_touch_new_i2c_cst9217(tio, &tcfg, &touch) == ESP_OK) {
        lv_indev_t *in = lv_indev_create(); lv_indev_set_type(in, LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(in, touch_read);
    } else ESP_LOGE("main", "touch init failed");
}
static void set_sleep(bool s) {
    sleeping = s;
    esp_lcd_panel_co5300_set_brightness(panel, s ? 0 : 80);
    esp_lcd_panel_disp_on_off(panel, !s);
    app_set_sleep(s);
}
/* PWR: quick press toggles screen sleep; hold ~2 s powers off (hint shown after 600 ms). */
static void power_poll(void) {
    static int64_t down_at; static bool down, hinted;
    int ev = hal_pkey_poll(); int64_t now = esp_timer_get_time() / 1000;
    if (ev & 1) { down = true; down_at = now; hinted = false; }
    if (down && !hinted && now - down_at > 600) { hinted = true; if (sleeping) set_sleep(false); app_power_hint(true); lv_refr_now(NULL); }
    if (down && now - down_at > 2000) { app_power_hint(true); lv_refr_now(NULL); vTaskDelay(pdMS_TO_TICKS(300)); hal_power_off(); }
    if (ev & 2) {
        if (!down || now - down_at <= 600) set_sleep(!sleeping);  /* short press (or release seen alone) */
        if (hinted) app_power_hint(false);
        down = false;
    }
}
static void ui_task(void *a) {
    int64_t last_status = 0, last_pk = 0, last_ota = 0; bool marked = false;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_pk >= 20) { last_pk = now; power_poll(); }
        if (now - last_status >= 1000) { last_status = now; hal_batt_poll(); app_refresh_status(); }
        if (now - last_ota >= 200) { last_ota = now; app_ota_tick(); }
        uint32_t d = lv_timer_handler();
        if (!marked && now > 8000) { marked = true; hal_ota_mark_valid(); }  /* UI up and stable for 8 s (Wi-Fi, audio, first frames) */
        vTaskDelay(pdMS_TO_TICKS(d < 2 ? 2 : d > 10 ? 10 : d));
    }
}
void app_main(void) {
    hal_nvs_init();
    hal_wifi_init();          /* Wi-Fi first: it needs internal RAM before the display grabs DMA buffers */
    hal_i2c_init(); hal_axp_init(); hal_batt_poll();
    lv_init(); lv_tick_set_cb(tick_cb);
    display_init();
    hal_audio_init();
    app_init();
    hal_ota_boot();
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 1);
}
