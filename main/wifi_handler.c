#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include "my_configs.h"
#include "wifi_handler.h"

#define ESP_MAXIMUM_RETRY  10

device_status_t device_status;

static int attemp_connect = 0;
static const char *TAG = "WI-FI";

static bool wifi_initialized = false;

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {// start connect
        ESP_LOGI(TAG, "Wi-Fi STA started");
        esp_wifi_connect(); 
        attemp_connect = 0;
    } 
    else 
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from Wi-Fi, retrying...");
        if(attemp_connect < 2){
            esp_wifi_connect(); 
            attemp_connect ++;
        }
        else{
            device_status.connected = false;
            ESP_LOGW(TAG, "--------------SSID 0R PASS ERROR -------------");
        }
        ESP_LOGI("","attemp_connect >> %d\n",attemp_connect);
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        device_status.connected = true;
        ESP_LOGW("", "\n============-- WI - FI Connected --==============\n");
        
    }
}
esp_err_t connect_to_wifi(char *ssid,char *password) {
    ESP_LOGW("", "\n=============-- Start STA Mode --================\n");
    if (wifi_initialized) {
        ESP_LOGW(TAG, "Wi-Fi already initialized, skipping.");
        return ESP_OK;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  
    esp_wifi_init(&cfg);
    esp_netif_create_default_wifi_sta();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  //  STA
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());  // start Wi-Fi
    ESP_LOGI(TAG, "Wi-Fi initialization complete.");
    return ESP_OK;
}