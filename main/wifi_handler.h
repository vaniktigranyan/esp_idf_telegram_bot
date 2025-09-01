typedef struct {
    bool connected;
}device_status_t;

extern device_status_t device_status;

esp_err_t connect_to_wifi(char *ssid,char *password);
