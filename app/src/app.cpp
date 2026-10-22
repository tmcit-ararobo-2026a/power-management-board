#include "app/app.hpp"

#include <cstdint>
#include <cstdio>

#include "adc.h"
#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "gn10_can/devices/power_manager_types.hpp"
#include "gn10_stm32_fdcan_driver/can_callback_helper.hpp"
#include "gn10_stm32_fdcan_driver/fdcan_driver.hpp"

namespace {
/* パラメータ */
gn10_can::devices::power_manager::Config config{false, 1000};
constexpr float VOLTAGE_CONVERSION_RATE   = 1.0f / 49.75f;  // No.2: 49.75
constexpr float VOLTAGE_CONVERSION_OFFSET = 1534.0f;        // No.2: 1534.0
constexpr float MCU_REFERENCE_VOLTAGE     = 3.30f;          // MCUのADC基準電圧[V]
constexpr float CURRENT_CONVERSION_RATE =
    1.0f / ((4095.0f / MCU_REFERENCE_VOLTAGE) * 0.012f);  // 12[mV/A]
constexpr int CURRENT_CONVERSION_OFFSET =
    1.65f * (4095.0f / MCU_REFERENCE_VOLTAGE);  // 0[A]での電圧は1.65[V]
constexpr uint32_t CAPACITOR_CHARGE_TIME_MS = 500;

/* CAN通信用クラス */
gn10_can::drivers::FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer server(fdcan_bus, 0);

/* フラグやバッファ */
bool initialized = false;
uint16_t adc_raw_value[2];
// 非常停止
bool can_stop_signal_enable = false;
bool emergency_stop_enabled = true;
// コンデンサ充電
uint32_t capacitor_charge_start_tick = 0;
enum class ChargeState : uint8_t {
    Uncharged,
    Charging,
    Charged,
};
ChargeState capacitor_state = ChargeState::Uncharged;

/* 前の状態保持 */
gn10_can::devices::power_manager::Status prev_status{false, false, false, false};

/* Hartbeat LED用 */
constexpr uint32_t HEARTBEAT_TOGGLE_INTERVAL_MS = 500;
uint32_t heartbeat_last_toggle_time_ms          = 0;
/**
 * @brief 一定周期のLEDチカチカ処理
 *
 */
void update_heartbeat_led()
{
    const uint32_t now_ms = HAL_GetTick();
    if ((now_ms - heartbeat_last_toggle_time_ms) >= HEARTBEAT_TOGGLE_INTERVAL_MS) {
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
        float voltage        = std::clamp(
            (float(vlotage_raw) - VOLTAGE_CONVERSION_OFFSET) * VOLTAGE_CONVERSION_RATE, 0.0f, 50.0f
        );
        float current = (float(current_raw) - CURRENT_CONVERSION_OFFSET) * CURRENT_CONVERSION_RATE;
        /* CANで送信 */
        gn10_can::devices::power_manager::Sensor sensor_msg;
        sensor_msg.voltage = voltage;
        sensor_msg.current = current;
        server.set_sensor(sensor_msg);
    }
}

void update_output()
{
    // 非常停止スイッチが作動して電源遮断中
    if (emergency_stop_enabled) {
        HAL_GPIO_WritePin(ENABLE_GPIO_Port, ENABLE_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(PRECHARGE_GPIO_Port, PRECHARGE_Pin, GPIO_PIN_RESET);
        capacitor_state = ChargeState::Uncharged;
        return;
    }

    // 非常停止スイッチは無効で電源ON
    switch (capacitor_state) {
        case ChargeState::Uncharged:
            capacitor_charge_start_tick = HAL_GetTick();
            capacitor_state             = ChargeState::Charging;
            HAL_GPIO_WritePin(PRECHARGE_GPIO_Port, PRECHARGE_Pin, GPIO_PIN_SET);
            break;

        case ChargeState::Charging:
            if ((HAL_GetTick() - capacitor_charge_start_tick) > CAPACITOR_CHARGE_TIME_MS) {
                capacitor_state = ChargeState::Charged;
                HAL_GPIO_WritePin(ENABLE_GPIO_Port, ENABLE_Pin, GPIO_PIN_SET);
            }
            break;

        case ChargeState::Charged:

        default:
            break;
    }
}

}  // namespace

void setup()
{
    fdcan_driver.init();
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_value, 2) != HAL_OK) {
        Error_Handler();
    }
}

void loop()
{
    if (server.get_new_init(config)) {
        initialized = true;
        HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
    }

    /* CAN通信経由の非常停止信号受信 */
    server.get_new_stop(can_stop_signal_enable);

    /* 電源遮断回路への信号を監視して放電回路を動作 */
    bool physical_emergency_stop_enabled =
        HAL_GPIO_ReadPin(EMS_EN_IN_GPIO_Port, EMS_EN_IN_Pin) == GPIO_PIN_SET;

    emergency_stop_enabled = physical_emergency_stop_enabled || can_stop_signal_enable;

    update_output();

    /* 過電流保護 */
    bool over_current =
        HAL_GPIO_ReadPin(OCD1_GPIO_Port, OCD1_Pin) || HAL_GPIO_ReadPin(OCD2_GPIO_Port, OCD2_Pin);

    /* Status送信 */
    gn10_can::devices::power_manager::Status status{};
    status.emergency_stop_enabled        = emergency_stop_enabled;
    status.remote_emergency_stop_enabled = can_stop_signal_enable;
    status.over_current                  = over_current;
    if (status != prev_status) {
        server.set_status(status);
        prev_status = status;
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