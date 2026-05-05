/**
 * @file retarget_io.c
 * @brief printf / scanf を huart1 にリターゲットする実装
 *
 * syscalls.c で weak 宣言された __io_putchar / __io_getchar を
 * 強いシンボルとして定義することで、printf が UART1 に出力される。
 */

#include <stdint.h>

#include "usart.h"

// タイムアウト: 1 文字あたりの最大待機時間 [ms]
#define UART_TIMEOUT_MS 100U

/**
 * @brief 1 文字を huart1 へ送信する (printf のリターゲット先)
 * @param ch 送信する文字
 * @return 送信した文字。失敗時は EOF (-1)
 */
int __io_putchar(int ch) {
    uint8_t byte = (uint8_t)ch;
    if (HAL_UART_Transmit(&huart1, &byte, 1U, UART_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }
    return ch;
}

/**
 * @brief 1 文字を huart1 から受信する (scanf のリターゲット先)
 * @return 受信した文字。失敗時は EOF (-1)
 */
int __io_getchar(void) {
    uint8_t byte;
    if (HAL_UART_Receive(&huart1, &byte, 1U, UART_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }
    return (int)byte;
}
