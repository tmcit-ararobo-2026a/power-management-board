#pragma once
#include "fdcan.h"
#include "gn10_can/drivers/fdcan_driver_interface.hpp"

class FDCANDriver : public gn10_can::drivers::IFDCANDriver
{
public:
    FDCANDriver(FDCAN_HandleTypeDef* hfdcan) : hfdcan_(hfdcan) {}

    bool init();
    bool send(const gn10_can::FDCANFrame& frame) override;
    bool receive(gn10_can::FDCANFrame& out_frame) override;

private:
    FDCAN_HandleTypeDef* hfdcan_;
};