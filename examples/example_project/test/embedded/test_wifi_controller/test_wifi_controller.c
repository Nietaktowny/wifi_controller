#include "unity.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_err.h"


#include "wifi_controller.h"

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void dummy_test(void) {
  TEST_PASS();
}

void run_wifi_controller_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(dummy_test);
  UNITY_END();
}

/**
  * For ESP-IDF framework
  */
void app_main(void) {
  run_wifi_controller_tests();
  vTaskDelay(200);
  fflush(stdout);
  esp_restart();
}