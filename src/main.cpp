#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ----------------------------------------------------------------------------
// Definition of macros
// ----------------------------------------------------------------------------

#define LED_PIN      15
#define PICKUP       13 
#define INDUCTIVE_IN  4     // pin sopra il 16 (compreso) non vanno bene per gli interrupt 
#define STATIC_ADV   36
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

const char index_html[] PROGMEM = R"(<!DOCTYPE html>
<html>

<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="description" content="ESP32 Remote Control with WebSocket">
  <title>ESP32 remote control</title>
</head>

<body>
    <div class="panel">
        <div class="rpm-container">
            <div id="status-led" class="led-red"></div>
            <h1 id="counter">RPM: 0</h1>
        </div>
        <h1 id="maxrpm">MAX RPM: 0</h1>
        <h1 id="advance">ADV: 0°</h1>
        <div id="led" class="%STATE%"></div>
        <button id="toggle">Toggle</button>
    </div>
</body>

<script>
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var maxRPM = 0;
var prevRPM = 0;
var csvfields = ""


// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButton();
}

// ----------------------------------------------------------------------------
// WebSocket handling
// ----------------------------------------------------------------------------

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    document.getElementById('status-led').className = 'led-green';
}

function onClose(event) {
    console.log('Connection closed');
    document.getElementById('status-led').className = 'led-red';
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    if (data.status)
        document.getElementById('led').className = data.status;
    if (data.rpm) {
        if (data.rpm > maxRPM) maxRPM = data.rpm
        document.getElementById('counter').textContent = "RPM: " + data.rpm;
        document.getElementById('maxrpm').textContent = "MAX: " + maxRPM;

        // logging
        if (prevRPM < data.rpm) {
            csvfields = csvfields.concat(data.rpm + ",")
            if (data.adv > 0 && data.adv < 50) {
                csvfields = csvfields.concat(data.adv + ",\n")
            } else {
                csvfields = csvfields.concat("-1,\n")
            }
            prevRPM = data.rpm
        }
    }
    if (data.adv) {
        document.getElementById('advance').textContent = "ADV: " + data.adv + "°";
    }
        
}

// ----------------------------------------------------------------------------
// Button handling
// ----------------------------------------------------------------------------

function initButton() {
    document.getElementById('toggle').addEventListener('click', onToggle);
}

function onToggle(event) {
    websocket.send(JSON.stringify({'action':'toggle'}));
    console.log(csvfields)
}
</script>

<style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    
    html, body {
      height: 100%;
      font-family: Roboto, sans-serif;
      font-size: 12pt;
      overflow: hidden;
    }
    
    body {
      display: grid;
      grid-template-rows: 1fr;
      align-items: center;
      justify-items: center;
    }
    
    .panel {
      display: grid;
      grid-gap: 3em;
      justify-items: center;
    }

    .rpm-container {
      display: flex;
      align-items: center;
    }

    h1 {
      font-size: 1.5rem;
      text-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
      margin-left: 10px;
    }
    
    #led, .led-red, .led-green {
      position: relative;
      width: 5em;
      height: 5em;
      border: 2px solid #000;
      border-radius: 2.5em;
      box-shadow: 0 0.5em 1em rgba(102, 0, 0, 0.3);
    }

    #led {
      background-image: radial-gradient(farthest-corner at 50% 20%, #b30000 0%, #330000 100%);
    }

    #led.on {
      background-image: radial-gradient(farthest-corner at 50% 75%, red 0%, #990000 100%);
      box-shadow: 0 1em 1.5em rgba(255, 0, 0, 0.5);
    }

    #led:after, .led-red:after, .led-green:after {
      content: '';
      position: absolute;
      top: .3em;
      left: 1em;
      width: 60%;
      height: 40%;
      border-radius: 60%;
      background-image: linear-gradient(rgba(255, 255, 255, 0.4), rgba(255, 255, 255, 0.1));
    }
    
    .led-red {
      width: 1em;
      height: 1em;
      background-color: red;
      border-radius: 50%;
      margin-right: 10px;
      box-shadow: 0 0.5em 1em rgba(102, 0, 0, 0.3);
    }
    
    .led-green {
      width: 1em;
      height: 1em;
      background-color: green;
      border-radius: 50%;
      margin-right: 10px;
      box-shadow: 0 0.5em 1em rgba(0, 102, 0, 0.3);
    }
    
    button {
      padding: .5em .75em;
      font-size: 1.2rem;
      color: #fff;
      text-shadow: 0 -1px 1px #000;
      border: 1px solid #000;
      border-radius: .5em;
      background-image: linear-gradient(#2e3538, #73848c);
      box-shadow: inset 0 2px 4px rgba(255, 255, 255, 0.5), 0 0.2em 0.4em rgba(0, 0, 0, 0.4);
      outline: none;
    }
    
    button:active {
      transform: translateY(2px);
    }
</style>

</html>

)";


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
// Web server initialization
// ----------------------------------------------------------------------------

String processor(const String &var) {
    return String(var == "STATE" && led.on ? "on" : "off");
}

void onRootRequest(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", index_html);
}

void initWebServer() {
    server.on("/", onRootRequest);
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
    attachInterrupt(digitalPinToInterrupt(PICKUP), pickupSignalDetected, FALLING);
    attachInterrupt(digitalPinToInterrupt(INDUCTIVE_IN), sparkSignalDetected, FALLING);

    Serial.begin(115200); delay(500);

    initWiFi();
    initWebSocket();
    initWebServer();
}

// ----------------------------------------------------------------------------
// Main control loop
// ----------------------------------------------------------------------------

void loop() {
    ws.cleanupClients();
    
    onboard_led.on = millis() % 1000 < 500;
    if (millis() - displayMillis >= 20) {
        Serial.printf("RPM: %ld       ADV: %ld\n", RPM, advance);
        updateRPM(RPM, advance);
    }

    led.update();
    onboard_led.update();
}