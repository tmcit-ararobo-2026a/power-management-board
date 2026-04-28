#include "power_management/app.hpp"

#include "adc.h"
#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "power_management/fdcan_driver.hpp"

namespace {

FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer server(fdcan_bus, 0);

bool initilized = false;
uint16_t adc_raw_value[2];
float voltage = 0.0f;
float current = 0.0f;

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
    if (server.get_new_init()) {
        initilized = true;
    }
    if (!initilized) {
        return;  // 初期化されていなければ動作しない
    }

    uint16_t current_raw = adc_raw_value[0];
    uint16_t vlotage_raw = adc_raw_value[1];
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