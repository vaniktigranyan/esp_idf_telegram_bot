void http_test_task(void *pvParameters);
void check_bot_info(void);
void send_telegram_message(const char* chat_id, const char* message_text);
esp_err_t telegram_delete_webhook(const char *bot_token);
void set_command(const char *cmd, const char *desc);
void telegram_upload_commands(void);
void init_telegram_bot(void);