#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include "my_configs.h"

typedef struct {
    char bot_token[128];        // Telegram bot token
    char api_url[128];          // Base API URL (e.g., "https://api.telegram.org")
} telegram_bot_info_t;

typedef struct {
    char allowed_chat_id[ALLOWED_MAX_CHAT_IDS][10]; // Array to store allowed chat IDs 
    char api_url[128];          
} nvs_telegram_data_t;

void http_test_task(void *pvParameters);
void check_bot_info(void);
void send_telegram_message(const char* chat_id, const char* message_text);
esp_err_t telegram_delete_webhook(const char *bot_token);
void set_command(const char *cmd, const char *desc);
void telegram_upload_commands(void);
void init_telegram_bot(void);

#endif // TELEGRAM_BOT_H