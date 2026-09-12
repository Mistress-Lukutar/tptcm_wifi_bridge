/**
 * @file   ota_server.c
 * @brief  Admin web page: device info, firmware upload (OTA), log view
 * @author Mistress-Lukutar
 * @date   2026-09-13
 * @version v1.0.0
 */

/* Includes ------------------------------------------------------------------*/
#include "ota_server.h"
#include "log_stream.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/* Private defines -----------------------------------------------------------*/
#define LS_ADMIN_PORT CONFIG_TPTCM_ADMIN_PORT
#define LS_OTA_CHUNK 2048       /**< Upload receive chunk (static, httpd
                                     serves one request at a time) */
#define LS_LOG_SNAPSHOT 6144    /**< Bytes returned by GET /log */
#define LS_REBOOT_DELAY_MS 1500 /**< Let the HTTP response finish first */

/* Private variables ---------------------------------------------------------*/
static const char* s_tag = "OTA_SERVER";
static httpd_handle_t s_httpd = NULL;
static bool s_update_busy = false;

/* Private function prototypes -----------------------------------------------*/
static esp_err_t _httpIndexGet(httpd_req_t* req);
static esp_err_t _httpLogGet(httpd_req_t* req);
static esp_err_t _httpOtaPost(httpd_req_t* req);
static const char* _imgStateStr(const esp_partition_t* part);
static void _rebootTask(void* arg);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Human-readable state of an app image (rollback bookkeeping)
 */
static const char* _imgStateStr(const esp_partition_t* part) {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(part, &state) != ESP_OK) {
    return "factory / unknown";
  }
  switch (state) {
    case ESP_OTA_IMG_PENDING_VERIFY:
      return "pending verify";
    case ESP_OTA_IMG_VALID:
      return "valid";
    case ESP_OTA_IMG_INVALID:
      return "invalid";
    case ESP_OTA_IMG_ABORTED:
      return "aborted";
    default:
      return "undefined";
  }
}

/**
 * @brief  Delayed reboot so the HTTP response reaches the browser first
 */
static void _rebootTask(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(LS_REBOOT_DELAY_MS));
  esp_restart();
}

/**
 * @brief  GET / - device info, firmware upload form, live log window
 */
static esp_err_t _httpIndexGet(httpd_req_t* req) {
  static char page[2048]; /* httpd serves one request at a time */

  const esp_app_desc_t* app = esp_app_get_description();
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);

  int n = snprintf(page, sizeof(page),
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>TPTCM Bridge Admin</title>"
      "<style>body{font-family:sans-serif;max-width:34em;margin:1.5em auto;"
      "padding:0 1em}table{border-collapse:collapse;width:100%%}"
      "td{border:1px solid #ccc;padding:.3em .5em;font-size:.95em}"
      "td:first-child{background:#f4f4f4;width:11em}"
      "button,input{padding:.5em;font-size:1em;margin:.2em 0}"
      "button{background:#0366d6;color:#fff;border:0;border-radius:4px}"
      "#log{background:#111;color:#ccc;font-size:.75em;padding:.6em;"
      "overflow:auto;height:16em;white-space:pre-wrap}</style></head><body>"
      "<h2>TPTCM WiFi Bridge</h2>"
      "<table>"
      "<tr><td>Firmware</td><td>%s %s</td></tr>"
      "<tr><td>Built</td><td>%s %s</td></tr>"
      "<tr><td>ESP-IDF</td><td>%s</td></tr>"
      "<tr><td>Running slot</td><td>%s (%s)</td></tr>"
      "<tr><td>Update slot</td><td>%s</td></tr>"
      "<tr><td>Bridge IP</td><td>%s</td></tr>"
      "</table>"
      "<h3>Firmware update</h3>"
      "<p><input type=\"file\" id=\"f\" accept=\".bin\">"
      "<button onclick=\"up()\">Upload &amp; flash</button></p>"
      "<p id=\"st\"></p>"
      "<h3>Log</h3>"
      "<p>Live tail: <code>nc %s %d</code> (or PuTTY, connection type Raw)."
      "</p><pre id=\"log\">loading...</pre>"
      "<script>"
      "const $=i=>document.getElementById(i);"
      "setInterval(async()=>{try{$('log').textContent="
      "await(await fetch('/log')).text();}catch(e){}},2000);"
      "async function up(){const f=$('f').files[0];"
      "if(!f){$('st').textContent='Choose a .bin file first';return;}"
      "$('st').textContent='Uploading '+f.size+' bytes, keep power on...';"
      "try{const r=await fetch('/ota',{method:'POST',body:f});"
      "$('st').textContent=await r.text();}"
      "catch(e){$('st').textContent='Upload failed: '+e;}}"
      "</script></body></html>",
      app->project_name,
      app->version,
      app->date,
      app->time,
      app->idf_ver,
      running->label,
      _imgStateStr(running),
      (next != NULL) ? next->label : "none",
      g_app_ctx.ip_str,
      g_app_ctx.ip_str,
      LS_ADMIN_PORT);
  if (n < 0 || n >= (int)sizeof(page)) {
    (void)httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                              "Page build failed");
    return ESP_FAIL;
  }

  (void)httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page, (size_t)n);
}

/**
 * @brief  GET /log - snapshot of the captured log (plain text)
 */
static esp_err_t _httpLogGet(httpd_req_t* req) {
  static uint8_t buf[LS_LOG_SNAPSHOT]; /* httpd serves one request at a time */

  (void)httpd_resp_set_type(req, "text/plain");
  uint32_t copied = LogStream_Collect(buf, sizeof(buf));
  if (copied == 0U) {
    return httpd_resp_send(req, "", 0);
  }
  return httpd_resp_send(req, (const char*)buf, (size_t)copied);
}

/**
 * @brief  POST /ota - receive a raw .bin image, verify it, boot it
 */
static esp_err_t _httpOtaPost(httpd_req_t* req) {
  static uint8_t chunk[LS_OTA_CHUNK]; /* httpd serves one request at a time */

  if (s_update_busy) {
    (void)httpd_resp_send_err(req, 503, "Another update is in progress");
    return ESP_FAIL;
  }
  if (g_app_ctx.state == APP_STATE_PRINTING) {
    (void)httpd_resp_send_err(req, 409,
                              "Printing is in progress, retry after it ends");
    return ESP_FAIL;
  }

  const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
  if (target == NULL) {
    (void)httpd_resp_send_err(req, 500, "No OTA slot available");
    return ESP_FAIL;
  }
  int content_len = req->content_len;
  if (content_len <= 0 || (uint32_t)content_len > target->size) {
    (void)httpd_resp_send_err(req, 400, "Bad or oversized image");
    return ESP_FAIL;
  }

  s_update_busy = true;
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(target, (size_t)content_len, &handle) != ESP_OK) {
    s_update_busy = false;
    (void)httpd_resp_send_err(req, 500, "esp_ota_begin failed");
    return ESP_FAIL;
  }

  ESP_LOGI(s_tag, "Receiving %d bytes into %s", content_len, target->label);
  bool ok = true;
  bool magic_ok = false;
  int received_total = 0;
  while (received_total < content_len) {
    int received = httpd_req_recv(req, (char*)chunk, sizeof(chunk));
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (received <= 0) {
      ok = false;
      break;
    }
    if (!magic_ok) {
      /* ESP32 application images always start with the 0xE9 magic byte */
      magic_ok = (chunk[0] == 0xE9);
      if (!magic_ok) {
        ESP_LOGW(s_tag, "Refused file: bad magic 0x%02X", chunk[0]);
        ok = false;
        break;
      }
    }
    if (esp_ota_write(handle, chunk, (size_t)received) != ESP_OK) {
      ESP_LOGE(s_tag, "esp_ota_write failed");
      ok = false;
      break;
    }
    received_total += received;
  }

  if (ok && magic_ok && (received_total == content_len)) {
    ok = (esp_ota_end(handle) == ESP_OK);
    if (ok) {
      ok = (esp_ota_set_boot_partition(target) == ESP_OK);
    }
  } else {
    ok = false;
  }
  if (!ok) {
    (void)esp_ota_abort(handle);
    s_update_busy = false;
    (void)httpd_resp_send_err(req, 500,
                              "Update failed, previous firmware untouched");
    return ESP_FAIL;
  }

  s_update_busy = false;
  ESP_LOGI(s_tag,
           "Image written to %s (%d bytes), booting it",
           target->label,
           received_total);
  (void)httpd_resp_send(req, "Update OK, device is rebooting",
                        HTTPD_RESP_USE_STRLEN);
  (void)xTaskCreate(_rebootTask, "ota_reboot", 2048, NULL, 5, NULL);
  return ESP_OK;
}

/* Public functions ----------------------------------------------------------*/

OtaServer_Status OtaServer_Start(void) {
  if (g_app_ctx.ip_str[0] == '\0') {
    return OTA_ERR_NOT_READY; /* the info page needs the station IP */
  }
  if (s_httpd != NULL) {
    return OTA_OK; /* already running */
  }

  /*
   * Plain HTTP on the LAN, no authentication: the device is a desk
   * appliance. The provisioning portal (wifi_manager) also binds port
   * 80, but it is stopped before the station gets an IP, so the two
   * never run at the same time.
   */
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = LS_ADMIN_PORT;
  cfg.max_uri_handlers = 4;
  if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
    s_httpd = NULL;
    ESP_LOGE(s_tag, "Failed to start admin HTTP server on port %d",
             LS_ADMIN_PORT);
    return OTA_ERR_HTTPD;
  }

  const httpd_uri_t uri_index = {.uri = "/",
                                 .method = HTTP_GET,
                                 .handler = _httpIndexGet};
  const httpd_uri_t uri_log = {.uri = "/log",
                               .method = HTTP_GET,
                               .handler = _httpLogGet};
  const httpd_uri_t uri_ota = {.uri = "/ota",
                               .method = HTTP_POST,
                               .handler = _httpOtaPost};
  (void)httpd_register_uri_handler(s_httpd, &uri_index);
  (void)httpd_register_uri_handler(s_httpd, &uri_log);
  (void)httpd_register_uri_handler(s_httpd, &uri_ota);

  ESP_LOGI(s_tag, "Admin page: http://%s:%d/", g_app_ctx.ip_str,
           LS_ADMIN_PORT);
  return OTA_OK;
}
