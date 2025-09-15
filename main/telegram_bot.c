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
#include "my_configs.h"
#include "telegram_bot.h"

#define TAG  "TELEGRAM_BOT"

/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define MAX_COMMANDS 10   // max command 

#define MAX_BUTTONS 20
#define MAX_CALLBACK_LEN 64


typedef struct {
    char command[32];
    char description[64];
} telegram_command_t;

static telegram_command_t commands[MAX_COMMANDS];
static int command_count = 0;

telegram_bot_info_t bot_info;

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
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
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
static void escape_json_string(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        switch (src[i]) {
            case '\"': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = '\"'; break;
            case '\\': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = '\\'; break;
            case '\b': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = 'b'; break;
            case '\f': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = 'f'; break;
            case '\n': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = 'n'; break;
            case '\r': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = 'r'; break;
            case '\t': dst[j++] = '\\'; if (j < dst_size - 1) dst[j++] = 't'; break;
            default: dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}
bool send_telegram_message_with_buttons(const char* chat_id, const char* message_text, const char* buttons[], int num_buttons, int layout_type) {
    if (num_buttons > MAX_BUTTONS) num_buttons = MAX_BUTTONS;

    char url[1024];  // увеличенный буфер для URL
    char output_buffer[2048] = {0};

    int n = snprintf(url, sizeof(url), "%s/sendMessage", url_string);
    if (n < 0 || n >= sizeof(url)) {
        ESP_LOGE(TAG, "URL слишком длинный!");
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = output_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Экранируем текст сообщения
    char safe_text[1024];
    escape_json_string(message_text, safe_text, sizeof(safe_text));

    // Создаём JSON клавиатуру
    cJSON *keyboard = cJSON_CreateObject();
    cJSON *inline_keyboard = cJSON_CreateArray();

    if (layout_type == 0) {
        // Горизонтально
        cJSON *row = cJSON_CreateArray();
        for (int i = 0; i < num_buttons; i++) {
            cJSON *button = cJSON_CreateObject();

            char safe_button[128];
            escape_json_string(buttons[i], safe_button, sizeof(safe_button));

            cJSON_AddStringToObject(button, "text", safe_button);

            char cb_data[MAX_CALLBACK_LEN];
            strncpy(cb_data, safe_button, MAX_CALLBACK_LEN - 1);
            cb_data[MAX_CALLBACK_LEN - 1] = '\0';
            cJSON_AddStringToObject(button, "callback_data", cb_data);

            cJSON_AddItemToArray(row, button);
        }
        cJSON_AddItemToArray(inline_keyboard, row);
    } else if (layout_type == 1) {
        // Вертикально
        for (int i = 0; i < num_buttons; i++) {
            cJSON *row = cJSON_CreateArray();
            cJSON *button = cJSON_CreateObject();

            char safe_button[128];
            escape_json_string(buttons[i], safe_button, sizeof(safe_button));

            cJSON_AddStringToObject(button, "text", safe_button);

            char cb_data[MAX_CALLBACK_LEN];
            strncpy(cb_data, safe_button, MAX_CALLBACK_LEN - 1);
            cb_data[MAX_CALLBACK_LEN - 1] = '\0';
            cJSON_AddStringToObject(button, "callback_data", cb_data);

            cJSON_AddItemToArray(row, button);
            cJSON_AddItemToArray(inline_keyboard, row);
        }
    }

    cJSON_AddItemToObject(keyboard, "inline_keyboard", inline_keyboard);
    char *keyboard_str = cJSON_PrintUnformatted(keyboard);
    cJSON_Delete(keyboard);

    char post_data[2048];
    int m = snprintf(post_data, sizeof(post_data),
                     "{\"chat_id\":\"%s\",\"text\":\"%s\",\"reply_markup\":%s}",
                     chat_id, safe_text, keyboard_str);
    free(keyboard_str);

    if (m < 0 || m >= sizeof(post_data)) {
        ESP_LOGE(TAG, "POST data слишком длинный!");
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status: %d", status);

        if (status == 200) {
            cJSON *json = cJSON_Parse(output_buffer);
            if (json) {
                cJSON *ok = cJSON_GetObjectItem(json, "ok");
                if (cJSON_IsBool(ok) && cJSON_IsTrue(ok)) {
                    success = true;
                }
                cJSON_Delete(json);
            }
        } else {
            ESP_LOGW(TAG, "Telegram API returned status: %d", status);
            ESP_LOGW(TAG, "Response: %s", output_buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return success;
}
void telegram_check_callback(void) {
    char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    char url[1024];         // больше места для URL
    char answer_url[1024];  // больше места для ответа

    // Формируем URL для getUpdates
    strlcpy(url, url_string, sizeof(url));
    strlcat(url, "/getUpdates?limit=1&offset=-1", sizeof(url));

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

    if (err == ESP_OK) {
        //ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client);
        //ESP_LOGW(TAG, "Response: %s", buffer);

        // парсим JSON
        cJSON *root = cJSON_Parse(buffer);
        if (root) {
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
                cJSON *update = cJSON_GetArrayItem(result, 0);
                cJSON *callback = cJSON_GetObjectItem(update, "callback_query");
                if (callback) {
                    cJSON *cb_id = cJSON_GetObjectItem(callback, "id");
                    cJSON *cb_data = cJSON_GetObjectItem(callback, "data");

                    if (cJSON_IsString(cb_id) && cJSON_IsString(cb_data)) {
                        //ESP_LOGI(TAG, "CallbackQuery ID: %s", cb_id->valuestring);
                        ESP_LOGI(TAG, "Callback Data: %s", cb_data->valuestring);

                        // Формируем URL для ответа
                        strlcpy(answer_url, url_string, sizeof(answer_url));
                        strlcat(answer_url, "/answerCallbackQuery?callback_query_id=", sizeof(answer_url));
                        strlcat(answer_url, cb_id->valuestring, sizeof(answer_url));

                        esp_http_client_set_url(client, answer_url);
                        esp_http_client_set_method(client, HTTP_METHOD_GET);

                        esp_err_t err2 = esp_http_client_perform(client);
                        if (err2 == ESP_OK) {
                            //ESP_LOGI(TAG, "Answered callback query OK");
                        } else {
                            ESP_LOGE(TAG, "Failed to answer callback query: %s", esp_err_to_name(err2));
                        }
                    }
                }
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}
bool delete_message(const char *chat_id, int message_id) {
    char url[1024];
    char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    strlcpy(url, url_string, sizeof(url));
    strlcat(url, "/deleteMessage", sizeof(url));
    snprintf(url + strlen(url), sizeof(url) - strlen(url),
             "?chat_id=%s&message_id=%d", chat_id, message_id);

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

    bool success = false;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Delete response: %s", buffer);
        if (strstr(buffer, "\"ok\":true")) {
            success = true;
        }
    } else {
        ESP_LOGE(TAG, "Delete request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return success;
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
bool send_telegram_message(const char* chat_id, const char* message_text) {
    char url[512] = "";
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = output_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    strcat(url, url_string);  // url_string = "https://api.telegram.org/bot<token>"
    strcat(url, "/sendMessage");
    esp_http_client_set_url(client, url);

    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "{\"chat_id\":\"%s\",\"text\":\"%s\"}", chat_id, message_text);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            cJSON *json = cJSON_Parse(output_buffer);
            if (json) {
                cJSON *ok = cJSON_GetObjectItem(json, "ok");
                if (cJSON_IsBool(ok) && cJSON_IsTrue(ok)) {
                    success = true;  // сообщение успешно отправлено

                    // сохраняем message_id
                    cJSON *result = cJSON_GetObjectItem(json, "result");
                    if (result) {
                        cJSON *msg_id = cJSON_GetObjectItem(result, "message_id");
                        if (cJSON_IsNumber(msg_id)) {
                            bot_info.last_message_id = msg_id->valueint;
                            strncpy(bot_info.current_chat_id, chat_id, sizeof(bot_info.current_chat_id) - 1);
                            ESP_LOGI(TAG, "Saved last_message_id = %d", bot_info.last_message_id);
                        }
                    }
                }
                cJSON_Delete(json);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!success) {
        ESP_LOGE(TAG, "Failed to send message to chat_id %s", chat_id);
    } else {
        ESP_LOGI(TAG, "Message sent to chat_id %s", chat_id);
    }

    return success;
}
esp_err_t telegram_delete_webhook(const char *bot_token) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/deleteWebhook", bot_token);

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = telegram_certificate_pem_start,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE("TELEGRAM", "Failed to delete webhook: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    char buffer[512];
    int content_len = esp_http_client_read_response(client, buffer, sizeof(buffer)-1);
    if (content_len > 0) {
        buffer[content_len] = '\0';
        ESP_LOGI("TELEGRAM", "deleteWebhook response: %s", buffer);
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
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

    //ESP_LOGI("TELEGRAM", ">>>>: %s", buffer); 


    cJSON *json = cJSON_Parse(buffer);
    if (!json) return NULL;

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

    // Парсим текст сообщения
    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (cJSON_IsString(text)) {
        strncpy(message_text, text->valuestring, sizeof(message_text) - 1);
        message_text[sizeof(message_text) - 1] = '\0';

        // Если новое сообщение отличается от последнего, обновляем
        if (strcmp(message_text, bot_info.last_msg) != 0) {
            strncpy(bot_info.last_msg, message_text, sizeof(bot_info.last_msg) - 1);
            bot_info.last_msg[sizeof(bot_info.last_msg) - 1] = '\0';
            strncpy(bot_info.current_msg, message_text, sizeof(bot_info.current_msg) - 1);
            bot_info.current_msg[sizeof(bot_info.current_msg) - 1] = '\0';
            ESP_LOGI("TELEGRAM", "New message detected: %s", bot_info.current_msg);
        } else {
            ESP_LOGI("TELEGRAM", "No new message");
            bot_info.current_msg[0] = '\0'; // current_msg пустое, потому что нет нового
        }
        
    }

    // Дополнительно можно логировать все доступные поля
    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    if (cJSON_IsObject(chat)) {
        cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
        if (cJSON_IsNumber(chat_id)){
            snprintf(bot_info.current_chat_id, sizeof(bot_info.current_chat_id), "%lld", (long long)chat_id->valuedouble);
            //ESP_LOGI("TELEGRAM", "chat_id: %lld", (long long)chat_id->valuedouble);
        } 

        cJSON *username = cJSON_GetObjectItem(chat, "username");
        if (cJSON_IsString(username)){
            strncpy(bot_info.sender_username, username->valuestring, sizeof(bot_info.sender_username) - 1);
            bot_info.sender_username[sizeof(bot_info.sender_username) - 1] = '\0';
            //ESP_LOGI("TELEGRAM", "username: %s", username->valuestring);
        } 
    }

    cJSON *date = cJSON_GetObjectItem(message, "date");
    if (cJSON_IsNumber(date)){
        time_t t = (time_t)date->valueint;        // timestamp Telegram
        struct tm ts;
        localtime_r(&t, &ts);                     // convert to local time
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S %d-%m-%Y", &ts);  
        ESP_LOGI("TELEGRAM", "date: %s", time_str);
    } 

    cJSON_Delete(json);
    return message_text;
}
void set_command(const char *cmd, const char *desc) {
    if (command_count < MAX_COMMANDS) {
        strncpy(commands[command_count].command, cmd, sizeof(commands[command_count].command)-1);
        strncpy(commands[command_count].description, desc, sizeof(commands[command_count].description)-1);
        command_count++;
        ESP_LOGI("TELEGRAM", "Добавлена команда: /%s -> %s", cmd, desc);
    } else {
        ESP_LOGW("TELEGRAM", "Лимит команд (%d) достигнут!", MAX_COMMANDS);
    }
}
void telegram_upload_commands(void) {
    char post_data[1024];
    strcpy(post_data, "{ \"commands\":[");

    for (int i = 0; i < command_count; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"command\":\"%s\",\"description\":\"%s\"}%s",
                 commands[i].command,
                 commands[i].description,
                 (i == command_count - 1) ? "" : ",");
        strcat(post_data, buf);
    }

    strcat(post_data, "]}");

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot%s/setMyCommands", TOKEN);

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = telegram_certificate_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("TELEGRAM", "setMyCommands -> HTTP status %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE("TELEGRAM", "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
void init_telegram_bot(void){
    strcat(url_string,TOKEN);
    // ESP_LOGW(TAG, "Wait 2 second before start");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG, "check_bot_info");
    check_bot_info();
    telegram_delete_webhook(TOKEN);
}
bool telegram_delete_message(const char* chat_id, int message_id) {
    if (chat_id == NULL || strlen(chat_id) == 0 || message_id <= 0) {
        ESP_LOGW(TAG, "Неверный chat_id или message_id");
        return false;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/deleteMessage", url_string);

    char post_data[256];
    snprintf(post_data, sizeof(post_data),
             "{\"chat_id\":%s,\"message_id\":%d}",
             chat_id, message_id);

    char output_buffer[2048] = {0};

    esp_http_client_config_t config = {
        .url = url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = NULL,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = output_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            cJSON *json = cJSON_Parse(output_buffer);
            if (json) {
                cJSON *ok = cJSON_GetObjectItem(json, "ok");
                if (cJSON_IsBool(ok) && cJSON_IsTrue(ok)) {
                    success = true;
                    ESP_LOGI(TAG, "Сообщение %d удалено в чате %s", message_id, chat_id);
                }
                cJSON_Delete(json);
            }
        } else {
            ESP_LOGW(TAG, "Telegram API вернул статус %d", status);
            ESP_LOGW(TAG, "Response: %s", output_buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return success;
}

// Удаление последних N сообщений по chat_id, используя last_message_id
bool telegram_delete_last_n_messages(const char* chat_id, int last_message_id, int n) {
    if (chat_id == NULL || last_message_id <= 0 || n <= 0) return false;

    bool all_success = true;
    for (int i = 0; i < n; i++) {
        if (!telegram_delete_message(chat_id, last_message_id - i)) {
            ESP_LOGW(TAG, "Не удалось удалить сообщение ID %d", last_message_id - i);
            all_success = false;
        }
    }
    return all_success;
}
// Удалить все сообщения от последнего до первого
void telegram_delete_all_messages(const char* chat_id, int last_message_id) {
    if (chat_id == NULL || last_message_id <= 0) {
        ESP_LOGW(TAG, "Неверный chat_id или last_message_id");
        return;
    }

    for (int id = last_message_id; id > 0; id--) {
        bool deleted = telegram_delete_message(chat_id, id);
        if (!deleted) {
            ESP_LOGW(TAG, "Не удалось удалить сообщение с id %d", id);
        }
    }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
void http_test_task(void *pvParameters) {

    init_telegram_bot();

    send_telegram_message(ROOT_CHAT_ID, "RESTART BOT");


    const char* buttons[] = {"ha", "che", "yola", "normal"};
    
    bool sent = send_telegram_message_with_buttons(ROOT_CHAT_ID, "barev ape jan\nlaves aper jan?\nlav eli aper jan✌️", buttons, 4, 0);

    const char* buttons2[] = {"butt1", "butt2", "butt3", "butt4", "butt5", "butt6"};
    sent = send_telegram_message_with_buttons(ROOT_CHAT_ID, "bareb barev karmir arev☀️\nkanach terev barev🎄\ninch ka exo jan?", buttons2, 6, 1); // 1 - вертикально
    
    if (sent) {
        ESP_LOGI(TAG, "Сообщение успешно отправлено!");
    } else {
        ESP_LOGW(TAG, "Ошибка отправки сообщения!");
    }

    //add commands
    set_command("start", "▶️ start bot");
    set_command("test", " ✅ bot testing");
    set_command("get", "  🔃 get info");
    telegram_upload_commands();

    while (1){
        telegram_get_last_message_text();
        telegram_check_callback();
        ESP_LOGI("current_chat_id", ">>>>: %s", bot_info.current_chat_id);
        ESP_LOGI("sender_username", ">>>>: %s", bot_info.sender_username);
        ESP_LOGI("last_msg", ">>>>: %s", bot_info.last_msg);
        ESP_LOGI("current_message", ">>>>: %s", bot_info.current_msg);
        ESP_LOGI("last_message_id", ">>>>: %d", bot_info.last_message_id);
        ESP_LOGI("","***********************************************************************");
        //telegram_delete_message(ROOT_CHAT_ID, --a);
        vTaskDelay(GET_NEW_MSG_TIME / portTICK_PERIOD_MS);
    }
    
    

    vTaskDelete(NULL);
}