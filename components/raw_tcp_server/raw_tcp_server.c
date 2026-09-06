/**
 * @file   raw_tcp_server.c
 * @brief  RAW TCP print server (JetDirect port 9100) forwarding to the
 *         printer UART
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

/* Includes ------------------------------------------------------------------*/
#include "raw_tcp_server.h"
#include "main.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "printer_uart.h"
#include "sdkconfig.h"

/* Private defines -----------------------------------------------------------*/
#define RTS_RX_BUF_SIZE 1460      /**< One TCP MSS per recv() */
#define RTS_BACKLOG 2             /**< Pending connections while busy */
#define RTS_TASK_STACK 4096       /**< Server task stack depth */
#define RTS_TASK_PRIO 5           /**< Server task priority */
#define RTS_FLUSH_TIMEOUT_MS 2000 /**< UART drain wait on disconnect */

/* Private variables ---------------------------------------------------------*/
static const char* s_tag = "RAW_TCP";
static bool s_has_client = false;

/* Private function prototypes -----------------------------------------------*/
static void _serverTask(void* arg);
static void _serveClient(int client_sock);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Accept loop: one client at a time, extra connections are refused
 */
static void _serverTask(void* arg) {
  (void)arg;

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_sock < 0) {
    ESP_LOGE(s_tag, "socket() failed: errno %d", errno);
    vTaskDelete(NULL);
    return;
  }

  int opt = 1;
  (void)setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CONFIG_TPTCM_TCP_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(s_tag, "bind() failed: errno %d", errno);
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }
  if (listen(listen_sock, RTS_BACKLOG) != 0) {
    ESP_LOGE(s_tag, "listen() failed: errno %d", errno);
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(s_tag, "TCP server on port %d", CONFIG_TPTCM_TCP_PORT);

  while (true) {
    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    int client_sock =
        accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock < 0) {
      ESP_LOGW(s_tag, "accept() failed: errno %d", errno);
      continue;
    }
    _serveClient(client_sock);
  }
}

/**
 * @brief  Forward one client's byte stream to the printer UART
 * @param  client_sock Connected client socket (closed on return)
 */
static void _serveClient(int client_sock) {
  uint8_t rx_buf[RTS_RX_BUF_SIZE];
  char ip_str[16] = {0};

  int opt = 1;
  (void)setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

  struct sockaddr_in client_addr = {0};
  socklen_t client_len = sizeof(client_addr);
  if (getpeername(client_sock, (struct sockaddr*)&client_addr,
                  &client_len) == 0) {
    const char* ntop = inet_ntoa(client_addr.sin_addr);
    if (ntop != NULL) {
      strncpy(ip_str, ntop, sizeof(ip_str) - 1U);
    }
  }

  s_has_client = true;
  App_SetState(APP_STATE_PRINTING);
  ESP_LOGI(s_tag, "Client connected: %s", ip_str);

  while (true) {
    int len = recv(client_sock, rx_buf, sizeof(rx_buf), 0);
    if (len < 0) {
      ESP_LOGW(s_tag, "recv() error: errno %d", errno);
      break;
    }
    if (len == 0) {
      break; /* orderly shutdown by the peer */
    }
    if (PrinterUart_Write(rx_buf, (uint32_t)len) != PUART_OK) {
      ESP_LOGE(s_tag, "UART write failed, dropping client");
      break;
    }
  }

  (void)PrinterUart_Flush(RTS_FLUSH_TIMEOUT_MS);
  shutdown(client_sock, 0);
  close(client_sock);

  s_has_client = false;
  App_SetState(APP_STATE_READY);
  ESP_LOGI(s_tag, "Client disconnected: %s", ip_str);
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Start the RAW TCP print server on CONFIG_TPTCM_TCP_PORT
 * @return RawTcpServer_Status error code
 */
RawTcpServer_Status RawTcpServer_Start(void) {
  if (!PrinterUart_IsReady()) {
    return RTS_ERR_NOT_READY;
  }
  if (xTaskCreate(_serverTask, "raw_tcp", RTS_TASK_STACK, NULL,
                  RTS_TASK_PRIO, NULL) != pdPASS) {
    return RTS_ERR_TASK;
  }
  return RTS_OK;
}

/**
 * @brief  Check whether a client is currently connected
 * @return true when a print client holds the connection
 */
bool RawTcpServer_HasClient(void) {
  return s_has_client;
}
