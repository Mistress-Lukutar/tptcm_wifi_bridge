/**
 * @file   printer_uart.h
 * @brief  UART driver for the TPTCM60 printer link (via SN74LVC1T45
 *         3.3V/5V TTL level translators U2/U3)
 * @author Mistress-Lukutar
 * @date   2026-09-07
 * @version v1.0.1
 */

#ifndef PRINTER_UART_H
#define PRINTER_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  PUART_OK = 0,       /**< Success */
  PUART_ERR_INVALID,  /**< Invalid parameter */
  PUART_ERR_INIT,     /**< UART driver installation failed */
  PUART_ERR_TIMEOUT,  /**< Write timed out (TX buffer full) */
  PUART_ERR_NOT_READY /**< Module not initialized */
} PrinterUart_Status;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  Initialize the printer UART using Kconfig settings
 *         (port, TX/RX GPIO, baud rate)
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Init(void);

/**
 * @brief  Write a raw byte block to the printer
 * @param  data Input buffer (const)
 * @param  len  Number of bytes to write
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Write(const uint8_t* data, uint32_t len);

/**
 * @brief  Wait until all pending TX data has left the UART
 * @param  timeout_ms Maximum wait time in milliseconds
 * @return PrinterUart_Status error code
 */
PrinterUart_Status PrinterUart_Flush(uint32_t timeout_ms);

/**
 * @brief  Check whether the module is initialized
 * @return true when initialized
 */
bool PrinterUart_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* PRINTER_UART_H */
