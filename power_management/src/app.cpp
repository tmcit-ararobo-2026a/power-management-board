#include "power_management/app.hpp"

#include <cstdint>

#include "adc.h"
#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "gn10_can/devices/power_manager_types.hpp"
#include "power_management/can_callback_helper.hpp"
#include "power_management/fdcan_driver.hpp"

namespace {
/* パラメータ */
gn10_can::devices::power_manager::Config config{false, 1000};
float conv_voltage = 0.00613573407f;
float conv_current = 0.055f;
int current_offset = 1985;

/* CAN通信用クラス */
gn10_can::drivers::FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer server(fdcan_bus, 0);

/* フラグやバッファ */
bool initilized = false;
uint16_t adc_raw_value[2];
bool can_stop_signal_enable = false;

/* 前の状態保持 */
gn10_can::devices::power_manager::Status prev_status{false, false, false, false};

/* Hartbeat LED用 */
constexpr uint32_t k_heartbeat_toggle_interval_ms = 500;
uint32_t heartbeat_last_toggle_time_ms            = 0;
/**
 * @brief 一定周期のLEDチカチカ処理
 *
 */
void update_heartbeat_led()
{
    const uint32_t now_ms = HAL_GetTick();
    if ((now_ms - heartbeat_last_toggle_time_ms) >= k_heartbeat_toggle_interval_ms) {
        heartbeat_last_toggle_time_ms = now_ms;
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    }
}

/* センサの送信タイミング調整用 */
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
    bool remote_emergency_stop_enable    = false;
    bool remote_emergency_stop_connected = HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin);
    // 0Vのとき遠隔非常停止を有効化
    if (HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin)) {
        remote_emergency_stop_enable = false;
    } else {
        remote_emergency_stop_enable = true;
    }
    // 遠隔非常停止機能を使わない場合は遠隔非常停止を無効化
    if (!config.use_remote_emergency_stop) {
        remote_emergency_stop_enable = false;
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
    bool emergency_stop_enabled =
        HAL_GPIO_ReadPin(EMS_EN_IN_GPIO_Port, EMS_EN_IN_Pin) == GPIO_PIN_SET;
    if (emergency_stop_enabled) {  // 負論理
        // 駆動系OFF
        HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
    } else {  // 駆動系ON
        HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_RESET);
    }

    /* 過電流保護 */
    bool over_current =
        HAL_GPIO_ReadPin(OCD1_GPIO_Port, OCD1_Pin) || HAL_GPIO_ReadPin(OCD2_GPIO_Port, OCD2_Pin);

    /* Status送信 */
    gn10_can::devices::power_manager::Status status{};
    status.emergency_stop_enabled          = emergency_stop_enabled;
    status.remote_emergency_stop_connected = remote_emergency_stop_connected;
    status.remote_emergency_stop_enabled   = remote_emergency_stop_enable;
    status.over_current                    = over_current;
    if (status != prev_status) {
        server.set_status(status);
        prev_status = status;
    }

    update_sensor();
    update_heartbeat_led();
}

extern "C" {
// C言語側の関数のオーバーライド
/**
 * @brief Receive callback for FDCAN FIFO0.
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo0ITs)
{
    (void)RxFifo0ITs;
    if (process_fdcan_fifo(hfdcan, &hfdcan1, fdcan_bus, FDCAN_RX_FIFO0)) return;
}

/**
 * @brief Receive callback for FDCAN FIFO1.
 */
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef* hfdcan, uint32_t RxFifo1ITs)
{
    (void)RxFifo1ITs;
    if (process_fdcan_fifo(hfdcan, &hfdcan1, fdcan_bus, FDCAN_RX_FIFO1)) return;
}
}