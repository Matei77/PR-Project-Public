#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Configurations
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// -------------------------------------------------------------------------------------------------
// Wifi Configuration

#define CONNECTION_TIMEOUT 10

const char* ssid = "xxx";
const char* password = "xxx";

WiFiClientSecure secured_client;


// -------------------------------------------------------------------------------------------------
// Telegram Bot Configuration

String chat_id = "xxx";
String bot_token = "xxx";

UniversalTelegramBot bot(bot_token, secured_client);

const unsigned long BOT_MTBS = 1000; // mean time between scan messages
unsigned long bot_last_time;          // last time messages' scan has been done


// -------------------------------------------------------------------------------------------------
// Sensor and outputs pins

#define GAS_SENSOR_PIN 35
#define FLAME_SENSOR_PIN 18
#define RED_LED_PIN 33
#define BUZZER_PIN 32

// -------------------------------------------------------------------------------------------------
// Initial states
bool flame_detected = false;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Functions declarations
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void handleNewMessages(int new_messages_num);
void IRAM_ATTR detectsFlame();

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Setup
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


void setup() {
    Serial.begin(115200);
    
    // ---------------------------------------------------------------------------------------------
    // Initialize sensors and outputs
    
    // Initialize flame sensor
    pinMode(FLAME_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLAME_SENSOR_PIN), detectsFlame, FALLING);

    // Initialize led
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);

    // Initialize buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    tone(BUZZER_PIN, 800, 2000);

    // ---------------------------------------------------------------------------------------------
    // Connect to WiFi

    WiFi.mode(WIFI_STA);
    Serial.println();
    Serial.println("\nConnecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);

    int timeout_counter = 0;

    // restart ESP32 after 10 seconds if it can't connect to the WiFi
    while(WiFi.status() != WL_CONNECTED){
        Serial.print(".");
        delay(200);
        timeout_counter++;
        if(timeout_counter >= CONNECTION_TIMEOUT*5){
        ESP.restart();
        }
    }

    // Add root certificate for api.telegram.org
    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    Serial.println("\nConnected to the WiFi network");
    Serial.print("Local ESP32 IP: ");
    Serial.println(WiFi.localIP());
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Loop
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void loop() {
    if (flame_detected) {
        bot.sendMessage(chat_id, "Fire detected!", "");
        digitalWrite(RED_LED_PIN, HIGH);
        tone(BUZZER_PIN, 1000, 1000);
        delay(1000);
        digitalWrite(RED_LED_PIN, LOW);
        flame_detected = false;
    }

    if (millis() - bot_last_time > BOT_MTBS) {
        int new_messages_num = bot.getUpdates(bot.last_message_received + 1);

        while (new_messages_num) {
            Serial.println("got response");
            handleNewMessages(new_messages_num);
            new_messages_num = bot.getUpdates(bot.last_message_received + 1);
        }

        bot_last_time = millis();
    }
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Functions definitions
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void handleNewMessages(int new_messages_num){
    Serial.print("Handle New Messages: ");
    Serial.println(new_messages_num);

    for (int i = 0; i < new_messages_num; i++){
        // Chat id of the requester
        String requester_chat_id = String(bot.messages[i].chat_id);
        if (requester_chat_id != chat_id){
            bot.sendMessage(requester_chat_id, "Unauthorized user", "");
            continue;
        }
        
        // Print the received message
        String text = bot.messages[i].text;
        Serial.println(text);

        String fromName = bot.messages[i].from_name;

        if (text == "/gas") {
            int gas_reading = analogRead(GAS_SENSOR_PIN);
            String message = "Gas sensor value: " + String(gas_reading);
            bot.sendMessage(chat_id, message, "");
        }

        if (text == "/flame") {
            int flame_reading = digitalRead(FLAME_SENSOR_PIN);
            String status = "";
            if (flame_reading == HIGH) {
                status = "No flame in sight!";
            }
            if (flame_reading == LOW) {
                status = "Flame detected!";
            }
            String message = "Flame sensor: " + status;
            bot.sendMessage(chat_id, message, "");
        }

        if (text == "/start") {
            String welcome = "Welcome to the Home Security Telegram bot.\n";
            welcome += "/photo_cam1 : takes a new photo on Camera #1\n";
            welcome += "/photo_cam2 : takes a new photo on Camera #2\n";
            welcome += "/flash_cam1 : toggle flash LED on Camera #1\n";
            welcome += "/flash_cam2 : toggle flash LED on Camera #2\n";
            welcome += "/toggle_motion_detection_cam1 : toggle motion detection on Camera #1\n";
            welcome += "/toggle_motion_detection_cam2 : toggle motion detection on Camera #2\n";
            welcome += "/gas : request gas sensor reading\n";
            welcome += "/flame : request flame sensor reading\n\n";
            welcome += "An alert will be sent if a fire is detected.\n";
            welcome += "If motion detection is activated you will receive a photo whenever motion is detected.\n";
            bot.sendMessage(chat_id, welcome, "");
        }
    }
}

// -------------------------------------------------------------------------------------------------

void IRAM_ATTR detectsFlame() {
    flame_detected = true;
}