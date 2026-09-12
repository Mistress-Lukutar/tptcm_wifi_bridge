/**
 * @file   log_stream.c
 * @brief  ESP_LOG tee: RAM ring capture, snapshot and live TCP log tail
 * @author Mistress-Lukutar
 * @date   2026-09-13
 * @version v1.0.0
 */

/* Includes ------------------------------------------------------------------*/
#include "log_stream.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

/* Private defines -----------------------------------------------------------*/
#define LS_LINE_SIZE 384        /**< Local formatting buffer, one log call */
#define LS_TAIL_PORT CONFIG_TPTCM_LOG_TCP_PORT
#define LS_TAIL_POLL_MS 200     /**< TCP tail poll period */
#define LS_TAIL_HISTORY 2048    /**< Context bytes for a fresh tail client */
#define LS_SEND_CHUNK 1024      /**< Max bytes sent to the tail per poll */

/* Private variables ---------------------------------------------------------*/
static const char* s_tag = "LOG_STREAM";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_ring[CONFIG_TPTCM_LOG_RING_SIZE];
static size_t s_head = 0;      /**< Ring write index */
static size_t s_count = 0;     /**< Valid bytes in the ring (<= ring size) */
static uint32_t s_total = 0;   /**< Monotonic bytes captured since boot */
static vprintf_like_t s_prev = NULL;
static TaskHandle_t s_tail_task = NULL;

/* Private function prototypes -----------------------------------------------*/
static int _logSink(const char* format, va_list args);
static void _ringWrite(const uint8_t* data, size_t len);
static uint32_t _ringReadFrom(uint32_t pos, uint8_t* out, uint32_t out_size);
static void _tailTask(void* arg);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  esp_log sink: format the line, store it in the ring, forward
 *         to the previous handler (USB/UART console)
 * @note   Called from any logging task, must not log itself and must not
 *         block. The ring copy is one short critical section
 */
static int _logSink(const char* format, va_list args) {
  char line[LS_LINE_SIZE];

  va_list copy;
  va_copy(copy, args);
  int n = vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  if (n > 0) {
    size_t len = ((size_t)n >= sizeof(line)) ? sizeof(line) - 1U
                                             : (size_t)n;
    if ((size_t)n >= sizeof(line)) {
      line[sizeof(line) - 2U] = '\n'; /* keep the frame readable */
    }
    _ringWrite((const uint8_t*)line, len);
  }

  if (s_prev != NULL) {
    return s_prev(format, args);
  }
  return n;
}

/**
 * @brief  Append bytes to the ring, dropping the oldest on overflow
 */
static void _ringWrite(const uint8_t* data, size_t len) {
  portENTER_CRITICAL(&s_mux);
  for (size_t i = 0U; i < len; i++) {
    s_ring[s_head] = data[i];
    s_head = (s_head + 1U) % sizeof(s_ring);
    if (s_count < sizeof(s_ring)) {
      s_count++;
    }
  }
  s_total += (uint32_t)len;
  portEXIT_CRITICAL(&s_mux);
}

/**
 * @brief  Copy captured bytes starting from a monotonic stream position
 * @param  pos      Logical offset since boot (clamped to what is kept)
 * @param  out      Output buffer
 * @param  out_size Output buffer size
 * @return Number of bytes copied
 */
static uint32_t _ringReadFrom(uint32_t pos, uint8_t* out, uint32_t out_size) {
  portENTER_CRITICAL(&s_mux);
  uint32_t base = (s_total > sizeof(s_ring))
                      ? s_total - (uint32_t)sizeof(s_ring)
                      : 0U;
  uint32_t from = (pos < base) ? base : pos;
  uint32_t avail = s_total - from;
  uint32_t len = (avail < out_size) ? avail : out_size;
  size_t start = (size_t)(from % (uint32_t)sizeof(s_ring));
  for (uint32_t i = 0U; i < len; i++) {
    out[i] = s_ring[(start + i) % sizeof(s_ring)];
  }
  portEXIT_CRITICAL(&s_mux);
  return len;
}

/**
 * @brief  Serve one tail client: send a short context snapshot, then
 *         stream new bytes as they are captured
 * @param  client_sock Connected client socket (closed on return)
 */
static void _serveTail(int client_sock) {
  uint8_t buf[LS_SEND_CHUNK];

  uint32_t total;
  portENTER_CRITICAL(&s_mux);
  total = s_total;
  portEXIT_CRITICAL(&s_mux);
  uint32_t sent = (total > LS_TAIL_HISTORY) ? total - LS_TAIL_HISTORY : 0U;

  while (true) {
    uint32_t len = _ringReadFrom(sent, buf, sizeof(buf));
    if (len > 0U) {
      int sent_now = send(client_sock, buf, len, 0);
      if (sent_now < 0) {
        break; /* client went away */
      }
      sent += (uint32_t)sent_now;
    }
    vTaskDelay(pdMS_TO_TICKS(LS_TAIL_POLL_MS));
  }
  close(client_sock);
}

/**
 * @brief  Accept loop of the live log tail server
 */
static void _tailTask(void* arg) {
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
  addr.sin_port = htons(LS_TAIL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(s_tag, "bind() failed: errno %d", errno);
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }
  if (listen(listen_sock, 1) != 0) {
    ESP_LOGE(s_tag, "listen() failed: errno %d", errno);
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(s_tag, "Log tail server on port %d", LS_TAIL_PORT);
  while (true) {
    int client_sock = accept(listen_sock, NULL, NULL);
    if (client_sock < 0) {
      ESP_LOGW(s_tag, "accept() failed: errno %d", errno);
      continue;
    }
    _serveTail(client_sock);
    ESP_LOGI(s_tag, "Tail client disconnected");
  }
}

/* Public functions ----------------------------------------------------------*/

LogStream_Status LogStream_Init(void) {
  if (s_prev != NULL) {
    return LS_OK; /* already installed */
  }
  s_prev = esp_log_set_vprintf(_logSink);
  ESP_LOGI(s_tag,
           "Log tee active, ring %u bytes",
           (unsigned)sizeof(s_ring));
  return LS_OK;
}

LogStream_Status LogStream_StartTcp(void) {
  if (s_prev == NULL) {
    return LS_ERR_NOT_READY;
  }
  if (s_tail_task != NULL) {
    return LS_OK; /* already running */
  }
  if (xTaskCreate(_tailTask, "log_tail", 3072, NULL, 3, &s_tail_task) !=
      pdPASS) {
    s_tail_task = NULL;
    return LS_ERR_TASK;
  }
  return LS_OK;
}

uint32_t LogStream_Collect(uint8_t* out, uint32_t out_size) {
  if (out == NULL || out_size == 0U) {
    return 0U;
  }
  portENTER_CRITICAL(&s_mux);
  size_t len = (s_count < out_size) ? s_count : (size_t)out_size;
  size_t start = (s_head + sizeof(s_ring) - len) % sizeof(s_ring);
  for (size_t i = 0U; i < len; i++) {
    out[i] = s_ring[(start + i) % sizeof(s_ring)];
  }
  portEXIT_CRITICAL(&s_mux);
  return (uint32_t)len;
}

bool LogStream_IsTcpUp(void) {
  return (s_tail_task != NULL);
}
