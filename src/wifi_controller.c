/*Beginning of ESP-IDF specific code.*/
#ifdef ESP_PLATFORM

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
/*
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
*/
#include <string.h>
#include "err_controller.h"
#include "wifi_controller.h"

static err_c_t wifi_c_init_netif(wifi_c_mode_t WIFI_C_WIFI_MODE);

static void wifi_c_ap_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

static void wifi_c_sta_event_handler (void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

static const char* LOG = "wifi_controller";

static wifi_c_status_t wifi_c_status = {
    .wifi_initialized = false,
    .netif_initialized = false,
    .wifi_mode = WIFI_C_NO_MODE,
    .even_loop_started = false,
    .wifi_started = false,
    .sta_started = false,
    .ap_started = false,
    .scan_done = false,
    .sta_connected = false,
};

static EventGroupHandle_t wifi_c_event_group;

static uint8_t wifi_sta_retry_num;

/*Variables needed for scan.*/
static wifi_ap_record_t ap_info[WIFI_C_DEFAULT_SCAN_SIZE];
static wifi_c_scan_result_t wifi_scan_info;

static void wifi_c_ap_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(LOG, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(LOG, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(LOG, "Total APs scanned: %u", wifi_scan_info.ap_count);
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT);
        wifi_c_status.scan_done = true;
    }
}

static void wifi_c_sta_event_handler (void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(LOG, "Station started, connecting to WiFi.");
        wifi_c_status.sta_started = true;
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_STA_STARTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if(wifi_sta_retry_num < WIFI_C_STA_RETRY_COUNT) {
            esp_wifi_connect();
            wifi_sta_retry_num++;
            ESP_LOGI(LOG, "Failed to connect to AP, trying again.");
        } else {
            ESP_LOGW(LOG, "Failed to connect to AP!");
            xEventGroupSetBits(wifi_c_event_group, WIFI_C_CONNECT_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(LOG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_c_status.sta_connected = true;
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_CONNECTED_BIT);
    }
}

static err_c_t wifi_c_init_netif(wifi_c_mode_t WIFI_C_WIFI_MODE) {
    volatile err_c_t err = ERR_C_OK;
    esp_netif_t *esp_netif_apsta;

    switch (WIFI_C_WIFI_MODE)
    {
    case WIFI_C_MODE_AP:
        esp_netif_apsta = esp_netif_create_default_wifi_ap();
        assert(esp_netif_apsta);
        wifi_c_status.wifi_mode = WIFI_C_MODE_AP;
        ESP_LOGD(LOG, "netif initialized as AP");
        break;
    case WIFI_C_MODE_STA:
        esp_netif_apsta = esp_netif_create_default_wifi_sta();
        assert(esp_netif_apsta);
        wifi_c_status.wifi_mode = WIFI_C_MODE_STA;
        ESP_LOGD(LOG, "netif initialized as STA");
        break;
    case WIFI_C_MODE_APSTA:
        esp_netif_apsta = esp_netif_create_default_wifi_ap();
        assert(esp_netif_apsta);

        esp_netif_apsta = esp_netif_create_default_wifi_sta();
        assert(esp_netif_apsta);

        wifi_c_status.wifi_mode = WIFI_C_MODE_APSTA;
        ESP_LOGD(LOG, "netif initialized as AP+STA");
        break;
    default:
        ESP_LOGE(LOG, "wifi_c_init_netif: Wrong wifi mode.");
        err = WIFI_C_ERR_NETIF_INIT_FAILED;
        break;
    }

    wifi_c_status.netif_initialized = true;
    return err;
}

static wifi_mode_t wifi_c_select_wifi_mode(wifi_c_mode_t WIFI_C_WIFI_MODE) {
    switch (WIFI_C_WIFI_MODE)
    {
    case WIFI_C_MODE_AP:
        return WIFI_MODE_AP;
        break;
    case WIFI_C_MODE_STA:
        return WIFI_MODE_STA;
        break;
    case WIFI_C_MODE_APSTA:
        return WIFI_MODE_APSTA;
        break;
    default:
        return WIFI_MODE_NULL;
        break;
    }
}

int wifi_c_create_default_event_loop(void) {
    volatile err_c_t err = ERR_C_OK;

    Try {
        ERR_C_CHECK_AND_THROW_ERR(esp_event_loop_create_default());

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_c_ap_event_handler,
                                                            NULL,
                                                            NULL));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_c_sta_event_handler,
                                                            NULL,
                                                            NULL));
        
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_c_sta_event_handler,
                                                            NULL,
                                                            NULL));

        wifi_c_status.even_loop_started = true;
    } Catch(err) {
        ESP_LOGE(LOG, "Error when creating default event loop: %d", err);
    }
    return err;
}

wifi_c_status_t *wifi_c_get_status(void) {
    return &wifi_c_status;
}

int wifi_c_init_wifi(wifi_c_mode_t WIFI_C_WIFI_MODE) {
    volatile err_c_t err = ERR_C_OK;
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    Try {
        if(wifi_c_status.wifi_initialized == true && wifi_c_status.wifi_mode == WIFI_C_WIFI_MODE) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_ALREADY_INIT);
        }
        ESP_ERROR_CHECK(esp_netif_init());
        wifi_c_event_group = xEventGroupCreate();
        ERR_C_CHECK_AND_THROW_ERR(wifi_c_create_default_event_loop());
        ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_netif(WIFI_C_WIFI_MODE));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_init(&wifi_init_config));
        ESP_LOGI(LOG, "Wifi initialized.");
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_mode(wifi_c_select_wifi_mode(WIFI_C_WIFI_MODE)));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_start());
        ESP_LOGD(LOG, "wifi successfully initialized");
        //Update wifi controller status.
        wifi_c_status.wifi_initialized = true;
        wifi_c_status.wifi_mode = WIFI_C_WIFI_MODE;
    } 
    Catch(err) {
        if(err == WIFI_C_ERR_WIFI_ALREADY_INIT) {
            ESP_LOGW(LOG, "WiFi already initialized.");
        } else {
            ESP_LOGE(LOG, "Error when initializing WiFi: %d", err);
        }
    }
    return err;
}

int wifi_c_start_ap(const char* ssid, const char* password) {
    volatile err_c_t err = ERR_C_OK;
    wifi_config_t wifi_ap_config = {
        .ap = {
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 6,
        }
    };

    Try {
        if(wifi_c_status.wifi_initialized != true) {
            ESP_LOGW(LOG, "WiFi not init, initializing...");
            ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_wifi(WIFI_C_MODE_AP));
        }

        if(wifi_c_status.wifi_mode == WIFI_C_MODE_STA) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE);
        }

        if(strlen(ssid) == 0) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_NULL_SSID);
        }
        
        if(strlen(password) == 0) {
            ESP_LOGW(LOG, "No password, setting wifi_auth_mode_t to WIFI_AUTH_OPEN.");
            wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
        } else if (strlen(password) < 8) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_PASSWORD);
        }
        else {
            if ((memcpy(&(wifi_ap_config.ap.password), password, sizeof(wifi_ap_config.ap.password))) != &(wifi_ap_config.ap.password)) {
                ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
            }
        }

        if ((memcpy(&(wifi_ap_config.ap.ssid), ssid, sizeof(wifi_ap_config.ap.ssid))) != &(wifi_ap_config.ap.ssid)) {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
        //ERR_C_CHECK_AND_THROW_ERR(esp_wifi_start());
        ESP_LOGI(LOG, "Started AP: \nSSID: %s \nPassword: %s", ssid, password);
        wifi_c_status.ap_started = true;
    } Catch(err) {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            ESP_LOGE(LOG, "Wrong Wifi mode.");
            break;
        case WIFI_C_ERR_NULL_SSID:
            ESP_LOGE(LOG, "SSID cannot be null");
            break;
        case ERR_C_MEMORY_ERR:
            ESP_LOGE(LOG, "Memory allocation was not successful");
            break;
        case WIFI_C_ERR_WRONG_PASSWORD:
            ESP_LOGE(LOG, "Password too short for WIFI_AUTH_WPA2_PSK.");
            break;
        default:
            ESP_LOGE(LOG, "Error when starting STA: %d, \nESP-IDF error: %s", err, esp_err_to_name(err));
            break;
        }

        memset(&wifi_ap_config, 0, sizeof(wifi_ap_config));
    }

    return err;
}

int wifi_c_start_sta(const char* ssid, const char* password) {
    volatile err_c_t err = ERR_C_OK;
    wifi_config_t wifi_sta_config = {
        .sta = {
            .failure_retry_cnt = WIFI_C_STA_RETRY_COUNT,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
        },
    };
    assert(ssid);

    Try {
        
        if(wifi_c_status.wifi_initialized != true) {
            ESP_LOGW(LOG, "WiFi not init, initializing...");
            ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_wifi(WIFI_C_MODE_STA));
        }

        if(wifi_c_status.wifi_mode == WIFI_C_MODE_AP) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE);
        }
        
        if(strlen(ssid) == 0) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_NULL_SSID);
        }
        
        if ((memcpy(&(wifi_sta_config.sta.ssid), ssid, sizeof(wifi_sta_config.sta.ssid))) != &(wifi_sta_config.sta.ssid)) {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        if ((memcpy(&(wifi_sta_config.sta.password), password, sizeof(wifi_sta_config.sta.password))) != &(wifi_sta_config.sta.password)) {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
        ESP_LOGD(LOG, "WiFi successfully configured as STA.");
        wifi_c_status.sta_started = true;

        /*Wait till sta started before trying to connect.*/
        xEventGroupWaitBits(wifi_c_event_group, WIFI_C_STA_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_connect());

        /*Wait for sta to finish connecting or timeout*/
        xEventGroupWaitBits(wifi_c_event_group, WIFI_C_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));

    } Catch(err) {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            ESP_LOGE(LOG, "Wrong Wifi mode.");
            break;
        case WIFI_C_ERR_NULL_SSID:
            ESP_LOGE(LOG, "SSID cannot be null");
            break;
        case ERR_C_MEMORY_ERR:
            ESP_LOGE(LOG, "Memory allocation was not successful");
            break;
        default:
            ESP_LOGE(LOG, "Error when starting STA: %d, \nESP-IDF error: %s", err, esp_err_to_name(err));
            break;
        }
        memset(&wifi_sta_config, 0, sizeof(wifi_sta_config));
    }

    return err;
}

int wifi_c_scan_all_ap(wifi_c_scan_result_t* result_to_return) {
    volatile err_c_t err = ERR_C_OK;
    wifi_scan_config_t scan_config = {
        //.ssid = NULL,                   //Search for AP with any SSID.
        //.bssid = NULL,                  //Search for AP with any BSSID.
        .show_hidden = 0                  //Don't  show hidden AP.
    };

    wifi_scan_info.ap_count = WIFI_C_DEFAULT_SCAN_SIZE;

    Try {
        assert(result_to_return);

        if(wifi_c_status.wifi_mode == WIFI_C_MODE_AP) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE); // scans are only allowed in STA mode.
        }

        if(!wifi_c_status.wifi_initialized) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }

        if(!wifi_c_status.sta_started) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_STA_NOT_STARTED);
        }

        memset(&ap_info, 0, sizeof(ap_info));

        err = esp_wifi_scan_start(&scan_config, WIFI_C_SCAN_BLOCK);
        
        /*If ESP_ERR_WIFI_STATE was returned, it is possible that sta was connecting, then wait and try again.*/
        if(err == ESP_ERR_WIFI_STATE) {
            vTaskDelay(1000);
            ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_start(&scan_config, WIFI_C_SCAN_BLOCK));
        } else {
            ERR_C_CHECK_AND_THROW_ERR(err);
        }
        /*Wait for scan to finish before reading results.*/
        xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_get_ap_records(&wifi_scan_info.ap_count, &ap_info[0]));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_get_ap_num(&(wifi_scan_info.ap_count)));
        wifi_scan_info.ap_record = &ap_info[0];

        /*Point passed pointer to scan results*/
        result_to_return = &wifi_scan_info;
    } Catch (err) {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            ESP_LOGE(LOG, "Wrong Wifi mode, scanning only possible in STA mode.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            ESP_LOGE(LOG, "WiFi was not initialized.");
            break;   
        case WIFI_C_ERR_STA_NOT_STARTED:
            ESP_LOGE(LOG, "STA was not started.");
            break; 
        default:
            ESP_LOGE(LOG, "Error when scanning: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t) err));
            break;
        }
        memset(&ap_info, 0, sizeof(ap_info));
        memset(&wifi_scan_info, 0, sizeof(wifi_scan_info));
    }
    
    return err;
}

int wifi_c_scan_for_ap_with_ssid(const char* searched_ssid, wifi_ap_record_t* ap_record) {
    volatile err_c_t err = ERR_C_OK;
    assert(searched_ssid);
    wifi_ap_record_t* record;
    bool success = false;

    Try {
        assert(&wifi_scan_info);
        record = wifi_scan_info.ap_record;
        uint8_t ssid_len = strlen(searched_ssid);

        for (uint16_t i = 0; i < wifi_scan_info.ap_count; i++)
        {
            const char* ssid = (char*) (record->ssid);
            if(strncmp(searched_ssid, ssid, ssid_len) == 0) {
                ESP_LOGI(LOG, "Found %s AP.", searched_ssid);
                success = true;
                break;
            }
            record++;
        }
        if (success) {
            memcpy(ap_record, record, sizeof(wifi_ap_record_t));
        } else {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_AP_NOT_FOUND);
        }

    } Catch (err) {
        switch (err)
        {
        case WIFI_C_AP_NOT_FOUND:
            ESP_LOGW(LOG, "Not found desired AP.");
            break;
        default:
            ESP_LOGE(LOG, "Error when scanning: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t) err));
            break;
        }
    }
    
    return err;
}

int wifi_c_print_scanned_ap (void) {
    volatile err_c_t err = ERR_C_OK;
    Try {
        if(!(wifi_c_status.wifi_initialized)) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }
            
        /*If scan is not yet done, wait for a while before continuing
        Then bits, if it's again not done, then throw errror.*/
        if(!(wifi_c_status.scan_done)) {
            EventBits_t bits = xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            if((bits & WIFI_C_SCAN_DONE_BIT) != WIFI_C_SCAN_DONE_BIT) {
                ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_SCAN_NOT_DONE);
            }
        }
            
        wifi_ap_record_t* record = wifi_scan_info.ap_record;
        for (uint16_t i = 0; i < WIFI_C_DEFAULT_SCAN_SIZE; i++)
        {
            const char* ssid = (char*) (record->ssid); 
            int8_t rssi = record->rssi;
            ESP_LOGI(LOG, "SSID \t%s", ssid);
            ESP_LOGI(LOG, "RSSI \t%d", rssi);
            record++;
        }
    } Catch(err) {
        switch (err)
        {
        case WIFI_C_ERR_SCAN_NOT_DONE:
            ESP_LOGE(LOG, "Scan not done, init scan before getting results.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            ESP_LOGE(LOG, "WiFi was not initialized.");
            break;    
        default:
            ESP_LOGE(LOG, "Error when getting scan results: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t) err));
            break;
        }
    }

    return err;
}


int wifi_c_store_scanned_ap (char buffer[], uint16_t buflen) {
    volatile err_c_t err = ERR_C_OK;
    Try {
        if(!(wifi_c_status.wifi_initialized)) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }
            
        /*If scan is not yet done, wait for a while before continuing
        Then bits, if it's again not done, then throw errror.*/
        if(!(wifi_c_status.scan_done)) {
            EventBits_t bits = xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            if((bits & WIFI_C_SCAN_DONE_BIT) != WIFI_C_SCAN_DONE_BIT) {
                ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_SCAN_NOT_DONE);
            }
        }
        uint16_t space_left = buflen;
        uint16_t ssid_len = 0;
        const char* newline = "\n"; 
        wifi_ap_record_t* record = wifi_scan_info.ap_record;
        strncat(&buffer[0], newline, sizeof(strlen(newline)));
        for (uint16_t i = 0; i < WIFI_C_DEFAULT_SCAN_SIZE; i++)
        {
            char* ssid = (char*) (record->ssid);
            strncat(ssid, newline, sizeof(strlen(newline)));
            ssid_len = strlen(ssid);
            space_left = space_left - ssid_len;
            strncat(&buffer[1], ssid, space_left);
            record++;
        }
    } Catch(err) {
        switch (err)
        {
        case WIFI_C_ERR_SCAN_NOT_DONE:
            ESP_LOGE(LOG, "Scan not done, init scan before getting results.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            ESP_LOGE(LOG, "WiFi was not initialized.");
            break;    
        default:
            ESP_LOGE(LOG, "Error when getting scan results: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t) err));
            break;
        }
    }

    return err;
}

int wifi_c_deinit(void) {
    err_c_t err = ERR_C_OK;

    Try {
        if(!wifi_c_status.wifi_started) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_STARTED);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_stop());
        wifi_c_status.wifi_started = false;

        if(!wifi_c_status.wifi_initialized) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_deinit());
        wifi_c_status.wifi_initialized = false;

        if(!wifi_c_status.netif_initialized) {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_NEITF_NOT_INIT);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_netif_deinit());
        wifi_c_status.netif_initialized = false;

    } 
    Catch(err) {
        switch (err)
        {
        case WIFI_C_ERR_WIFI_NOT_STARTED:
            ESP_LOGE(LOG, "Wifi was not started.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            ESP_LOGE(LOG, "WiFi was not initialized.");
            break;
        case WIFI_C_NEITF_NOT_INIT:
            ESP_LOGE(LOG, "netif interface was not initialized.");
            break;        
        default:
            ESP_LOGE(LOG, "Error when deinitializing wifi controller.");
            break;
        }
    }

    return err;
}
#endif //ESP_PLATFORM