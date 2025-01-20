#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <ArduinoJson.h>

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

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
// Camera Configuration (CAMERA_MODEL_AI_THINKER)

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN 4


// -------------------------------------------------------------------------------------------------
// Sensor and outputs pins

#define MOTION_SENSOR_PIN 2
#define RED_LED_PIN 13
#define BUZZER_PIN 15


// -------------------------------------------------------------------------------------------------
// Initial states

bool flash_state = LOW;
bool motion_detected = false;
bool motion_detection_toggle = false;
bool send_photo = false;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Functions declarations
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

String sendPhotoToTelegram();
void handleNewMessages(int new_messages_num);
void IRAM_ATTR detectsMovement();

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Setup
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
    Serial.begin(115200);
    
    // ---------------------------------------------------------------------------------------------
    // Initialize sensors and outputs
    
    // Initialize flash led
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, flash_state);

    // Initialize motion sensor
    pinMode(MOTION_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MOTION_SENSOR_PIN), detectsMovement, RISING);

    // Initialize led
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);

    // Initialize buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    tone(BUZZER_PIN, 800, 2000);
    
    //----------------------------------------------------------------------------------------------
    // Initilize camera

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;

    if(psramFound()){
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    
    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);        // flip it back
        s->set_brightness(s, 1);   // up the brightness just a bit
        s->set_saturation(s, -2);  // lower the saturation
    }
    // drop down frame size for higher initial frame rate
    if (config.pixel_format == PIXFORMAT_JPEG) {
        s->set_framesize(s, FRAMESIZE_QVGA);
    }

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
    if (send_photo) {
        Serial.println("Sending photo to Telegram...");
        send_photo = false;
        sendPhotoToTelegram();
    }

    if (motion_detected && motion_detection_toggle) {
        bot.sendMessage(chat_id, "Motion detected on Camera #2!", "");
        Serial.println("Motion detected!");
        digitalWrite(RED_LED_PIN, HIGH);
        sendPhotoToTelegram();
        digitalWrite(RED_LED_PIN, LOW);
        tone(BUZZER_PIN, 1000, 1000);
        motion_detected = false;
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

String sendPhotoToTelegram(){
    const char* telegram_domain = "api.telegram.org";
    String get_all = "";
    String get_body = "";

    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();  
    if(!fb) {
        Serial.println("Camera capture failed");
        delay(1000);
        ESP.restart();
        return "Camera capture failed";
    }  
    
    Serial.println("Connect to " + String(telegram_domain));

    if (secured_client.connect(telegram_domain, 443)) {
        Serial.println("Connection successful");
        
        String head = "--PR_IoT\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chat_id + "\r\n--PR_IoT\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
        String tail = "\r\n--PR_IoT--\r\n";

        uint16_t image_len = fb->len;
        uint16_t extra_len = head.length() + tail.length();
        uint16_t total_len = image_len + extra_len;
    
        secured_client.println("POST /bot"+ bot_token +"/sendPhoto HTTP/1.1");
        secured_client.println("Host: " + String(telegram_domain));
        secured_client.println("Content-Length: " + String(total_len));
        secured_client.println("Content-Type: multipart/form-data; boundary=PR_IoT");
        secured_client.println();
        secured_client.print(head);
    
        uint8_t *fb_buf = fb->buf;
        size_t fb_len = fb->len;
        for (size_t n=0;n<fb_len;n=n+1024) {
            if (n+1024<fb_len) {
                secured_client.write(fb_buf, 1024);
                fb_buf += 1024;
            }
            else if (fb_len%1024>0) {
                size_t remainder = fb_len%1024;
                secured_client.write(fb_buf, remainder);
            }
        }  
        
        secured_client.print(tail);
        
        esp_camera_fb_return(fb);
        
        int timeout = 10000;   // timeout 10 seconds
        long start_timer = millis();
        boolean state = false;
        
        while ((start_timer + timeout) > millis()) {
            Serial.print(".");
            delay(100);      
            
            while (secured_client.available()) {
                char c = secured_client.read();

                if (state==true) get_body += String(c);  

                if (c == '\n') {
                    if (get_all.length()==0) state=true; 
                    get_all = "";
                } 
                else if (c != '\r')
                    get_all += String(c);
                    start_timer = millis();
            }

            if (get_body.length()>0) break;
        }
        secured_client.stop();
        Serial.println(get_body);
    }
    else {
        get_body="Connected to api.telegram.org failed.";
        Serial.println("Connected to api.telegram.org failed.");
    }
    return get_body;
}

// -------------------------------------------------------------------------------------------------

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

    if (text == "/flash_cam2") {
        flash_state = !flash_state;
        digitalWrite(FLASH_LED_PIN, flash_state);
    }
    
    if (text == "/photo_cam2") {
        send_photo = true;
        Serial.println("New photo request");
        bot.sendMessage(chat_id, "Photo taken on Camera #2", "");
    }

    if (text == "/toggle_motion_detection_cam2") {
        motion_detection_toggle = !motion_detection_toggle;
        if (motion_detection_toggle) {
            bot.sendMessage(chat_id, "Motion detection enabled on Camera #2", "");
        } else {
            bot.sendMessage(chat_id, "Motion detection disabled on Camera #2", "");
        }
    }
  }
}

// -------------------------------------------------------------------------------------------------

void IRAM_ATTR detectsMovement() {
    motion_detected = true;
}