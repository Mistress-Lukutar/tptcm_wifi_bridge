/**
 * @file   log_stream.h
 * @brief  ESP_LOG tee: RAM ring capture, snapshot and live TCP log tail
 * @author Mistress-Lukutar
 * @date   2026-09-13
 * @version v1.0.0
 */

#ifndef LOG_STREAM_H
#define LOG_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  LS_OK = 0,        /**< Success */
  LS_ERR_INIT,      /**< Sink installation failed */
  LS_ERR_TASK,      /**< TCP tail task creation failed */
  LS_ERR_NOT_READY  /**< Called before LogStream_Init() */
} LogStream_Status;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Install the log tee sink: every ESP_LOG line is forwarded to
 *         the previous output (USB/UART console) and kept in a RAM ring
 *         for the remote viewers. Call once, early in app_main
 * @return LogStream_Status error code
 */
LogStream_Status LogStream_Init(void);

/**
 * @brief  Start the live TCP log tail server (one client at a time) on
 *         CONFIG_TPTCM_LOG_TCP_PORT. Requires LogStream_Init() and an
 *         IP address; idempotent
 * @return LogStream_Status error code
 */
LogStream_Status LogStream_StartTcp(void);

/**
 * @brief  Copy the most recent captured bytes into the output buffer
 * @param  out      Output buffer
 * @param  out_size Output buffer size
 * @return Number of bytes actually copied
 */
uint32_t LogStream_Collect(uint8_t* out, uint32_t out_size);

/**
 * @brief  Check whether the TCP tail server is running
 * @return true when a client can connect
 */
bool LogStream_IsTcpUp(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_STREAM_H */
