/* Wi-Fi firmware updates. Manifest (JSON: {"version":"0.2.0","url":"https://.../pocket-games-0.2.0.bin"}) at
 * CONFIG_PG_OTA_MANIFEST_URL. Never installs on its own: boot check only sets a prompt flag. */
#include "hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "ota";
static hal_ota_t st;
static char bin_url[256];
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool busy;

void hal_ota_get(hal_ota_t *o) { taskENTER_CRITICAL(&mux); *o = st; taskEXIT_CRITICAL(&mux); }
void hal_ota_ack(void) { st.prompt = false; if (st.rolled_back) { st.rolled_back = false; hal_kv_set("ota_rb_seen", 1); } }
static void set_state(hal_ota_state_t s) { st.state = s; }
static void fail(const char *msg) { snprintf(st.error, sizeof st.error, "%s", msg); set_state(OTA_FAILED); ESP_LOGW(TAG, "%s", msg); }

/* semver "a.b.c" compare */
static int vcmp(const char *a, const char *b) {
    int x[3] = {0}, y[3] = {0};
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]); sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
static bool http_cfg_tls(esp_http_client_config_t *c) {
    c->crt_bundle_attach = esp_crt_bundle_attach; c->timeout_ms = 15000;
    c->buffer_size = 4096; c->buffer_size_tx = 4096;   /* GitHub's release-asset redirect URL is ~920 chars */
    c->max_redirection_count = 6; c->keep_alive_enable = true;
    return true;
}
static void do_check(bool from_boot) {
    hal_wifi_t w;
    for (int i = 0; i < (from_boot ? 60 : 10); i++) { hal_wifi_get(&w); if (w.connected) break; vTaskDelay(pdMS_TO_TICKS(500)); }
    if (!w.connected) { set_state(OTA_NO_WIFI); return; }
    set_state(OTA_CHECKING);
    ESP_LOGI(TAG, "check: free internal %u, largest %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_http_client_config_t c = { .url = CONFIG_PG_OTA_MANIFEST_URL };
    http_cfg_tls(&c);
    esp_http_client_handle_t h = esp_http_client_init(&c);
    char *body = heap_caps_calloc(1, 2048, MALLOC_CAP_SPIRAM);
    int len = -1;
    esp_err_t oe = ESP_FAIL;
    if (h && body && (oe = esp_http_client_open(h, 0)) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        int code = esp_http_client_get_status_code(h);
        /* follow GitHub's release-asset redirects */
        for (int r = 0; r < 5 && (code == 301 || code == 302 || code == 303 || code == 307 || code == 308); r++) {
            esp_http_client_set_redirection(h); esp_http_client_close(h);
            if (esp_http_client_open(h, 0) != ESP_OK) break;
            esp_http_client_fetch_headers(h); code = esp_http_client_get_status_code(h);
        }
        if (code == 200) {   /* read until EOF: works for chunked and unknown length */
            len = 0; int r;
            while (len < 2047 && (r = esp_http_client_read(h, body + len, 2047 - len)) > 0) len += r;
            if (len == 0) ESP_LOGW(TAG, "manifest: empty body");
        } else { ESP_LOGW(TAG, "manifest HTTP %d", code); len = -2; }
    }
    else ESP_LOGW(TAG, "manifest open failed: %s", esp_err_to_name(oe));
    if (h) esp_http_client_cleanup(h);
    if (len <= 0) { free(body); fail(len == -2 ? "Server error" : "Couldn't reach update server"); return; }
    body[len] = 0;
    cJSON *j = cJSON_Parse(body); free(body);
    const cJSON *v = j ? cJSON_GetObjectItem(j, "version") : NULL, *u = j ? cJSON_GetObjectItem(j, "url") : NULL;
    if (!cJSON_IsString(v) || !cJSON_IsString(u)) { cJSON_Delete(j); fail("Bad update manifest"); return; }
    char ver[16]; snprintf(ver, sizeof ver, "%s", v->valuestring);
    snprintf(bin_url, sizeof bin_url, "%s", u->valuestring);
    cJSON_Delete(j);
    /* A version that already failed to boot here is never offered again (prevents rollback loops). */
    char bad[16] = ""; int32_t bv = hal_kv_get("ota_bad", 0);
    if (bv) snprintf(bad, sizeof bad, "%ld.%ld.%ld", (long)(bv >> 16), (long)((bv >> 8) & 0xFF), (long)(bv & 0xFF));
    if (vcmp(ver, st.current) > 0 && strcmp(ver, bad) != 0) {
        snprintf(st.available, sizeof st.available, "%s", ver);
        set_state(OTA_AVAILABLE); if (from_boot) st.prompt = true;
        ESP_LOGI(TAG, "check result: update %s available (running %s)", ver, st.current);
    } else { set_state(OTA_UPTODATE); ESP_LOGI(TAG, "check result: up to date (running %s, latest %s%s)", st.current, ver, strcmp(ver, bad) ? "" : ", skipped: failed before"); }
}
static void do_install(void) {
    set_state(OTA_INSTALLING); st.progress = 0;
    esp_http_client_config_t c = { .url = bin_url }; http_cfg_tls(&c);
    esp_https_ota_config_t oc = { .http_config = &c };
    esp_https_ota_handle_t oh = NULL;
    esp_err_t be = esp_https_ota_begin(&oc, &oh);
    if (be != ESP_OK) { ESP_LOGW(TAG, "ota begin: %s", esp_err_to_name(be)); fail("Download failed"); return; }
    esp_app_desc_t nd;
    if (esp_https_ota_get_img_desc(oh, &nd) == ESP_OK && strcmp(nd.version, st.available) != 0)
        ESP_LOGW(TAG, "image reports %s, manifest said %s", nd.version, st.available);
    int total = esp_https_ota_get_image_size(oh);
    esp_err_t e;
    while ((e = esp_https_ota_perform(oh)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(oh);
        if (total > 0) st.progress = got * 100 / total;
    }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(oh)) { esp_https_ota_abort(oh); ESP_LOGW(TAG, "ota perform: %s", esp_err_to_name(e)); fail("Download interrupted"); return; }
    if (esp_https_ota_finish(oh) != ESP_OK) { fail("Update didn't verify"); return; }   /* image hash/header checked here */
    int a, b, d; sscanf(st.available, "%d.%d.%d", &a, &b, &d);
    hal_kv_set("ota_try", (a << 16) | (b << 8) | d);   /* remembered in case it fails to boot */
    hal_kv_set("ota_rb_seen", 0);
    st.progress = 100; set_state(OTA_RESTARTING);
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}
typedef enum { JOB_BOOT, JOB_CHECK, JOB_INSTALL } job_t;
static void job(void *arg) {
    job_t j = (job_t)(intptr_t)arg;
    if (j == JOB_INSTALL) do_install(); else do_check(j == JOB_BOOT);
    busy = false; vTaskDelete(NULL);
}
static void spawn(job_t j) {
    if (busy) return; busy = true;
    /* TLS runs in this task: 8 KB stack in internal RAM, mbedTLS buffers go to PSRAM (see sdkconfig.defaults). */
    if (xTaskCreatePinnedToCore(job, "ota", 8192, (void *)(intptr_t)j, 3, NULL, 0) != pdPASS) { busy = false; fail("Not enough memory"); }
}
void hal_ota_check(void) { spawn(JOB_CHECK); }
void hal_ota_install(void) { if (st.state == OTA_AVAILABLE) spawn(JOB_INSTALL); }

/* Boot: detect a rollback (a new image crashed before being marked valid) and record it. */
void hal_ota_boot(void) {
    const esp_app_desc_t *d = esp_app_get_description();
    snprintf(st.current, sizeof st.current, "%s", d->version);
    int32_t tried = hal_kv_get("ota_try", 0);
    if (tried) {
        char tv[16]; snprintf(tv, sizeof tv, "%ld.%ld.%ld", (long)(tried >> 16), (long)((tried >> 8) & 0xFF), (long)(tried & 0xFF));
        if (strcmp(tv, st.current) != 0) {   /* we tried to install tv but are running something else: it rolled back */
            hal_kv_set("ota_bad", tried);
            snprintf(st.failed_version, sizeof st.failed_version, "%s", tv);
            st.rolled_back = !hal_kv_get("ota_rb_seen", 0);
            ESP_LOGW(TAG, "update %s failed to boot; running %s (rolled back)", tv, st.current);
        }
        hal_kv_set("ota_try", 0);
    }
    if (!st.rolled_back && hal_kv_get("ota_bad", 0)) {
        int32_t bv = hal_kv_get("ota_bad", 0);
        snprintf(st.failed_version, sizeof st.failed_version, "%ld.%ld.%ld", (long)(bv >> 16), (long)((bv >> 8) & 0xFF), (long)(bv & 0xFF));
    }
    spawn(JOB_BOOT);
}
/* Call once the UI has drawn its first frame. Until then a crash/reset rolls back to the previous slot. */
void hal_ota_mark_valid(void) {
    esp_ota_img_states_t s;
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(p, &s) == ESP_OK && s == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "%s marked valid", st.current);
    }
}
