/**
 * @file wifi_controller.c
 * @author Wojciech Mytych (wojciech.lukasz.mytych@gmail.com)
 * @brief Wifi controller source file.
 * @version 0.1
 * @date 2024-02-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */

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
#include "errors_list.h"
#include "wifi_controller.h"
#include "logger.h"
#include "memory_utils.h"

/**
 * @brief Initialize network interface.
 */
static err_c_t wifi_c_init_netif(wifi_c_mode_t WIFI_C_WIFI_MODE);

/**
 * @brief Default event handler for AP mode.
 */
static void wifi_c_ap_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data);

/**
 * @brief Default event handler for STA mode.
 */
static void wifi_c_sta_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data);

/**
 * @brief Check event group bits of connection status, and return result.
 */
static err_c_t wifi_c_check_sta_connection_result(uint16_t timeout_sec);

/**
 * @brief Deinit netif interfaces.
 */
static void wifi_c_netif_deinit(wifi_c_mode_t mode);

static wifi_c_status_t wifi_c_status = {
    .wifi_initialized = false,
    .netif_initialized = false,
    .wifi_mode = WIFI_C_NO_MODE,
    .even_loop_started = false,
    .sta_started = false,
    .ap_started = false,
    .scan_done = false,
    .sta_connected = false,
    .ap.ip = "0.0.0.0",
    .ap.ssid = "none",
    .ap.connect_handler = NULL,
    .sta.ip = "0.0.0.0",
    .sta.ssid = "none",
    .sta.connect_handler = NULL,
};

static EventGroupHandle_t wifi_c_event_group;

static uint8_t wifi_sta_retry_num;

/*Variables needed for scan.*/
static wifi_ap_record_t ap_info[WIFI_C_DEFAULT_SCAN_SIZE];
static wifi_c_scan_result_t wifi_scan_info;

// netif handles, needed for deinitialization
static esp_netif_t *netif_handle_sta = NULL;
static esp_netif_t *netif_handle_ap = NULL;

static void wifi_c_ap_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        LOG_INFO("Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        LOG_INFO("Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        LOG_INFO("Total APs scanned: %u", wifi_scan_info.ap_count);
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT);
        wifi_c_status.scan_done = true;
    }
}

static void wifi_c_sta_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        LOG_INFO("Station started, connecting to WiFi.");
        wifi_c_status.sta_started = true;
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_STA_STARTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (wifi_sta_retry_num < WIFI_C_STA_RETRY_COUNT)
        {
            esp_wifi_connect();
            wifi_sta_retry_num++;
            LOG_WARN("Failed to connect to AP, trying again.");
        }
        else
        {
            xEventGroupSetBits(wifi_c_event_group, WIFI_C_CONNECT_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(&(wifi_c_status.sta.ip[0]), IPSTR, IP2STR(&event->ip_info.ip));
        LOG_INFO("Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_c_status.sta_connected = true;
        xEventGroupSetBits(wifi_c_event_group, WIFI_C_CONNECTED_BIT);
        wifi_c_status.sta.connect_handler();
    }
}

static err_c_t wifi_c_check_sta_connection_result(uint16_t timeout_sec)
{
    /*Wait for sta to finish connecting or timeout*/
    EventBits_t bits = xEventGroupWaitBits(wifi_c_event_group, WIFI_C_CONNECTED_BIT | WIFI_C_CONNECT_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_sec * 1000));
    switch (bits)
    {
    case WIFI_C_CONNECTED_BIT | WIFI_C_STA_STARTED_BIT:
        LOG_DEBUG("WIFI_C_CONNECTED_BIT is set!");
        return 0;
    case WIFI_C_CONNECT_FAIL_BIT | WIFI_C_STA_STARTED_BIT:
        LOG_DEBUG("WIFI_C_CONNECT_FAIL_BIT is set!");
        return WIFI_C_ERR_STA_CONNECT_FAIL;
    case WIFI_C_STA_STARTED_BIT:
        LOG_DEBUG("WIFI_C_STA_STARTED_BIT is set, but timeout expired, connection failed");
        return WIFI_C_ERR_STA_TIMEOUT_EXPIRE;
    case 0:
        LOG_DEBUG("WIFI_C_STA_STARTED_BIT not set");
        return WIFI_C_ERR_STA_NOT_STARTED;
    default:
        LOG_DEBUG("i don't know which bits are set, see: %u", bits);
        return ERR_C_INVALID_ARGS;
    }
}

static err_c_t wifi_c_init_netif(wifi_c_mode_t WIFI_C_WIFI_MODE)
{
    volatile err_c_t err = ERR_C_OK;

    switch (WIFI_C_WIFI_MODE)
    {
    case WIFI_C_MODE_AP:
        netif_handle_ap = esp_netif_create_default_wifi_ap();
        assert(netif_handle_ap);
        wifi_c_status.wifi_mode = WIFI_C_MODE_AP;
        LOG_DEBUG("netif initialized as AP");
        break;
    case WIFI_C_MODE_STA:
        netif_handle_sta = esp_netif_create_default_wifi_sta();
        assert(netif_handle_sta);
        wifi_c_status.wifi_mode = WIFI_C_MODE_STA;
        LOG_DEBUG("netif initialized as STA");
        break;
    case WIFI_C_MODE_APSTA:
        netif_handle_ap = esp_netif_create_default_wifi_ap();
        assert(netif_handle_ap);

        netif_handle_sta = esp_netif_create_default_wifi_sta();
        assert(netif_handle_sta);

        wifi_c_status.wifi_mode = WIFI_C_MODE_APSTA;
        LOG_DEBUG("netif initialized as AP+STA");
        break;
    default:
        LOG_ERROR("wifi_c_init_netif: Wrong wifi mode.");
        err = WIFI_C_ERR_NETIF_INIT_FAILED;
        break;
    }

    wifi_c_status.netif_initialized = true;
    return err;
}

static wifi_mode_t wifi_c_select_wifi_mode(wifi_c_mode_t WIFI_C_WIFI_MODE)
{
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

int wifi_c_create_default_event_loop(void)
{
    volatile err_c_t err = ERR_C_OK;

    Try
    {
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
    }
    Catch(err)
    {
        LOG_ERROR("Error when creating default event loop: %d", err);
    }
    return err;
}

int wifi_c_sta_register_connect_handler(void (*connect_handler)(void))
{
    err_c_t err = 0;
    ERR_C_CHECK_NULL_PTR(connect_handler, LOG_ERROR("connect handler function cannot be NULL"));

    wifi_c_status.sta.connect_handler = connect_handler;
    LOG_INFO("connect handler function of wifi controller changed!");
    return err;
}

wifi_c_status_t *wifi_c_get_status(void)
{
    return &wifi_c_status;
}

static inline char *wifi_c_get_bool_as_char(bool value)
{
    return (value) ? "true" : "false";
}

int wifi_c_get_status_as_json(char *buffer, size_t buflen)
{
    err_c_t err = 0;

    ERR_C_CHECK_NULL_PTR(buffer, LOG_ERROR("buffer to store wifi_c_status as JSON cannot be NULL"));
    memutil_zero_memory(buffer, buflen);

    LOG_DEBUG("storing wifi_c_status structure as JSON string...");

    snprintf(buffer, buflen, "{\"wifi_initialized\": %s, \"netif_initialized\":%s, \"wifi_mode\": \"%s\", \"event_loop_started\": %s, \"sta_started\": %s, \"ap_started\": %s, \"scan_done\": %s, \"sta_connected\":%s, \"sta_ip\": \"%s\", \"sta_ssid\": \"%s\", \"ap_ip\": \"%s\", \"ap_ssid\": \"%s\"}",
             wifi_c_get_bool_as_char(wifi_c_status.wifi_initialized),
             wifi_c_get_bool_as_char(wifi_c_status.netif_initialized),
             wifi_c_get_wifi_mode_as_string(wifi_c_status.wifi_mode),
             wifi_c_get_bool_as_char(wifi_c_status.even_loop_started),
             wifi_c_get_bool_as_char(wifi_c_status.sta_started),
             wifi_c_get_bool_as_char(wifi_c_status.ap_started),
             wifi_c_get_bool_as_char(wifi_c_status.scan_done),
             wifi_c_get_bool_as_char(wifi_c_status.sta_connected),
             wifi_c_get_sta_ipv4(),
             wifi_c_sta_get_ap_ssid(),
             wifi_c_get_ap_ipv4(),
             wifi_c_ap_get_ssid());
    LOG_DEBUG("wifi_c_status structure as JSON: \n%s", buffer);
    return err;
}

char *wifi_c_get_wifi_mode_as_string(wifi_c_mode_t wifi_mode)
{
    switch (wifi_mode)
    {
    case WIFI_C_MODE_AP:
        return "WIFI_C_MODE_AP";
    case WIFI_C_MODE_STA:
        return "WIFI_C_MODE_STA";
    case WIFI_C_MODE_APSTA:
        return "WIFI_C_MODE_APSTA";
    default:
        LOG_ERROR("not known wifi_c_mode_t: %d, cannot translate to string.", wifi_mode);
        break;
    }
    return NULL;
}

char *wifi_c_get_sta_ipv4(void)
{
    return wifi_c_status.sta.ip;
}

char *wifi_c_get_ap_ipv4(void)
{
    return wifi_c_status.ap.ip;
}

char *wifi_c_sta_get_ap_ssid(void)
{
    return wifi_c_status.sta.ssid;
}

char *wifi_c_ap_get_ssid(void)
{
    return wifi_c_status.ap.ssid;
}

int wifi_c_init_wifi(wifi_c_mode_t WIFI_C_WIFI_MODE)
{
    volatile err_c_t err = ERR_C_OK;
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    Try
    {
        if (wifi_c_status.wifi_initialized == true && wifi_c_status.wifi_mode == WIFI_C_WIFI_MODE)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_ALREADY_INIT);
        } else if(wifi_c_status.wifi_initialized == true && wifi_c_status.wifi_mode != WIFI_C_WIFI_MODE) {
            wifi_c_deinit();    //if it's init with different mode, deinit and init with new wanted mode
        }

        ESP_ERROR_CHECK(esp_netif_init());
        wifi_c_event_group = xEventGroupCreate();
        ERR_C_CHECK_AND_THROW_ERR(wifi_c_create_default_event_loop());
        ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_netif(WIFI_C_WIFI_MODE));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_init(&wifi_init_config));
        LOG_INFO("Wifi initialized.");
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_mode(wifi_c_select_wifi_mode(WIFI_C_WIFI_MODE)));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_start());
        LOG_DEBUG("wifi successfully initialized");
        // Update wifi controller status.
        wifi_c_status.wifi_initialized = true;
        wifi_c_status.wifi_mode = WIFI_C_WIFI_MODE;
    }
    Catch(err)
    {
        if (err == WIFI_C_ERR_WIFI_ALREADY_INIT)
        {
            LOG_WARN("WiFi already initialized.");
        }
        else
        {
            LOG_ERROR("Error when initializing WiFi: %d", err);
        }
    }
    return err;
}

int wifi_c_start_ap(const char *ssid, const char *password)
{
    volatile err_c_t err = ERR_C_OK;
    wifi_config_t wifi_ap_config = {
        .ap = {
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 6,
        }};

    Try
    {
        if (wifi_c_status.wifi_initialized != true)
        {
            LOG_WARN("WiFi not init, initializing...");
            ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_wifi(WIFI_C_MODE_AP));
        }

        if (wifi_c_status.wifi_mode == WIFI_C_MODE_STA)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE);
        }

        if (strlen(ssid) == 0)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_NULL_SSID);
        }

        if(password == NULL) {
            LOG_WARN("No password, setting wifi_auth_mode_t to WIFI_AUTH_OPEN.");
            wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;            
        }
        else if (strlen(password) == 0)
        {
            LOG_WARN("No password, setting wifi_auth_mode_t to WIFI_AUTH_OPEN.");
            wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
        }
        else if (strlen(password) < 8)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_PASSWORD);
        }
        else
        {
            if ((memcpy(&(wifi_ap_config.ap.password), password, sizeof(wifi_ap_config.ap.password))) != &(wifi_ap_config.ap.password))
            {
                ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
            }
        }

        if ((memcpy(&(wifi_ap_config.ap.ssid), ssid, sizeof(wifi_ap_config.ap.ssid))) != &(wifi_ap_config.ap.ssid))
        {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
        // ERR_C_CHECK_AND_THROW_ERR(esp_wifi_start());
        if(password != NULL) {
            LOG_INFO("Started AP: \nSSID: %s \nPassword: %s", ssid, password);
        } else {
            LOG_INFO("Started AP: \nSSID: %s \nNo Password", ssid);
        }

        // update wifi_c_status
        wifi_c_status.ap_started = true;

        memutil_zero_memory(&(wifi_c_status.ap.ssid), sizeof(wifi_c_status.ap.ssid));
        memcpy(&(wifi_c_status.ap.ssid), ssid, strlen(ssid));

        memutil_zero_memory(&(wifi_c_status.ap.ip), sizeof(wifi_c_status.ap.ip));
        memcpy(&(wifi_c_status.ap.ip), "192.168.4.1", strlen("192.168.4.1")); // use standard address got by DHCP
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            LOG_ERROR("Wrong Wifi mode.");
            break;
        case WIFI_C_ERR_NULL_SSID:
            LOG_ERROR("SSID cannot be null");
            break;
        case ERR_C_MEMORY_ERR:
            LOG_ERROR("Memory allocation was not successful");
            break;
        case WIFI_C_ERR_WRONG_PASSWORD:
            LOG_ERROR("Password too short for WIFI_AUTH_WPA2_PSK.");
            break;
        default:
            LOG_ERROR("Error when starting STA: %d, \nESP-IDF error: %s", err, esp_err_to_name(err));
            break;
        }

        memset(&wifi_ap_config, 0, sizeof(wifi_ap_config));
    }

    return err;
}

/**
 * @todo changing connection timeout time
 */
int wifi_c_start_sta(const char *ssid, const char *password)
{
    volatile err_c_t err = ERR_C_OK;
    wifi_config_t wifi_sta_config = {
        .sta = {
            .failure_retry_cnt = 1, // WIFI_C_STA_RETRY_COUNT,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
        },
    };
    assert(ssid);

    Try
    {

        if (wifi_c_status.wifi_initialized != true)
        {
            LOG_WARN("WiFi not init, initializing...");
            ERR_C_CHECK_AND_THROW_ERR(wifi_c_init_wifi(WIFI_C_MODE_STA));
        }

        if (wifi_c_status.wifi_mode == WIFI_C_MODE_AP)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE);
        }

        if (strlen(ssid) == 0)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_NULL_SSID);
        }

        if ((memcpy(&(wifi_sta_config.sta.ssid), ssid, sizeof(wifi_sta_config.sta.ssid))) != &(wifi_sta_config.sta.ssid))
        {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        if ((memcpy(&(wifi_sta_config.sta.password), password, sizeof(wifi_sta_config.sta.password))) != &(wifi_sta_config.sta.password))
        {
            ERR_C_SET_AND_THROW_ERR(err, ERR_C_MEMORY_ERR);
        }

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
        LOG_DEBUG("WiFi successfully configured as STA.");
        wifi_c_status.sta_started = true;

        /*Wait till sta started before trying to connect.*/
        xEventGroupWaitBits(wifi_c_event_group, WIFI_C_STA_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));

        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_connect());

        /*Wait for sta to finish connecting or timeout*/
        ERR_C_CHECK_AND_THROW_ERR(wifi_c_check_sta_connection_result(60));

        // update AP of ssid we are connected to in status
        memutil_zero_memory(&(wifi_c_status.sta.ssid), sizeof(wifi_c_status.sta.ssid));
        memcpy(&(wifi_c_status.sta.ssid), ssid, strlen(ssid));
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            LOG_ERROR("Wrong Wifi mode.");
            break;
        case WIFI_C_ERR_NULL_SSID:
            LOG_ERROR("SSID cannot be null");
            break;
        case ERR_C_MEMORY_ERR:
            LOG_ERROR("Memory allocation was not successful");
            break;
        case WIFI_C_ERR_STA_NOT_STARTED:
            LOG_ERROR("STA didn't start properly");
            break;
        case WIFI_C_ERR_STA_CONNECT_FAIL:
            LOG_ERROR("All attempts to connect to Wifi failed");
            break;
        case WIFI_C_ERR_STA_TIMEOUT_EXPIRE:
            LOG_ERROR("Failed to connect before timeout expired, returning...");
            break;
        default:
            LOG_ERROR("Error when starting STA: %d, \nESP-IDF error: %s", err, esp_err_to_name(err));
            break;
        }
        memset(&wifi_sta_config, 0, sizeof(wifi_sta_config));
    }

    return err;
}

/**
 * @todo return only needed number of scan results
 * @todo use memory arena for storing scan results
 */
int wifi_c_scan_all_ap(wifi_c_scan_result_t *result_to_return)
{
    volatile err_c_t err = ERR_C_OK;
    wifi_scan_config_t scan_config = {
        .show_hidden = 0 // Don't  show hidden AP.
    };

    wifi_scan_info.ap_count = WIFI_C_DEFAULT_SCAN_SIZE;

    Try
    {
        ERR_C_CHECK_NULL_PTR(result_to_return, LOG_ERROR("pointer to scan result buffer cannot be NULL"));

        if (!wifi_c_status.wifi_initialized)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }

        if (wifi_c_status.wifi_mode == WIFI_C_MODE_AP)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WRONG_MODE); // scans are only allowed in STA mode.
        }

        if (!wifi_c_status.sta_started)
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_STA_NOT_STARTED);
        }

        memset(&ap_info, 0, sizeof(ap_info));
        LOG_DEBUG("scanning for Access Points...");

        err = esp_wifi_scan_start(&scan_config, WIFI_C_SCAN_BLOCK);

        /*If ESP_ERR_WIFI_STATE was returned, it is possible that sta was connecting, then wait and try again.*/
        if (err == ESP_ERR_WIFI_STATE)
        {
            vTaskDelay(1000);
            ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_start(&scan_config, WIFI_C_SCAN_BLOCK));
        }
        else
        {
            ERR_C_CHECK_AND_THROW_ERR(err);
        }
        /*Wait for scan to finish before reading results.*/
        xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_get_ap_records(&wifi_scan_info.ap_count, &ap_info[0]));
        ERR_C_CHECK_AND_THROW_ERR(esp_wifi_scan_get_ap_num(&(wifi_scan_info.ap_count)));
        wifi_scan_info.ap_record = &ap_info[0];

        /*Point passed pointer to scan results*/
        result_to_return = &wifi_scan_info;
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_WRONG_MODE:
            LOG_ERROR("Wrong Wifi mode, scanning only possible in STA mode.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            LOG_ERROR("WiFi was not initialized.");
            break;
        case WIFI_C_ERR_STA_NOT_STARTED:
            LOG_ERROR("STA was not started.");
            break;
        default:
            LOG_ERROR("Error when scanning: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t)err));
            break;
        }
        // Clear AP list found in last scan
        memset(&ap_info, 0, sizeof(ap_info));
        memset(&wifi_scan_info, 0, sizeof(wifi_scan_info));
        esp_wifi_clear_ap_list();
    }

    return err;
}

int wifi_c_scan_for_ap_with_ssid(const char *searched_ssid, wifi_c_ap_record_t *ap_record)
{
    volatile err_c_t err = ERR_C_OK;
    assert(searched_ssid);
    wifi_ap_record_t *record;
    bool success = false;

    Try
    {
        assert(&wifi_scan_info);
        record = wifi_scan_info.ap_record;
        uint8_t ssid_len = strlen(searched_ssid);

        for (uint16_t i = 0; i < wifi_scan_info.ap_count; i++)
        {
            const char *ssid = (char *)(record->ssid);
            if (strncmp(searched_ssid, ssid, ssid_len) == 0)
            {
                LOG_INFO("Found %s AP.", searched_ssid);
                success = true;
                break;
            }
            record++;
        }
        if (success)
        {
            memcpy(ap_record, record, sizeof(wifi_ap_record_t));
        }
        else
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_AP_NOT_FOUND);
        }
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_AP_NOT_FOUND:
            LOG_WARN("Not found desired AP.");
            break;
        default:
            LOG_ERROR("Error when scanning: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t)err));
            esp_wifi_clear_ap_list(); // Clear AP list found in last scan
            break;
        }
    }

    return err;
}

/**
 * @todo Change print format.
 */
int wifi_c_print_scanned_ap(void)
{
    volatile err_c_t err = ERR_C_OK;
    Try
    {
        if (!(wifi_c_status.wifi_initialized))
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }

        /*If scan is not yet done, wait for a while before continuing
        Then bits, if it's again not done, then throw errror.*/
        if (!(wifi_c_status.scan_done))
        {
            EventBits_t bits = xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            if ((bits & WIFI_C_SCAN_DONE_BIT) != WIFI_C_SCAN_DONE_BIT)
            {
                ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_SCAN_NOT_DONE);
            }
        }

        wifi_ap_record_t *record = wifi_scan_info.ap_record;
        for (uint16_t i = 0; i < WIFI_C_DEFAULT_SCAN_SIZE; i++)
        {
            const char *ssid = (char *)(record->ssid);
            int8_t rssi = record->rssi;
            LOG_INFO("SSID \t%s", ssid);
            LOG_INFO("RSSI \t%d", rssi);
            record++;
        }
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_SCAN_NOT_DONE:
            LOG_ERROR("Scan not done, init scan before getting results.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            LOG_ERROR("WiFi was not initialized.");
            break;
        default:
            LOG_ERROR("Error when getting scan results: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t)err));
            break;
        }
    }

    return err;
}

int wifi_c_store_scan_result_as_json(char *buffer, uint16_t buflen)
{
    volatile err_c_t err = ERR_C_OK;
    ERR_C_CHECK_NULL_PTR(buffer, LOG_ERROR("buffer to store scanned APs cannot be NULL"));
    Try
    {
        if (!(wifi_c_status.wifi_initialized))
        {
            ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_WIFI_NOT_INIT);
        }

        /*If scan is not yet done, wait for a while before continuing
        Then bits, if it's again not done, then throw errror.*/
        if (!(wifi_c_status.scan_done))
        {
            EventBits_t bits = xEventGroupWaitBits(wifi_c_event_group, WIFI_C_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            if ((bits & WIFI_C_SCAN_DONE_BIT) != WIFI_C_SCAN_DONE_BIT)
            {
                ERR_C_SET_AND_THROW_ERR(err, WIFI_C_ERR_SCAN_NOT_DONE);
            }
        }

        char ap[100];
        uint16_t ap_len = 0;
        uint16_t space_left = buflen;
        uint16_t index = 0;
        wifi_ap_record_t *record = wifi_scan_info.ap_record;
        for (uint16_t i = 0; i < WIFI_C_DEFAULT_SCAN_SIZE; i++)
        {
            memutil_zero_memory(&ap, sizeof(ap));
            char *ssid = (char *)(record->ssid);

            sprintf(&ap[0], ", {\"ssid\": \"%s\", \"rssi\": %d}", ssid, record->rssi);
            ap_len = strlen(&ap[0]);
            memcpy(&(buffer[index]), &ap[0], ap_len);
            space_left -= ap_len;
            index += ap_len;
            record++;
        }
        // delete first comma nad insert json array
        buffer[index++] = ']';
        buffer[0] = '[';

        // null terminate end result
        buffer[index] = '\0';
    }
    Catch(err)
    {
        switch (err)
        {
        case WIFI_C_ERR_SCAN_NOT_DONE:
            LOG_ERROR("Scan not done, init scan before getting results.");
            break;
        case WIFI_C_ERR_WIFI_NOT_INIT:
            LOG_ERROR("WiFi was not initialized.");
            break;
        default:
            LOG_ERROR("Error when getting scan results: %d \nESP-IDF error: %s", err, esp_err_to_name((esp_err_t)err));
            break;
        }
    }

    return err;
}

int wifi_c_disconnect(void)
{
    err_c_t err = 0;
    err = esp_wifi_disconnect();
    if (err != ESP_OK)
    {
        LOG_ERROR("error %d when trying to disconnect: %s", err, error_to_name(err));
        return err;
    }
    wifi_c_status.sta_connected = false;

    // update IP
    memutil_zero_memory(&(wifi_c_status.sta.ip), sizeof(wifi_c_status.sta.ip));
    memcpy(&(wifi_c_status.sta.ip), "0.0.0.0", strlen("0.0.0.0"));

    // update ap_ssid
    memutil_zero_memory((&wifi_c_status.sta.ssid), sizeof(wifi_c_status.sta.ssid));
    memcpy(&(wifi_c_status.sta.ssid), "none", strlen("none"));

    return err;
}

int wifi_c_change_mode(wifi_c_mode_t mode)
{
    err_c_t err = 0;
    if (wifi_c_status.wifi_mode == mode)
    {
        LOG_WARN("mode to set is the same as current mode");
        return WIFI_C_ERR_WRONG_MODE;
    }
    err = esp_wifi_set_mode(wifi_c_select_wifi_mode(mode));
    if (err != ERR_C_OK)
    {
        LOG_ERROR("error %d when changing wifi mode: %s", err, error_to_name(err));
        return err;
    }
    return err;
}

static void wifi_c_netif_deinit(wifi_c_mode_t mode)
{
    switch (mode)
    {
    case WIFI_C_MODE_STA:
        esp_netif_destroy_default_wifi(netif_handle_sta);
        break;
    case WIFI_C_MODE_AP:
        esp_netif_destroy_default_wifi(netif_handle_ap);
        break;
    case WIFI_C_MODE_APSTA:
        esp_netif_destroy_default_wifi(netif_handle_ap);
        esp_netif_destroy_default_wifi(netif_handle_sta);
        break;
    default:
        break;
    }
}

/**
 * Mostly when we are deinitializing wifi interface in our application, it means something
 * somewhere has gone really bad, and we are doing panic exit.
 * This function was also created with this assumption, so no error checking is done here.
 */
void wifi_c_deinit(void)
{
    LOG_DEBUG("Deinitializing wifi_controller...");
    if (wifi_c_status.sta_connected)
    {
        esp_wifi_disconnect();
        LOG_DEBUG("disconnected sta from AP...");
    }
    if (wifi_c_status.wifi_initialized)
    {
        esp_wifi_stop();
        esp_wifi_deinit();
        LOG_DEBUG("stopped and deinitialized wifi...");
    }
    if (wifi_c_status.netif_initialized)
    {
        wifi_c_netif_deinit(wifi_c_status.wifi_mode);
        LOG_DEBUG("netif deinitialized...");
    }

    if (wifi_c_status.even_loop_started)
    {
        vEventGroupDelete(wifi_c_event_group);
        esp_event_loop_delete_default();
        LOG_DEBUG("wifi_c_event loop destroyed...");
    }

    // at least clear wifi_c_status state
    wifi_c_status.wifi_initialized = false;
    wifi_c_status.netif_initialized = false;
    wifi_c_status.wifi_mode = WIFI_C_NO_MODE;
    wifi_c_status.even_loop_started = false;
    wifi_c_status.sta_started = false;
    wifi_c_status.ap_started = false;
    wifi_c_status.scan_done = false;
    wifi_c_status.sta_connected = false;
    wifi_c_status.sta.connect_handler = NULL;
    wifi_c_status.ap.connect_handler = NULL;
    memcpy(wifi_c_status.ap.ip, "0.0.0.0", 8);
    memcpy(wifi_c_status.sta.ip, "0.0.0.0", 8);
    memcpy(wifi_c_status.ap.ssid, "none", 5);
    memcpy(wifi_c_status.sta.ssid, "none", 5);
    LOG_WARN("wifi_controller deinitialized");
}
#endif // ESP_PLATFORM
