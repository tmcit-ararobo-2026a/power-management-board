#include "power_management/app.hpp"

#include "adc.h"
#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "power_management/fdcan_driver.hpp"
#include "stdio.h"

namespace {

FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer server(fdcan_bus, 0);

bool initilized = false;
uint16_t adc_raw_value[2];
float voltage      = 0.0f;
float current      = 0.0f;
int i              = 0;
float conv_voltage = 0.00613573407f;
int current_offset = 1985;
float conv_current = 0.055f;

}  // namespace

void setup()
{
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
    fdcan_driver.init();
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_value, 2) != HAL_OK) {
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        Error_Handler();
    }
    HAL_GPIO_WritePin(EMS_EN_OUT_GPIO_Port, EMS_EN_OUT_Pin, GPIO_PIN_SET);
    // HAL_GPIO_WritePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin, GPIO_PIN_SET);
}

void loop()
{
    // if (server.get_new_init()) {
    //     initilized = true;
    // }
    // if (!initilized) {
    //     return;  // 初期化されていなければ動作しない
    // }
    if (i > 50) {
        HAL_GPIO_TogglePin(EMS_EN_OUT_GPIO_Port, EMS_EN_OUT_Pin);
        HAL_GPIO_TogglePin(DISCHARGE_GPIO_Port, DISCHARGE_Pin);
        i = 0;
    }
    i++;

    uint16_t current_raw = adc_raw_value[0];
    uint16_t vlotage_raw = adc_raw_value[1];
    voltage              = vlotage_raw * conv_voltage;
    current              = float((int)current_raw - current_offset) * conv_current;
    printf("current:%f, voltage:%f\n", current, voltage);
    HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    HAL_Delay(100);
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