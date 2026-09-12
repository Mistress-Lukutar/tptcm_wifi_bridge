/**
 * @file   ota_server.h
 * @brief  Admin web page: device info, firmware upload (OTA), log view
 * @author Mistress-Lukutar
 * @date   2026-09-13
 * @version v1.0.0
 */

#ifndef OTA_SERVER_H
#define OTA_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  OTA_OK = 0,        /**< Success */
  OTA_ERR_NOT_READY, /**< LogStream is not initialized */
  OTA_ERR_HTTPD      /**< HTTP server failed to start */
} OtaServer_Status;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Start the admin HTTP server on CONFIG_TPTCM_ADMIN_PORT:
 *         GET / device info + firmware upload page + log viewer,
 *         GET /log captured log snapshot, POST /ota firmware image.
 *         Requires LogStream_Init() and an IP address; idempotent
 * @return OtaServer_Status error code
 */
OtaServer_Status OtaServer_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SERVER_H */
