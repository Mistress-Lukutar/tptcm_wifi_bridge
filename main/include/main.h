/**
 * @file   main.h
 * @brief  Global application configuration for the TPTCM WiFi bridge
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

/* Exported types ------------------------------------------------------------*/
typedef enum {
  APP_STATE_INIT = 0, /**< Initialization in progress */
  APP_STATE_PROV,     /**< Provisioning AP is up, waiting for WiFi setup */
  APP_STATE_READY,    /**< Connected to WiFi, TCP server accepting clients */
  APP_STATE_PRINTING  /**< A client is connected and printing */
} App_State;

typedef enum {
  APP_ERROR_NONE = 0,    /**< No error */
  APP_ERROR_UART,        /**< Printer UART init failed */
  APP_ERROR_WIFI,        /**< WiFi manager init/start failed */
  APP_ERROR_TCP_SERVER,  /**< TCP server start failed */
  APP_ERROR_NVS          /**< NVS init failed */
} App_Error;

typedef struct {
  App_State state;        /**< Current application state */
  App_Error last_error;   /**< Last recorded error */
  char ip_str[16];        /**< Station IPv4 address when connected */
} App_Context;

/* Exported variables --------------------------------------------------------*/
extern App_Context g_app_ctx;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Atomically switch the application state and log the transition
 * @param  new_state Target state
 */
void App_SetState(App_State new_state);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
