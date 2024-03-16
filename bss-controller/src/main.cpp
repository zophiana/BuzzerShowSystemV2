/*
  Copyright © 2024 Leonard Sebastian Schwennesen. All rights reserved.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ReactESP.h>
#include "bss_shared.h"

uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t broadcast_peer;

typedef struct client_struct
{
    uint8_t id;
    uint8_t mac[6];
    uint64_t timeout;
    esp_now_peer_info_t peer;
    client_struct *next;
} client_struct;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

client_struct *clients = NULL;

SemaphoreHandle_t xMutex = NULL;

uint8_t msg_buf[250];
#define MSG_CLEAR_BUF memset(msg_buf, 0, 8 * sizeof(uint8_t))

reactesp::ReactESP app;

void send_msg(const uint8_t *mac_addr, const uint8_t *data, const uint8_t size);

void add_client(client_struct *client)
{
    if (clients == NULL)
    {
        clients = client;
        return;
    }

    client_struct *head = clients;

    while (head->next != NULL)
        head = head->next;

    head->next = client;
}

inline void copy_mac(uint8_t *dest, const uint8_t *source)
{
    memcpy(dest, source, 6);
}

void add_peer(esp_now_peer_info_t *peer_info, uint8_t *peer_mac)
{
    copy_mac(peer_info->peer_addr, peer_mac);
    peer_info->channel = BSS_ESP_NOW_CHANNEL;
    peer_info->encrypt = BSS_ESP_NOW_ENCRYPT;

    esp_now_add_peer(peer_info);
}

void PrintMac(const uint8_t *mac)
{
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6]);
}

void fill_msg_buf(uint8_t *buf, uint8_t id, rgb_t rgb)
{
    buf[0] = id;
    buf[1] = 4;
    buf[2] = BSS_MSG_SET_NEOPIXEL_COLOR;
    buf[3] = rgb.r;
    buf[4] = rgb.g;
    buf[5] = rgb.b;
}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
    if (xSemaphoreTake(xMutex, portMAX_DELAY))
    {
        uint8_t id = data[0];
        client_struct *current_client = clients;

        while (current_client != NULL)
        {
            if (current_client->id == id)
            {
                current_client->timeout = millis();
                break;
            }

            current_client = current_client->next;
        }

        PrintMac(mac);

        if (data[2] == BSS_MSG_BUZZER_PRESSED)
        {
            // Serial.println("Buzzer Pressed");

            if (current_client != NULL)
            {
                uint8_t i = 0;
                client_struct *tmp_client = clients;

                for (; tmp_client != NULL; i += 6)
                {
                    // Serial.print("add msg with id: ");
                    fill_msg_buf(&msg_buf[i], tmp_client->id, {1, 1, 1});
                    tmp_client = tmp_client->next;
                }

                send_msg(broadcast_address, msg_buf, i);

                app.onDelay(1000, []()
                            {
                            uint8_t i = 0;
                            client_struct *tmp_client = clients;

                            for (; tmp_client != NULL; i += 6)
                            {
                                // Serial.print("add msg with id: ");
                                fill_msg_buf(&msg_buf[i], tmp_client->id, {0, 0, 0});
                                tmp_client = tmp_client->next;
                            }

                            send_msg(broadcast_address, msg_buf, i); });
            }
        }
        else if (data[2] == BSS_MSG_PAIRING_REQUEST)
        {
            Serial.println("Pairing Request");

            msg_buf[0] = id;
            msg_buf[1] = 1;
            msg_buf[2] = BSS_MSG_PAIRING_ACCEPTED;

            if (current_client == NULL)
            {
                client_struct *new_client = (client_struct *)malloc(sizeof(client_struct));
                new_client->id = id;
                copy_mac(new_client->mac, mac);
                new_client->timeout = millis();
                new_client->next = NULL;

                memset(&new_client->peer, 0, sizeof(esp_now_peer_info_t));

                PrintMac(new_client->mac);

                copy_mac(new_client->peer.peer_addr, mac);
                new_client->peer.channel = BSS_ESP_NOW_CHANNEL;
                new_client->peer.encrypt = BSS_ESP_NOW_ENCRYPT;

                add_client(new_client);

                current_client = new_client;
            }

            Serial.println("Add peer: ");
            if (!esp_now_is_peer_exist(mac))
                esp_now_add_peer(&current_client->peer);

            Serial.println("add client");
            send_msg(current_client->mac, msg_buf, 3);
        }

        xSemaphoreGive(xMutex);
    }
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    // Serial.print("\r\nLast Packet Send Status:\n");
    //  Serial.printf("time needed to send: %i\n", millis() - send_time);
    // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

    // Serial.printf("msg delivered: %i\n", millis() - send_time);
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

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    Serial.println();

    xMutex = xSemaphoreCreateMutex();

    // Once ESPNow is successfully Init, we will register for recv CB to
    // get recv packer info
    esp_now_register_recv_cb(OnDataRecv);

    // Once ESPNow is successfully Init, we will register for Send CB to
    // get the status of Trasnmitted packet
    esp_now_register_send_cb(OnDataSent);

    // Register peer
    memcpy(broadcast_peer.peer_addr, broadcast_address, 6);
    broadcast_peer.channel = 0;
    broadcast_peer.encrypt = false;

    esp_now_add_peer(&broadcast_peer);

    Serial.println("Starting now...");
}

void loop()
{
    if (xSemaphoreTake(xMutex, 10))
    {
        app.tick();
        xSemaphoreGive(xMutex);
    }
}
