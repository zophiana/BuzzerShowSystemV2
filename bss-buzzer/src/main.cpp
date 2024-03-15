#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ReactESP.h>
#include "bss_shared.h"

uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info broadcast_peer;

#define LED_PIN 26
#define BUZZER_PIN 25

// Structure example to receive data
// Must match the sender structure
// typedef struct
//{
//    uint8_t id;
//    uint8_t len;
//} struct_message;

// Create a struct_message called myData
// struct_message myData;
uint8_t *msg_buf;
#define MSG_CLEAR_BUF memset(msg_buf, 0, 8 * sizeof(uint8_t))

uint8_t *my_mac;
uint8_t my_id;

uint8_t controller_mac[6];
esp_now_peer_info_t controller_peer;

bool paired = false;

reactesp::ReactESP app;
reactesp::RepeatReaction *pairing_loop;

SemaphoreHandle_t xMutex = NULL;

inline void copy_mac(uint8_t *dest, const uint8_t *source)
{
    memcpy(dest, source, 6);
}

void add_peer(esp_now_peer_info *peer_info, uint8_t *peer_mac)
{
    copy_mac(peer_info->peer_addr, peer_mac);
    peer_info->channel = ESP_NOW_BSS_CHANNEL;
    peer_info->encrypt = ESP_NOW_BSS_ENCRYPT;

    esp_now_add_peer(peer_info);
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    // Serial.print("\r\nLast Packet Send Status:\n");
    //  Serial.printf("time needed to send: %i\n", millis() - send_time);
    // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

    // Serial.printf("msg delivered: %i\n", millis() - send_time);
}

// memset(controller_mac, 0, 6 * sizeof(uint8_t));

bool controller_mac_is_empty()
{
    for (size_t i = 0; i < 6; i++)
    {
        if (controller_mac[i] != 0)
            return false;
    }

    return true;
}

void PrintMac(const uint8_t *mac)
{
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6]);
}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
    Serial.println("Recived some things...");
    if (xSemaphoreTake(xMutex, portMAX_DELAY))
    {
        Serial.println("Got mutex!");
        uint8_t msg_type;
        const uint8_t *start_ptr;
        bool for_me = false;

        Serial.println("my id: ");
        Serial.println(my_id);

        for (uint8_t i = 0; i < len; i++)
        {
            if (data[i] == my_id)
            {
                start_ptr = &data[i];
                for_me = true;
                break;
            }
            Serial.println(data[i]);
            i += data[i + 1] + 2;
        }

        PrintMac(mac);
        Serial.print("Bytes received: ");
        Serial.println(len);
        // Serial.println(start);

        if (for_me)
        {
            switch (start_ptr[2])
            {
            case MSG_SET_NEOPIXEL_COLOR:
                digitalWrite(LED_PIN, start_ptr[2]);
                // digitalWrite(LED_PIN, data[start + 4]);
                break;

            case MSG_PAIRING_ACCEPTED:
                Serial.println("Pairing Accepted");

                if (pairing_loop != NULL)
                {
                    pairing_loop->remove();
                    pairing_loop = NULL;

                    paired = true;

                    digitalWrite(LED_PIN, false);

                    copy_mac(controller_mac, mac);

                    copy_mac(controller_peer.peer_addr, controller_mac);
                    Serial.println("some 4");
                    esp_now_add_peer(&controller_peer);
                    Serial.println("some 5");
                }

                Serial.println("some 6");

                break;

            default:
                break;
            }
        }

        xSemaphoreGive(xMutex);
    }
}

void send_msg(const uint8_t *mac_addr, const uint8_t *data, const uint8_t size)
{
    esp_err_t result = esp_now_send(mac_addr, data, size);

    Serial.println(millis());

    if (result == ESP_OK)
    {
        Serial.println("Sent with success\n");
    }
    else
    {
        Serial.println("Error sending the data\n");
    }
}

void setup()
{
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Set device as a Wi-Fi Station
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    my_mac = (uint8_t *)malloc(6 * sizeof(uint8_t));
    WiFi.macAddress(my_mac);

    for (uint8_t i = 0; i < 6; i++)
    {
        my_id += my_mac[i];
    }

    msg_buf = (uint8_t *)malloc(8 * sizeof(uint8_t));

    xMutex = xSemaphoreCreateMutex();

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    Serial.println();
    delay(100);

    // Once ESPNow is successfully Init, we will register for recv CB to
    // get recv packer info
    esp_now_register_recv_cb(OnDataRecv);

    // Once ESPNow is successfully Init, we will register for Send CB to
    // get the status of Trasnmitted packet
    esp_now_register_send_cb(OnDataSent);

    // Register peer
    add_peer(&broadcast_peer, broadcast_address);

    controller_peer.channel = 0;
    controller_peer.encrypt = false;

    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, INPUT_PULLUP);

    digitalWrite(LED_PIN, true);

    Serial.println("Starting now...");
}

void loop()
{
    if (xSemaphoreTake(xMutex, 10))
    {
        app.tick();

        if (paired)
        {
            static bool buzzer_state = false;
            static bool buzzer_state_old = buzzer_state;

            buzzer_state_old = buzzer_state;
            buzzer_state = !digitalRead(BUZZER_PIN);

            if (buzzer_state && !buzzer_state_old)
            {
                msg_buf[0] = my_id;
                msg_buf[1] = 1;
                msg_buf[2] = MSG_BUZZER_PRESSED;
                send_msg(controller_mac, msg_buf, 3);
            }
        }
        else if ((!paired) && (pairing_loop == NULL))
        {
            Serial.println("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
            Serial.printf("Pairing Loop: %d\nPaired: %d\nif-Statement: %d\n", pairing_loop == NULL, !paired, (!paired) && (pairing_loop == NULL));
            pairing_loop = app.onRepeat(1000, []()
                                        {
                                        Serial.println("###############################################");
                                        Serial.println("pairing request");
                                        Serial.printf("Paired: %d\n", paired);
                                        if (!paired) {
                                            Serial.println("###############################################");
                                            Serial.println("not paired");
                                            static bool led_state = true;
                                            digitalWrite(LED_PIN, led_state = !led_state);
                                            msg_buf[0] = my_id;
                                            msg_buf[1] = 1;
                                            msg_buf[2] = MSG_PAIRING_REQUEST;
                                            send_msg(broadcast_address, msg_buf, 3);
                                        } });
        }

        xSemaphoreGive(xMutex);
    }
}
