typedef struct {
    char bot_token[128];        // Telegram bot token
    char api_url[128];          // Base API URL (e.g., "https://api.telegram.org")
    char chat_id[32][15];       // chat ID to send messages
    char last_message[512];     // Last received message text
    bool connected;             // Flag: true if bot is connected to API
    uint32_t messages_sent;     // Counter of sent messages
    uint32_t messages_received; // Counter of received messages

} telegram_bot_info_t;



void http_test_task(void *pvParameters);
void check_bot_info(void);
void send_telegram_message(const char* chat_id, const char* message_text);
esp_err_t telegram_delete_webhook(const char *bot_token);
void set_command(const char *cmd, const char *desc);
void telegram_upload_commands(void);
void init_telegram_bot(void);