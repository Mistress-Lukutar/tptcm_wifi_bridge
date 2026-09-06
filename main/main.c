/**
 * @file   main.c
 * @brief  Main application entry point: UART init, WiFi manager, TCP server
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

#include "main.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

/* Component includes */
#include "printer_uart.h"
#include "raw_tcp_server.h"
#include "wifi_manager.h"

static const char* TAG = "MAIN";

/* ==================== Global Variables ==================== */

App_Context g_app_ctx = {.state = APP_STATE_INIT,
                         .last_error = APP_ERROR_NONE,
                         .ip_str = {0}};

/* ==================== Private Functions =================== */

/**
 * @brief  WiFi "station got IP" callback: start the print server
 * @param  ip_str Station IPv4 address in dotted-decimal notation
 */
static void _onWifiConnected(const char* ip_str) {
  if (ip_str == NULL) {
    return;
  }
  strncpy(g_app_ctx.ip_str, ip_str, sizeof(g_app_ctx.ip_str) - 1U);
  ESP_LOGI(TAG, "WiFi connected, IP: %s", g_app_ctx.ip_str);

  if (g_app_ctx.state != APP_STATE_READY &&
      g_app_ctx.state != APP_STATE_PRINTING) {
    if (RawTcpServer_Start() != RTS_OK) {
      g_app_ctx.last_error = APP_ERROR_TCP_SERVER;
      ESP_LOGE(TAG, "Failed to start TCP print server");
      return;
    }
    ESP_LOGI(TAG, "Print server ready on %s:%d", g_app_ctx.ip_str,
             CONFIG_TPTCM_TCP_PORT);
  }
  App_SetState(APP_STATE_READY);
}

/**
 * @brief  Initialize NVS, erasing the partition when it is corrupted
 * @return true on success
 */
static bool _initNvs(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK) {
      return false;
    }
    err = nvs_flash_init();
  }
  return (err == ESP_OK);
}

/* ==================== Public Functions ==================== */

/**
 * @brief  Atomically switch the application state and log the transition
 * @param  new_state Target state
 */
void App_SetState(App_State new_state) {
  if (g_app_ctx.state == new_state) {
    return;
  }
  ESP_LOGI(TAG, "State: %d -> %d", (int)g_app_ctx.state, (int)new_state);
  g_app_ctx.state = new_state;
}

/**
 * @brief  Application entry point
 */
void app_main(void) {
  ESP_LOGI(TAG, "TPTCM WiFi Bridge v1.0.0");

  if (!_initNvs()) {
    g_app_ctx.last_error = APP_ERROR_NVS;
    ESP_LOGE(TAG, "NVS init failed, credentials will not persist");
  }

  if (PrinterUart_Init() != PUART_OK) {
    g_app_ctx.last_error = APP_ERROR_UART;
    ESP_LOGE(TAG, "Printer UART init failed");
    return;
  }

  if (WifiManager_Init() != WM_OK ||
      WifiManager_Start(_onWifiConnected) != WM_OK) {
    g_app_ctx.last_error = APP_ERROR_WIFI;
    ESP_LOGE(TAG, "WiFi manager start failed");
    return;
  }

  if (!WifiManager_IsConnected()) {
    App_SetState(APP_STATE_PROV);
    ESP_LOGI(TAG, "Waiting for WiFi (saved network or provisioning AP)");
  }
}
