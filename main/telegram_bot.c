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
#include <inttypes.h>
#include "cJSON.h"


/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

/*Telegram configuration*/
#define TOKEN "------------------------------------------------"

#define chat_ID1 "-------------"
#define chat_ID2 "-------------"

/* TAGs for the system*/
static const char *TAG = "HTTP_CLIENT Handler";

char url_string[512] = "https://api.telegram.org/bot";
extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

esp_err_t _http_event_handler_sms(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // evt->user_data — это указатель на buffer[]
                strncat((char *)evt->user_data, (char *)evt->data, evt->data_len);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT to:");
            break;
    }
    return ESP_OK;
}
void check_bot_info(void) {
	char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
	char url[512] = "";
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = buffer,        // Pass address of local buffer to get response
    };
    strcat(url,url_string);
    strcat(url,"/getMe");
  
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_url(client, url);
   
    esp_http_client_set_method(client, HTTP_METHOD_GET);
   
    esp_err_t err = esp_http_client_perform(client);


    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",esp_http_client_get_status_code(client),esp_http_client_get_content_length(client));
        ESP_LOGW(TAG, "Desde Perform el output es: %s",buffer);
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "Cerrar Cliente");
    esp_http_client_close(client);
    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_cleanup(client);
}

void send_telegram_message(void) {


	char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0}; 
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
		.user_data = output_buffer,
    };
    //POST
    ESP_LOGW(TAG, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    strcat(url,url_string);
    strcat(url,"/sendMessage");
    esp_http_client_set_url(client, url);


	ESP_LOGW(TAG, "Enviare POST");
	
	//const char *post_data = "{\"chat_id\":1234567890,\"text\":\"Envio de post\"}";
	char post_data[512] = "";
	sprintf(post_data,"{\"chat_id\":%s,\"text\":\"test message from esp\"}",chat_ID2);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",esp_http_client_get_status_code(client),esp_http_client_get_content_length(client));
        ESP_LOGW(TAG, "Desde Perform el output es: %s",output_buffer);

    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    //ESP_LOGI(TAG, "esp_get_free_heap_size: %lld", esp_get_free_heap_size());
}
char* telegram_get_last_message_text(void) {
    static char message_text[256] = {0};  // buffer
    char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    char url[512] = "";

    strcpy(url, url_string);  // url_string = "https://api.telegram.org/bot<token>"
    strcat(url, "/getUpdates?limit=1&offset=-1");

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = buffer,  
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE("TELEGRAM", "ERROR getUpdates: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }

    esp_http_client_cleanup(client);

    ESP_LOGI("TELEGRAM", ">>>>: %s", buffer); 


    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        ESP_LOGE("TELEGRAM", "ERRO PARSE JSON");
        return NULL;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) == 0) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *item = cJSON_GetArrayItem(result, 0);
    cJSON *message = cJSON_GetObjectItem(item, "message");
    if (!cJSON_IsObject(message)) {
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (!cJSON_IsString(text)) {
        cJSON_Delete(json);
        return NULL;
    }

    strncpy(message_text, text->valuestring, sizeof(message_text) - 1);
    message_text[sizeof(message_text) - 1] = '\0';

    cJSON_Delete(json);
    return message_text;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
void http_test_task(void *pvParameters) {
    
	strcat(url_string,TOKEN);
    ESP_LOGW(TAG, "Wait 2 second before start");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG, "check_bot_info");
    check_bot_info();
    
    ESP_LOGW(TAG, "send_telegram_message");
    send_telegram_message();

    
    while (1){

        char *msg = telegram_get_last_message_text();
        if (msg) {
            ESP_LOGI("FROM_TELEGRAM_CHAT", ">>>>: %s", msg);
        } 
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    
    

    vTaskDelete(NULL);
}