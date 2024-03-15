#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ReactESP.h>
#include "bss_shared.h"

uint8_t broadcast_address[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info broadcast_peer;

// REPLACE WITH YOUR RECEIVER MAC Address
uint8_t addresses[][6] = {
    {0xA0, 0xB7, 0x65, 0x67, 0xCA, 0xD4},
    {0xA0, 0xB7, 0x65, 0x4C, 0xBF, 0x04},
    {0xA0, 0xB7, 0x65, 0x58, 0x69, 0x28},
};

typedef struct client_struct
{
    uint8_t id;
    uint8_t *mac;
    uint64_t timeout;
    esp_now_peer_info peer;
    client_struct *next;
} client_struct;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

// Structure example to receive data
// Must match the sender structure
typedef struct
{
    char type;
    uint8_t a;
    uint8_t b;
    uint8_t c;
} struct_message;

time_t send_time = 0;
bool test = false;
client_struct *new_client_g = NULL;

// Create a struct_message called myData
struct_message myData;
client_struct *clients = NULL;

uint8_t *msg_buf;
#define MSG_CLEAR_BUF memset(msg_buf, 0, 8 * sizeof(uint8_t))

// reactesp::ReactESP app;

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

void add_peer(esp_now_peer_info *peer_info, uint8_t *peer_mac)
{
    copy_mac(peer_info->peer_addr, peer_mac);
    peer_info->channel = ESP_NOW_BSS_CHANNEL;
    peer_info->encrypt = ESP_NOW_BSS_ENCRYPT;

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
    buf[2] = MSG_SET_NEOPIXEL_COLOR;
    buf[3] = rgb.r;
    buf[4] = rgb.g;
    buf[5] = rgb.b;
}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len)
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
    // Serial.println(millis());
    // Serial.print("Bytes received: ");
    // Serial.println(len);
    // Serial.println(id);
    // Serial.println("current client: ");
    // Serial.println(current_client == NULL);
    // if (current_client != NULL)
    // Serial.println(current_client->id);
    // Serial.println(clients == NULL);

    if (data[2] == MSG_BUZZER_PRESSED)
    {
        // Serial.println("Buzzer Pressed");

        if (current_client != NULL)
        {
            uint8_t i = 0;
            client_struct *tmp_client = clients;

            while (tmp_client != NULL)
            {
                // Serial.print("add msg with id: ");
                fill_msg_buf(&msg_buf[i], tmp_client->id, {1, 1, 1});
                i += 6;
                tmp_client = tmp_client->next;
            }

            send_msg(broadcast_address, msg_buf, i);

            /*app.onDelay(1000, []()
                        {
                            uint8_t i = 0;
                            client_struct *tmp_client = clients;

                            while (tmp_client != NULL)
                            {
                                //Serial.println("add msg");
                                fill_msg_buf(&msg_buf[i], tmp_client->id, {0, 0, 0});
                                i += 6;
                                tmp_client = tmp_client->next;
                            }

                            send_msg(broadcast_address, msg_buf, i); });*/
        }
    }
    else if (data[2] == MSG_PAIRING_REQUEST)
    {
        Serial.println("Pairing Request");

        if (test != true)
        {
            msg_buf[0] = id;
            msg_buf[1] = 1;
            msg_buf[2] = MSG_PAIRING_ACCEPTED;

            uint8_t *new_mac = (uint8_t *)malloc(6 * sizeof(uint8_t));
            copy_mac(new_mac, mac);

            client_struct *new_client = (client_struct *)malloc(sizeof(client_struct));
            new_client->id = id;
            new_client->mac = new_mac;
            new_client->timeout = millis();
            new_client->next = NULL;

            memset(&new_client->peer, 1, sizeof(esp_now_peer_info));

            PrintMac(mac);
            PrintMac(new_mac);

            // esp_now_peer_info *tmp_peer = (esp_now_peer_info *)malloc(sizeof(esp_now_peer_info));

            memcpy(new_client->peer.peer_addr, mac, 6);
            new_client->peer.channel = ESP_NOW_BSS_CHANNEL;
            new_client->peer.encrypt = false;

            if (current_client == NULL)
                add_client(new_client);

            // Serial.println("add client");
            new_client_g = new_client;
            test = true;
        }
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

    msg_buf = (uint8_t *)malloc(250 * sizeof(uint8_t));

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
    delay(100);

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
    if (test == true)
    {

        Serial.println("test");

        Serial.println("Add peer: ");
        int err = esp_now_add_peer(&new_client_g->peer);
        Serial.println("#################################################################");
        Serial.printf("%X", err);
        Serial.println();

        // Serial.println("Add peer: ");
        // err = esp_now_add_peer(tmp_peer);
        // Serial.println("#################################################################");
        // Serial.printf("%X", err);
        // Serial.println();

        // add_peer(tmp_peer, new_mac);

        // Serial.println("foo");

        send_msg(new_client_g->mac, msg_buf, 3);
        test = false;
    }
    // app.tick();
}
