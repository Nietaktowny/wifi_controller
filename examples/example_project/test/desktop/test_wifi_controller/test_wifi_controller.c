#include "unity.h"
#include "wifi_controller.h"

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void test_should_check_if_wifi_mode_is_set_to_apsta_mode(void) {
    //given
    wifi_c_status_t wifi_c_test_status;

    //when
    wifi_c_test_status.wifi_mode = WIFI_C_MODE_APSTA;

    //then
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_C_MODE_APSTA, wifi_c_test_status.wifi_mode, "wifi_c_test_status.wifi_mode should be set to WIFI_C_MODE_APSTA.");
}

void test_should_check_if_wifi_mode_is_set_to_sta_mode(void) {
    //given
    wifi_c_status_t wifi_c_test_status;

    //when
    wifi_c_test_status.wifi_mode = WIFI_C_MODE_STA;

    //then
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_C_MODE_STA, wifi_c_test_status.wifi_mode, "wifi_c_test_status.wifi_mode should be set to WIFI_C_MODE_STA.");
}

void test_should_check_if_wifi_mode_is_set_to_ap_mode(void) {
    //given
    wifi_c_status_t wifi_c_test_status;

    //when
    wifi_c_test_status.wifi_mode = WIFI_C_MODE_AP;

    //then
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_C_MODE_AP, wifi_c_test_status.wifi_mode, "wifi_c_test_status.wifi_mode should be set to WIFI_C_MODE_AP.");
}

void test_should_check_if_wifi_mode_is_set_to_no_mode(void) {
    //given
    wifi_c_status_t wifi_c_test_status;

    //when
    wifi_c_test_status.wifi_mode = WIFI_C_NO_MODE;

    //then
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_C_NO_MODE, wifi_c_test_status.wifi_mode, "wifi_c_test_status.wifi_mode should be set to WIFI_C_NO_MODE.");
}

void test_should_check_if_wifi_initialized_is_declared(void) {
    //given
    wifi_c_status_t wifi_c_test_status;

    //when
    wifi_c_test_status.wifi_initialized = true;

    //then
    TEST_ASSERT_EQUAL_MESSAGE(true, wifi_c_test_status.wifi_initialized, "wifi_c_test_status.wifi_initialized should be set to true.");
}

void test_should_check_if_get_wifi_status_returns_not_null(void) {
    //given
    wifi_c_status_t* wifi_c_status;

    //when
    wifi_c_status = wifi_c_get_status();

    //then
    TEST_ASSERT_NOT_NULL_MESSAGE(wifi_c_status, "wifi_c_get_status should not return NULL pointer.");
}

void test_should_check_if_wifi_c_status_t_is_declared(void) {
  //given
  wifi_c_status_t wifi_c_test_status;
  //then
  TEST_PASS();
}


/*To add test use: RUN_TEST(test_name) macro.*/
int run_wifi_c_desktop_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_should_check_if_wifi_c_status_t_is_declared);
  RUN_TEST(test_should_check_if_get_wifi_status_returns_not_null);
  RUN_TEST(test_should_check_if_wifi_initialized_is_declared);
  RUN_TEST(test_should_check_if_wifi_mode_is_set_to_no_mode);
  RUN_TEST(test_should_check_if_wifi_mode_is_set_to_ap_mode);
  RUN_TEST(test_should_check_if_wifi_mode_is_set_to_sta_mode);
  RUN_TEST(test_should_check_if_wifi_mode_is_set_to_apsta_mode);
  return UNITY_END();
}

/**
  * main function for native dev-platform
  */
int main(void) {
  run_wifi_c_desktop_tests();
}