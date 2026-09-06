/**
 * @file   printer_uart.c
 * @brief  UART driver for the TPTCM60 printer link (via SN74LVC1T45
 *         3.3V/5V TTL level translators U2/U3)
 * @author Mistress-Lukutar
 * @date   2026-09-07
 * @version v1.0.1
 */

/* Includes ------------------------------------------------------------------*/
#include "printer_uart.h"
#include "main.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

/* Private defines -----------------------------------------------------------*/
#define PUART_TX_BUF_SIZE 4096 /**< TX ring buffer, absorbs TCP bursts */
#define PUART_RX_BUF_SIZE 1024 /**< RX ring buffer (status bytes unused) */

/* Private variables ---------------------------------------------------------*/
static const char* s_tag = "PRINTER_UART";
static bool s_initialized = false;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Initialize the printer UART using Kconfig settings
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Init(void) {
  uart_config_t uart_cfg = {
      .baud_rate = CONFIG_TPTCM_UART_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  if (uart_driver_install((uart_port_t)CONFIG_TPTCM_UART_PORT,
                          PUART_RX_BUF_SIZE,
                          PUART_TX_BUF_SIZE,
                          0,
                          NULL,
                          0) != ESP_OK) {
    ESP_LOGE(s_tag, "uart_driver_install failed");
    return PUART_ERR_INIT;
  }

  if (uart_param_config((uart_port_t)CONFIG_TPTCM_UART_PORT, &uart_cfg) !=
      ESP_OK) {
    ESP_LOGE(s_tag, "uart_param_config failed");
    return PUART_ERR_INIT;
  }

  if (uart_set_pin((uart_port_t)CONFIG_TPTCM_UART_PORT,
                   CONFIG_TPTCM_UART_TX_GPIO,
                   CONFIG_TPTCM_UART_RX_GPIO,
                   UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK) {
    ESP_LOGE(s_tag, "uart_set_pin failed");
    return PUART_ERR_INIT;
  }

  s_initialized = true;
  ESP_LOGI(s_tag,
           "UART%d ready: %d 8N1, TX=GPIO%d, RX=GPIO%d",
           CONFIG_TPTCM_UART_PORT,
           CONFIG_TPTCM_UART_BAUD,
           CONFIG_TPTCM_UART_TX_GPIO,
           CONFIG_TPTCM_UART_RX_GPIO);
  return PUART_OK;
}

/**
 * @brief  Write a raw byte block to the printer
 * @param  data Input buffer (const)
 * @param  len  Number of bytes to write
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Write(const uint8_t* data, uint32_t len) {
  if (data == NULL || len == 0U) {
    return PUART_ERR_INVALID;
  }
  if (!s_initialized) {
    return PUART_ERR_NOT_READY;
  }

  int written = uart_write_bytes((uart_port_t)CONFIG_TPTCM_UART_PORT,
                                 (const char*)data,
                                 len);
  if (written < 0) {
    return PUART_ERR_TIMEOUT;
  }
  return PUART_OK;
}

/**
 * @brief  Wait until all pending TX data has left the UART
 * @param  timeout_ms Maximum wait time in milliseconds
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Flush(uint32_t timeout_ms) {
  if (!s_initialized) {
    return PUART_ERR_NOT_READY;
  }
  if (uart_wait_tx_done((uart_port_t)CONFIG_TPTCM_UART_PORT,
                        pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
    return PUART_ERR_TIMEOUT;
  }
  return PUART_OK;
}

/**
 * @brief  Check whether the module is initialized
 * @return true when initialized
 */
bool PrinterUart_IsReady(void) {
  return s_initialized;
}
