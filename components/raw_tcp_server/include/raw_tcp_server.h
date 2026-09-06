/**
 * @file   raw_tcp_server.h
 * @brief  RAW TCP print server (JetDirect port 9100) forwarding to the
 *         printer UART
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

#ifndef RAW_TCP_SERVER_H
#define RAW_TCP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  RTS_OK = 0,       /**< Success */
  RTS_ERR_INVALID,  /**< Invalid parameter */
  RTS_ERR_SOCKET,   /**< Socket creation/bind/listen failed */
  RTS_ERR_TASK,     /**< Server task creation failed */
  RTS_ERR_NOT_READY /**< Printer UART is not initialized */
} RawTcpServer_Status;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Start the RAW TCP print server on CONFIG_TPTCM_TCP_PORT
 *
 * One client at a time: while a client holds the connection, further
 * connections are accepted and immediately closed. Received bytes are
 * transparently forwarded to the printer UART.
 *
 * @return RawTcpServer_Status error code
 */
RawTcpServer_Status RawTcpServer_Start(void);

/**
 * @brief  Check whether a client is currently connected
 * @return true when a print client holds the connection
 */
bool RawTcpServer_HasClient(void);

#ifdef __cplusplus
}
#endif

#endif /* RAW_TCP_SERVER_H */
