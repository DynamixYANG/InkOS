#include "src/App.h"
void setup() {
  ink::start();
}
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
