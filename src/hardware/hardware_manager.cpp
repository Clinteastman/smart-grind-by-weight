#include "hardware_manager.h"
#include "../controllers/grind_controller.h"
#include "../config/constants.h"

void HardwareManager::init() {
    preferences.begin("grinder", false);
    display_manager.init();
    weight_sensor.init(&preferences);
    grinder.init(HW_MOTOR_RELAY_PIN);

    grind_controller = nullptr; // Will be set later

    initialized = true;
}

void HardwareManager::update() {
    if (!initialized) return;
    
    // Each peripheral is updated by its own FreeRTOS task: the weight sensor
    // from WeightSamplingTask and the display from TaskManager's UI render
    // task, so there is nothing left to poll here.
}


