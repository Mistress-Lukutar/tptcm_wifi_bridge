/**
 * @file   wifi_manager.h
 * @brief  WiFi connection manager with NVS credentials and AP provisioning
 *         portal
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  WM_OK = 0,       /**< Success */
  WM_ERR_INVALID,  /**< Invalid parameter */
  WM_ERR_NVS,      /**< NVS access error */
  WM_ERR_WIFI,     /**< WiFi subsystem error */
  WM_ERR_HTTP,     /**< HTTP server error */
  WM_ERR_NOT_READY /**< Module not initialized */
} WifiManager_Status;

/**
 * @brief Callback invoked when the station obtains an IP address
 * @param ip_str IPv4 address in dotted-decimal notation
 */
typedef void (*WifiManager_ConnectedCb)(const char* ip_str);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Initialize the WiFi manager (netif, event handlers, NVS
 *         credentials). Does not start the radio yet.
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_Init(void);

/**
 * @brief  Start the WiFi manager
 *
 * Behavior:
 * - Credentials saved in NVS (or non-empty Kconfig defaults): connect as
 *   station. After CONFIG_TPTCM_STA_MAX_RETRY failed attempts the
 *   provisioning access point is started additionally; STA retries
 *   continue in the background.
 * - No credentials: start the provisioning access point immediately.
 *
 * Provisioning AP: CONFIG_TPTCM_AP_SSID / CONFIG_TPTCM_AP_PASSWORD,
 * captive portal on http://192.168.4.1/ (wildcard DNS). Saving
 * credentials stores them in NVS and reboots the device.
 *
 * @param  connected_cb Callback for "STA got IP" event (may be NULL)
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_Start(WifiManager_ConnectedCb connected_cb);

/**
 * @brief  Check whether the station is connected and has an IP
 * @return true when connected
 */
bool WifiManager_IsConnected(void);

/**
 * @brief  Erase saved WiFi credentials from NVS
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_ClearCredentials(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
