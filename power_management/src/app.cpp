#include "power_management/app.hpp"

#include "gn10_can/core/fdcan_bus.hpp"
#include "gn10_can/devices/power_manager_server.hpp"
#include "power_management/fdcan_driver.hpp"

FDCANDriver fdcan_driver(&hfdcan1);
gn10_can::FDCANBus fdcan_bus(fdcan_driver);
gn10_can::devices::PowerManagerServer power_manager_server(fdcan_bus, 0);

void setup()
{
    fdcan_driver.init();
}

void loop() {}

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