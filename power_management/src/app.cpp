#include "power_management/app.hpp"

#include <cstdint>

#include "adc.h"
#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "gn10_can/devices/power_manager_types.hpp"
#include "power_management/fdcan_driver.hpp"

namespace {
/* パラメータ */
gn10_can::devices::power_manager::Config config;
float conv_voltage = 0.00613573407f;
float conv_current = 0.055f;
int current_offset = 1985;
/* CAN通信用クラス */
FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer server(fdcan_bus, 0);
/* フラグやバッファ */
bool initilized = false;
uint16_t adc_raw_value[2];
bool can_stop_signal_enable = false;

constexpr uint32_t k_heartbeat_toggle_interval_ms = 500;
uint32_t heartbeat_last_toggle_time_ms            = 0;
/**
 * @brief Toggle heartbeat LED at a fixed interval.
 */
void update_heartbeat_led()
{
    const uint32_t now_ms = HAL_GetTick();
    if ((now_ms - heartbeat_last_toggle_time_ms) >= k_heartbeat_toggle_interval_ms) {
        heartbeat_last_toggle_time_ms = now_ms;
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    }
}

uint32_t sensor_last_update_time_ms = 0;
/**
 * @brief センサのデータを取得し正規化した値をCAN通信で送信
 *
 */
void update_sensor()
{
    const uint32_t now_ms = HAL_GetTick();
    if ((now_ms - sensor_last_update_time_ms) >= config.sensor_rate_ms) {
        sensor_last_update_time_ms = now_ms;
        /* 電圧・電流測定 */
        uint16_t current_raw = adc_raw_value[0];
        uint16_t vlotage_raw = adc_raw_value[1];
        float voltage        = vlotage_raw * conv_voltage;
        float current        = float((int)current_raw - current_offset) * conv_current;
        /* CANで送信 */
        gn10_can::devices::power_manager::Sensor sensor_msg;
        sensor_msg.voltage = voltage;
        sensor_msg.current = current;
        server.set_sensor(sensor_msg);
    }
}

}  // namespace

void setup()
{
    fdcan_driver.init();
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_value, 2) != HAL_OK) {
        Error_Handler();
    }
}

void loop()
{
    if (server.get_new_init(config)) {
        initilized = true;
        HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
    }

    /* 遠隔非常停止 */
    bool remote_emergency_stop_enable = false;
    if (config.use_remote_emergency_stop) {
        if (HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin) && HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin)) {
            remote_emergency_stop_enable = false;
        } else {
            remote_emergency_stop_enable = true;
        }
    }

    /* CAN通信経由の非常停止信号受信 */
    server.get_new_stop(can_stop_signal_enable);
    if (can_stop_signal_enable) {
        HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
    }

    /* 遠隔非常停止＆CAN通信経由非常停止を反映 */
    bool logical_stop_enable = can_stop_signal_enable || remote_emergency_stop_enable;
    if (logical_stop_enable) {
        HAL_GPIO_WritePin(EMS_EN_OUT_GPIO_Port, EMS_EN_OUT_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(EMS_EN_OUT_GPIO_Port, EMS_EN_OUT_Pin, GPIO_PIN_SET);
    }

    /* 電源遮断回路への信号を監視して放電回路を動作 */
    if (HAL_GPIO_ReadPin(EMS_EN_IN_GPIO_Port, EMS_EN_IN_Pin)) {  // 駆動系ON
        HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_RESET);
    } else {  // 駆動系OFF
        HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
    }

    update_sensor();
    update_heartbeat_led();
    HAL_Delay(1);
}

extern "C" {
// C言語側の関数のオーバーライド
/**
 * @brief Receive callback for FDCAN FIFO0.
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs)
{
    fdcan_bus.update();
}
}