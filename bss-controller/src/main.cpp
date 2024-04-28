/*
  Copyright © 2024 Leonard Sebastian Schwennesen. All rights reserved.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ReactESP.h>
#include "bss_shared.h"

#define sec *1000
#define MAX_ALLOED_TIMEOUT 5 sec

uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t broadcast_peer;

typedef struct client_struct
{
    uint8_t id;
    uint8_t mac[6];
    ulong last_msg;
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

bool buzzer_pressed = false;

#define PAIRING_BUTTON D9
#define RESET_BUTTON D8
#define WRONG_BUTTON D7

bool pairing_mode = false;

uint8_t msg_buf[250];

reactesp::ReactESP app;

inline void blink(uint8_t pin, bool initial_state)
{
    static bool state = !initial_state;

    digitalWrite(pin, state = !state);
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

void print_mac(const uint8_t *mac)
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

void on_data_recv(const uint8_t *mac, const uint8_t *data, int len)
{
    if (xSemaphoreTake(xMutex, portMAX_DELAY))
    {
        uint8_t id = data[0];
        client_struct *current_client = clients;
        client_struct *last_client = NULL;

        while (current_client != NULL)
        {
            if (current_client->id == id && mac_equal(mac, current_client->mac))
            {
                current_client->last_msg = millis();
                break;
            }

            last_client = current_client;
            current_client = current_client->next;
        }

        print_mac(mac);
        Serial.println(id);

        if (current_client != NULL)
        {
            Serial.println(mac_equal(mac, current_client->mac));
            print_mac(current_client->mac);
            Serial.println(current_client->id);
        }

        if (data[2] == BSS_MSG_BUZZER_PRESSED)
        {
            Serial.println("Buzzer Pressed");

            if (current_client != NULL && !buzzer_pressed)
            {
                buzzer_pressed = true;

                uint8_t i = 0;
                client_struct *tmp_client = clients;

                for (; tmp_client != NULL; i += 6)
                {
                    // Serial.print("add msg with id: ");
                    fill_msg_buf(&msg_buf[i], tmp_client->id, {255, 255, 255});
                    tmp_client = tmp_client->next;
                }

                send_msg(broadcast_mac, msg_buf, i);
            }
        }
        else if (data[2] == BSS_MSG_PAIRING_REQUEST)
        {
            if (pairing_mode)
            {
                Serial.println("Pairing Request");

                msg_buf[0] = id;
                msg_buf[1] = 1;
                msg_buf[2] = BSS_MSG_PAIRING_ACCEPTED;

                if (current_client == NULL)
                {
                    client_struct *new_client = (client_struct *)malloc(sizeof(client_struct));
                    new_client->id = id;
                    mac_copy(new_client->mac, mac);
                    new_client->last_msg = millis();
                    new_client->next = NULL;

                    memset(&new_client->peer, 0, sizeof(esp_now_peer_info_t));

                    print_mac(new_client->mac);

                    mac_copy(new_client->peer.peer_addr, mac);
                    new_client->peer.channel = BSS_ESP_NOW_CHANNEL;
                    new_client->peer.encrypt = BSS_ESP_NOW_ENCRYPT;

                    if (clients == NULL)
                    {
                        clients = new_client;
                    }
                    else
                    {
                        client_struct *head = clients;

                        while (head->next != NULL)
                            head = head->next;

                        head->next = new_client;
                    }

                    current_client = new_client;
                }

                Serial.println("Add peer: ");
                if (!esp_now_is_peer_exist(mac))
                    esp_now_add_peer(&current_client->peer);

                Serial.println("add client");
                send_msg(current_client->mac, msg_buf, 3);
            }
        }
        else if (data[2] == BSS_MSG_WAKEUP_REQUEST)
        {
            Serial.println("wakeup request");

            if (pairing_mode && current_client != NULL)
            {
                Serial.println("accepted");
                msg_buf[0] = id;
                msg_buf[1] = 1;
                msg_buf[2] = BSS_MSG_WAKEUP_ACCEPTED;

                send_msg(current_client->mac, msg_buf, 3);
            }
            else if (current_client == NULL && !mac_equal(mac, broadcast_mac))
            {
                Serial.println("declined");
                print_mac(mac);

                msg_buf[0] = id;
                msg_buf[1] = 1;
                msg_buf[2] = BSS_MSG_PAIRING_REMOVE;

                send_msg(mac, msg_buf, 3);
            }
        }
        else if (data[2] == BSS_MSG_PAIRING_REMOVE)
        {
            if (current_client != NULL)
            {
                if (esp_now_is_peer_exist(mac))
                    esp_now_del_peer(mac);

                if (last_client != NULL)
                    last_client->next = current_client->next;
                else
                    clients = current_client->next;

                free(current_client);
            }
        }

        xSemaphoreGive(xMutex);
    }
}

void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    // Serial.print("\r\nLast Packet Send Status:\n");
    //  Serial.printf("time needed to send: %i\n", millis() - send_time);
    // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

    // Serial.printf("msg delivered: %i\n", millis() - send_time);
}

void setup()
{
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    Serial.println();

    pinMode(D10, OUTPUT);
    pinMode(PAIRING_BUTTON, INPUT_PULLUP);
    pinMode(RESET_BUTTON, INPUT_PULLUP);
    pinMode(WRONG_BUTTON, INPUT_PULLUP);

    app.onInterrupt(PAIRING_BUTTON, RISING, []()
                    { 
                        static ulong last_pressed = 0;

                        if ((millis() - last_pressed) >= 250) {
                            last_pressed = millis();

                            if (!buzzer_pressed) {
                                static reactesp::RepeatReaction *react_blink = NULL;
                                pairing_mode = !pairing_mode;

                                if (pairing_mode && react_blink == NULL) {
                                    digitalWrite(D10, true);
                                    react_blink = app.onRepeat(1000, [](){blink(D10, false); });
                                }
                                else if (!pairing_mode && react_blink != NULL) {
                                    digitalWrite(D10, false);
                                    
                                    react_blink->remove();
                                    react_blink = NULL;
                                }
                            } else {
                                uint8_t i = 0;
                                client_struct *client = clients;

                                for (; client != NULL; i += 6)
                                {
                                    fill_msg_buf(&msg_buf[i], client->id, {0, 255, 0});
                                    client = client->next;
                                }

                                send_msg(broadcast_mac, msg_buf, i);
                            }
                        } });

    app.onInterrupt(RESET_BUTTON, RISING, []()
                    {
                        static ulong last_pressed = 0;

                        if ((millis() - last_pressed) >= 250) {
                            last_pressed = millis();

                            if (buzzer_pressed)
                            {
                                buzzer_pressed = false;

                                uint8_t i = 0;
                                client_struct *client = clients;

                                for (; client != NULL; i += 6)
                                {
                                    fill_msg_buf(&msg_buf[i], client->id, {0, 0, 0});
                                    client = client->next;
                                }

                                send_msg(broadcast_mac, msg_buf, i);
                            }
                        } });

    app.onInterrupt(WRONG_BUTTON, RISING, []()
                    {
                        static ulong last_pressed = 0;

                        if ((millis() - last_pressed) >= 250) {
                            last_pressed = millis();

                            if (buzzer_pressed)
                            {
                                uint8_t i = 0;
                                client_struct *client = clients;

                                for (; client != NULL; i += 6)
                                {
                                    fill_msg_buf(&msg_buf[i], client->id, {255, 0, 0});
                                    client = client->next;
                                }

                                send_msg(broadcast_mac, msg_buf, i);
                            }
                        } });

    xMutex = xSemaphoreCreateMutex();

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    mac_copy(broadcast_peer.peer_addr, broadcast_mac);
    broadcast_peer.channel = BSS_ESP_NOW_CHANNEL;
    broadcast_peer.encrypt = BSS_ESP_NOW_ENCRYPT;
    esp_now_add_peer(&broadcast_peer);

    Serial.println("Starting now...");
}

void loop()
{
    if (xSemaphoreTake(xMutex, 10))
    {
        app.tick();

        client_struct *current_client = clients;

        while (current_client != NULL)
        {
            /*if ((millis() - current_client->last_msg) >= MAX_ALLOED_TIMEOUT)
            {
                Serial.println("lost the connection to:");
                print_mac(current_client->mac);
            }*/

            current_client = current_client->next;
        }

        xSemaphoreGive(xMutex);
    }
}
