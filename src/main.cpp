#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

// ----------------------------------------------------------------------------
// Definition of macros
// ----------------------------------------------------------------------------

#define LED_PIN      15
#define PICKUP       2 
#define INDUCTIVE_IN  11
#define DEFAULT_STATIC_ADV   36
#define HTTP_PORT    80

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

// Button debouncing
const uint8_t DEBOUNCE_DELAY = 10; // in milliseconds

// WiFi credentials
const char *WIFI_SSID = "GiuseppeBagnile";
const char *WIFI_PASS = "MikroTik2020";

const char *AP_SSID = "rpmcounter";
const char *AP_PASS = "ciaociao";
IPAddress AP_IP (42, 42, 42, 42);

// rpm count
unsigned long RPM = 0;
unsigned long currentMicros = 0;
unsigned long lastMicros = 0;
unsigned long displayMillis = 0;
unsigned long revolutionTime = 0;
unsigned long advance = 0;
bool advanceAlreadyCalculated = true;

// LED flash timing
unsigned long ledFlashTime = 0;
const unsigned long LED_FLASH_DURATION = 50; // milliseconds

// Preferences
Preferences preferences;
unsigned long STATIC_ADV = DEFAULT_STATIC_ADV;

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct Led {
    // state variables
    uint8_t pin;
    bool    on;

    // methods
    void update() {
        digitalWrite(pin, on ? HIGH : LOW);
    }
};

// ----------------------------------------------------------------------------
// Definition of the Button component
// ----------------------------------------------------------------------------

struct Button {
    // state variables
    uint8_t  pin;
    bool     lastReading;
    uint32_t lastDebounceTime;
    uint16_t state;

    // methods determining the logical state of the button
    bool pressed()                { return state == 1; }
    bool released()               { return state == 0xffff; }
    bool held(uint16_t count = 0) { return state > 1 + count && state < 0xffff; }

    // method for reading the physical state of the button
    void read() {
        // reads the voltage on the pin connected to the button
        bool reading = digitalRead(pin);

        // if the logic level has changed since the last reading,
        // we reset the timer which counts down the necessary time
        // beyond which we can consider that the bouncing effect
        // has passed.
        if (reading != lastReading) {
            lastDebounceTime = millis();
        }

        // from the moment we're out of the bouncing phase
        // the actual status of the button can be determined
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
            // don't forget that the read pin is pulled-up
            bool pressed = reading == LOW;
            if (pressed) {
                if (state  < 0xfffe) state++;
                else if (state == 0xfffe) state = 2;
            } else if (state) {
                state = state == 0xffff ? 0 : 0xffff;
            }
        }

        // finally, each new reading is saved
        lastReading = reading;
    }
};

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

Led    onboard_led = { LED_BUILTIN, true };
Led    led         = { LED_PIN, false };

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");


// ----------------------------------------------------------------------------
// Interrupt for RPM measurement
// ----------------------------------------------------------------------------


IRAM_ATTR void pickupSignalDetected() {
    currentMicros = micros();

    if (lastMicros != currentMicros && (currentMicros - lastMicros) >= 500) {
        revolutionTime = currentMicros - lastMicros;
        RPM = 60000000 / revolutionTime;
        lastMicros = currentMicros;
        advanceAlreadyCalculated = false;
    }

}

IRAM_ATTR void sparkSignalDetected() {
    // lastMicros = currentMicros perchè questa funzione (in teoria) viene chiamata sempre dopo pickupSignalDetected
    // revolutionTime è il tempo che ci ha messo la moto a fare un giro l'ultima volta che è stato misurato
    // gradi aspettati : 360 = (millis() - lastMicros) : revolutionTime
    // 360 : revolutionTime = gradi aspettati : (millis() - lastMicros)
    // sottraggo a STATIC_ADV la variabile 'gradi aspettati' e ho quanto sono anticipato
    currentMicros = micros();
    if (advanceAlreadyCalculated == false && lastMicros != currentMicros && revolutionTime >= 500) {
        advance = STATIC_ADV - ((360 * (currentMicros - lastMicros)) / revolutionTime);
        advanceAlreadyCalculated = true;
        onboard_led.on = false;
        ledFlashTime = millis();
    }
    
}

// ----------------------------------------------------------------------------
// Connecting to the WiFi network
// ----------------------------------------------------------------------------

void initWiFi() {
    /*
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    
    Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }*/

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);


    Serial.printf(" %s\n", WiFi.softAPIP().toString().c_str());
}

// ----------------------------------------------------------------------------
// LittleFS initialization
// ----------------------------------------------------------------------------

void initLittleFS() {
    if (!LittleFS.begin()) {
        Serial.println("An error occurred while mounting LittleFS");
        return;
    }
    Serial.println("LittleFS mounted successfully");
}

// ----------------------------------------------------------------------------
// Preferences initialization
// ----------------------------------------------------------------------------

void initPreferences() {
    preferences.begin("advance-meter", false);
    STATIC_ADV = preferences.getULong("static_adv", DEFAULT_STATIC_ADV);
    Serial.printf("Loaded STATIC_ADV: %ld\n", STATIC_ADV);
}

void saveStaticAdv(unsigned long value) {
    STATIC_ADV = value;
    preferences.putULong("static_adv", STATIC_ADV);
    Serial.printf("Saved STATIC_ADV: %ld\n", STATIC_ADV);
}

// ----------------------------------------------------------------------------
// Web server initialization
// ----------------------------------------------------------------------------

void initWebServer() {
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

// ----------------------------------------------------------------------------
// WebSocket initialization
// ----------------------------------------------------------------------------

void notifyClients() {
    JsonDocument json;
    json["status"] = led.on ? "on" : "off";

    char data[17];
    size_t len = serializeJson(json, data);
    ws.textAll(data, len);
}

void updateRPM(unsigned long rpm = 0, unsigned long adv = 0) {
    JsonDocument json;
    json["rpm"] = rpm;
    json["adv"] = adv;

    char data[60];
    size_t len = serializeJson(json, data);
    ws.textAll(data, len);
    displayMillis = millis();
}

void sendStaticAdv() {
    JsonDocument json;
    json["static_adv"] = STATIC_ADV;

    char data[40];
    size_t len = serializeJson(json, data);
    ws.textAll(data, len);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

        JsonDocument  json;
        DeserializationError err = deserializeJson(json, data);
        if (err) {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(err.c_str());
            return;
        }

        const char *action = json["action"];
        if (strcmp(action, "toggle") == 0) {
            led.on = !led.on;
            notifyClients();
        }
        else if (strcmp(action, "set_static_adv") == 0) {
            unsigned long value = json["value"];
            if (value > 0 && value < 360) {
                saveStaticAdv(value);
                sendStaticAdv();
            }
        }
        else if (strcmp(action, "get_static_adv") == 0) {
            sendStaticAdv();
        }

    }
}

void onEvent(AsyncWebSocket       *server,
             AsyncWebSocketClient *client,
             AwsEventType          type,
             void                 *arg,
             uint8_t              *data,
             size_t                len) {

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            sendStaticAdv();
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

void setup() {
    pinMode(onboard_led.pin, OUTPUT);
    pinMode(led.pin,         OUTPUT);
    pinMode(PICKUP,          INPUT);
    pinMode(INDUCTIVE_IN,    INPUT);
    attachInterrupt(digitalPinToInterrupt(PICKUP), pickupSignalDetected, RISING);
    attachInterrupt(digitalPinToInterrupt(INDUCTIVE_IN), sparkSignalDetected, RISING);

    Serial.begin(115200); delay(500);

    initLittleFS();
    initPreferences();
    initWiFi();
    initWebSocket();
    initWebServer();
}

// ----------------------------------------------------------------------------
// Main control loop
// ----------------------------------------------------------------------------

void loop() {
    ws.cleanupClients();
    
    // LED flashes off on spark detection, then turns back on
    if (!onboard_led.on && millis() - ledFlashTime >= LED_FLASH_DURATION) {
        onboard_led.on = true;
    }
    
    if (millis() - displayMillis >= 20) {
        Serial.printf("RPM: %ld       ADV: %ld\n", RPM, advance);
        updateRPM(RPM, advance);
    }

    onboard_led.update();
}