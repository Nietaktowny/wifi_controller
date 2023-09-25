#include "wifi_controller.h"
#include "nvs_flash.h"
#include "esp_log.h"

const char* MAIN = "main";

wifi_c_scan_result_t scan_results;
wifi_ap_record_t ap_record;

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(wifi_c_init_wifi(WIFI_C_MODE_STA));

    ESP_ERROR_CHECK(wifi_c_start_sta("DUMMY", "DUMMY"));
    //wait for some time to wifi start properly
    vTaskDelay(1000);
    
    while(1) {
      /*SCAN FOR ONE AP WITH WANTED SSID*/
      /*
      if(wifi_c_scan_for_ap_with_ssid("SSID_TO_SEARCH_FOR", &ap_record)) {
        //SUCCESS
        //Do something with ap_record...
      }
      */

      /*SCAN ALL*/

      //Scan for AP on all channels and use pointers to wifi_ap_record_t structures in scan results to iterate
      ESP_ERROR_CHECK(wifi_c_scan_all_ap(&scan_results));

      //You can use wifi_c_print_scanned_ap to log scan results to monitor.
      ESP_ERROR_CHECK(wifi_c_print_scanned_ap());
      vTaskDelay(pdMS_TO_TICKS(3000));
    }
}