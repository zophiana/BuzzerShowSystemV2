/*
  Copyright © 2024 Leonard Sebastian Schwennesen. All rights reserved.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ReactESP.h>
#include <FastLED.h>
#include "bss_shared.h"

uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t broadcast_peer;

#define LED_PIN 33
#define BUZZER_PIN 25

#define LED_NUM 12
#define BRIGHTNESS 20

CRGB leds[LED_NUM];

uint8_t msg_buf[250];
#define MSG_CLEAR_BUF memset(msg_buf, 0, 8 * sizeof(uint8_t))

#define sec *1000

uint8_t my_mac[6];
uint8_t my_id;

RTC_DATA_ATTR bool paired = false;
bool pairing_mode = false;
bool pairing_mode_enter;
RTC_DATA_ATTR uint8_t controller_mac[6];
esp_now_peer_info_t controller_peer;

ulong last_buzzer_pressed = 0;

reactesp::ReactESP app;
reactesp::RepeatReaction *pairing_loop;
reactesp::DelayReaction *pairing_disable_delay;
reactesp::RepeatReaction *ping_loop;

SemaphoreHandle_t xMutex = NULL;

inline void copy_mac(uint8_t *dest, const uint8_t *source)
{
    memcpy(dest, source, 6);
}

// callback when data is sent
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
    // Serial.print("\r\nLast Packet Send Status:\n");
    //  Serial.printf("time needed to send: %i\n", millis() - send_time);
    // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

    // Serial.printf("msg delivered: %i\n", millis() - send_time);
}

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

        for (uint8_t i = 0; i < len;)
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
            case BSS_MSG_SET_NEOPIXEL_COLOR:
                digitalWrite(LED_PIN, start_ptr[3]);
                digitalWrite(LED_PIN, start_ptr[4]);
                break;

            case BSS_MSG_PAIRING_ACCEPTED:
                Serial.println("Pairing Accepted");

                if (pairing_disable_delay != NULL)
                {
                    pairing_disable_delay->remove();
                    pairing_disable_delay = NULL;
                }

                if (pairing_loop != NULL)
                {
                    pairing_loop->remove();
                    pairing_loop = NULL;
                    pairing_mode = false;

                    digitalWrite(LED_PIN, false);
                }

                if (!paired)
                {
                    paired = true;

                    copy_mac(controller_mac, mac);

                    copy_mac(controller_peer.peer_addr, controller_mac);
                    esp_now_add_peer(&controller_peer);

                    // app.onDelay(1000, []()
                    //             {esp_sleep_enable_timer_wakeup(1000);   esp_deep_sleep_start(); });
                }
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

    WiFi.macAddress(my_mac);

    uint64_t id_buf = 0;

    for (uint8_t i = 0; i < 6; i++)
    {
        id_buf += my_mac[i];
    }

    my_id = ((uint8_t *)&id_buf)[0];

    xMutex = xSemaphoreCreateMutex();

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    Serial.println();

    // Once ESPNow is successfully Init, we will register for recv CB to
    // get recv packer info
    esp_now_register_recv_cb(OnDataRecv);

    // Once ESPNow is successfully Init, we will register for Send CB to
    // get the status of Trasnmitted packet
    esp_now_register_send_cb(OnDataSent);

    // Register broadcast peer
    copy_mac(broadcast_peer.peer_addr, broadcast_address);
    broadcast_peer.channel = BSS_ESP_NOW_CHANNEL;
    broadcast_peer.encrypt = BSS_ESP_NOW_ENCRYPT;
    esp_now_add_peer(&broadcast_peer);

    controller_peer.channel = BSS_ESP_NOW_CHANNEL;
    controller_peer.encrypt = BSS_ESP_NOW_ENCRYPT;

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_NUM).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);

    pinMode(BUZZER_PIN, INPUT_PULLUP);

    fill_solid(leds, LED_NUM, CRGB::Black);
    FastLED.show();

    if (paired)
    {
        Serial.println("already paired!");

        pairing_loop = NULL;

        digitalWrite(LED_PIN, false);

        copy_mac(controller_peer.peer_addr, controller_mac);
        esp_now_add_peer(&controller_peer);
    }

    Serial.println("Starting now...");
}

void loop()
{
    if (xSemaphoreTake(xMutex, 10))
    {
        app.tick();

        static bool buzzer_state = false;
        static bool buzzer_state_old = false;
        static bool buzzer_pressed = false;
        static bool buzzer_released = false;
        static ulong last_change = 0;

        buzzer_state_old = buzzer_state;
        buzzer_state = !digitalRead(BUZZER_PIN);

        if (buzzer_state && !buzzer_state_old)
        {
            if ((millis() - last_change) >= 10)
            {
                Serial.println("buzzer pressed");

                buzzer_pressed = true;
                buzzer_released = false;
            }

            if (paired)
            {
                msg_buf[0] = my_id;
                msg_buf[1] = 1;
                msg_buf[2] = BSS_MSG_BUZZER_PRESSED;
                send_msg(controller_mac, msg_buf, 3);
            }

            last_change = millis();
        }
        else if (!buzzer_state && buzzer_state_old)
        {
            ulong pressed_duration = millis() - last_change;

            if (pressed_duration >= 10)
            {
                Serial.println("buzzer released");

                buzzer_pressed = false;
                buzzer_released = true;
            }

            last_change = millis();

            if (pressed_duration >= 3 sec && buzzer_released && !pairing_mode)
            {
                paired = false;
                pairing_mode = true;
                pairing_mode_enter = millis();
                pairing_loop = NULL;

                // digitalWrite(LED_PIN, true);
                fill_solid(leds, LED_NUM, CRGB::White);
                FastLED.show();
            }
        }

        if (paired)
        {
            /*if (buzzer_state && !buzzer_state_old)
            {
                msg_buf[0] = my_id;
                msg_buf[1] = 1;
                msg_buf[2] = BSS_MSG_BUZZER_PRESSED;
                send_msg(controller_mac, msg_buf, 3);
            }*/

            if (ping_loop == NULL)
            {
                ping_loop = app.onRepeat(2 sec, []()
                                         {
                                            msg_buf[0] = my_id;
                                            msg_buf[1] = 1;
                                            msg_buf[2] = BSS_MSG_PING;

                                            send_msg(controller_mac, msg_buf, 3); });
            }
        }
        else if (!paired && pairing_loop == NULL && pairing_mode)
        {
            pairing_loop = app.onRepeat(1 sec, []()
                                        {
                                            static bool led_state = true;

                                            digitalWrite(LED_PIN, led_state = !led_state);

                                            msg_buf[0] = my_id;
                                            msg_buf[1] = 1;
                                            msg_buf[2] = BSS_MSG_PAIRING_REQUEST;

                                            send_msg(broadcast_address, msg_buf, 3); });

            pairing_disable_delay = app.onDelay(30 sec, []()
                                                {
                                                    if (!paired && pairing_loop != NULL && pairing_mode)
                                                    {
                                                        pairing_loop->remove();
                                                        pairing_loop = NULL;
                                                        pairing_mode = false;

                                                        // digitalWrite(LED_PIN, false);
                                                        fill_solid(leds, LED_NUM, CRGB::Black);
                                                        FastLED.show();
                                                    }
                                                    
                                                    pairing_disable_delay = NULL; });
        }

        buzzer_pressed = false;
        buzzer_released = false;

        xSemaphoreGive(xMutex);
    }
}
