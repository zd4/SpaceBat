#include "Wire.h"
#include <MPU6050_light.h>

#include <FastLED.h>

#include "credentials.h"
#include <ESPmDNS.h>
#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include "AsyncUDP.h"
#include <SPIFFS.h>
#else
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#endif
#include <ESPAsyncWebServer.h>
#define PIN_BUTTON 0
#define PIN_LED 21
#define NUM_LEDS 1

CRGB leds[NUM_LEDS];

MPU6050 mpu(Wire);
unsigned long timer = 0;

AsyncWebServer server(80);
AsyncUDP udp;
IPAddress cameraAddress;

const char *_angleOffset = "angleOffset";
const char *_deadzonePT = "deadzonePanTilt";
const char *_maxPanTilt = "maxPanTilt";
const char *_deadzoneZoom = "deadzoneZoom";
const char *_maxZoom = "maxZoom";
const char *_cameraIP = "cameraIP";
const char *_sendPort = "sendPort";
const char *_receivePort = "receivePort";
const char *_pan = "pan";
const char *_tilt = "tilt";
const char *_zoom = "zoom";
const char *_invertPan = "invertPan";
const char *_invertTilt = "invertTilt";
const char *_invertZoom = "invertZoom";
const char *_swap = "swap";

float angleOffset;       // MPU vs base (-19)
float angleOffsetExtra;  // rotation over time, zero on start
float deadzonePT;        // ~1
float maxPanTilt;        // 3
float deadzoneZoom;      // .5
float maxZoom;           // 1
String cameraIP;         //"192.168.178.88"
int sendPort = 1259;     // 1259
int receivePort = 11000; // 11000
bool pan;
bool invertPan;
bool tilt;
bool invertTilt;
bool zoom;
bool invertZoom;
bool swap;
bool suspendPT;
bool suspendZoom;

enum SocketState
{
  ready,
  unknown,
  busy,
};
SocketState socket1 = unknown;
SocketState socket2 = unknown;
long timer1;
long timer2;
int timeoutAck = 300;
int timeoutWork = 2000;

bool waiting = false;

int interval = 30; // ms

const char index_html[] PROGMEM = R"rawliteral(
<html>

<head>
    <title>PTZ-Joystick config page</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        html {
            font-family: Arial;
            display: inline-block;
            text-align: left;
        }

        h2 {
            font-size: 1.5rem;
            color: #339955;
            font-weight: bolder;
        }

        h3 {
            font-size: 1.2rem;
            color: #339955;
            text-decoration-line: underline;
        }

        h4 {
            font-size: 0.85rem;
            color: grey;
            font-style: italic;
        }

        form {
            display: table;
        }

        p {
            display: table-row;
        }

        label {
            display: table-cell;
            text-align: right;
        }

        input {
            display: table-cell;
        }

        text {
            display: table-cell;
        }

        hr {
            width: 150;
            margin: 35 auto;
        }

    </style>
    <script>
        function message_popup() {
            alert("Saved value to ESP SPIFFS");
            setTimeout(function () { document.location.reload(false); }, 500);
        }
        function reloadPage() { location.reload(); }
        function calibrateAngle() {
            if (confirm('keep joystick tilted straight forward and press ok')) {
                fetch('/calibrateAngle')
                    .then(response => response.text())
                    .then(result => {
                        document.getElementById('result').innerHTML = result;
                    });
                setTimeout(function () { location.reload(); }, 800);
            }

        }
        function reboot() {
            fetch('/reboot')
                .then(response => response.text())
                .then(result => {
                    document.getElementById('result').innerHTML = result;
                });
        }
        function reCenter() {
            if (confirm('keep joystick centered and press ok')) {
                fetch('/reCenter')
                    .then(response => response.text())
                    .then(result => {
                        document.getElementById('result').innerHTML = result;
                    });
                setTimeout(function () {
                    document.getElementById('result').innerHTML = "";
                    location.reload();
                }
                    , 800);
            }
        }
    </script>

</head>

<body>
    <h2>PTZ-Joystick config page</h2>
    <form action="/get" target="hidden-form">
        <h3>Compensate Misalignment</h3>
        <p>
            <!-- <label>Angle Offset (deg)</label> -->
            <label>forward direction MPU vs base (deg)</label>
            <input type="number" style="margin: 0px 25px;" step="0.1" name="angleOffset" value=%angleOffset%>
            <input type="button" value="calibrate" onclick="calibrateAngle()">
        </p>
        <hr>
        <h3>Pan/Tilt Control</h3>
        <p>
            <label for="pan"> enable Pan</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()" id="pan" name="pan" %pan%>
        </p>
        <p>
            <label for="invertPan"> invert Pan</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()"
                id="invertPan" name="invertPan" %invertPan%>
        </p>
        <p>
            <label for="tilt"> enable Tilt</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()" id="tilt" name="tilt" %tilt%>
        </p>
        <p>
            <label for="invertTilt"> invert Tilt</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()"
                id="invertTilt" name="invertTilt" %invertTilt%>
        </p>
        <p>
            <label for="swap"> swap Pan/Tilt</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()" id="swap" name="swap" %swap%>
        </p>
        <p>
            <label>deadzone Pan/Tilt</label>
            <input type="number" step="0.1" style="margin: 0px 25px;" name="deadzonePanTilt" value=%deadzonePanTilt%>
            <input type="button" value="recenter" onclick="reCenter()">
        </p>
        <p>
            <label>max Pan/Tilt</label>
            <input type="number" step="0.1" style="margin: 0px 25px;" name="maxPanTilt" value=%maxPanTilt%>
        </p>
        <hr>
        <h3>Zoom Control</h3>
        <p>
            <label for="zoom"> enable Zoom</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()" id="zoom" name="zoom" %zoom%>
        </p>
        <p>
            <label for="invertZoom"> invert Zoom</label>
            <input type="checkbox" style="margin: 0px 25px;" value="active" onchange="this.form.submit()"
                id="invertZoom" name="invertZoom" %invertZoom%>
        </p>
        <p>
            <label>deadzone Zoom</label>
            <input type="number" step="0.1" name="deadzoneZoom" style="margin: 0px 25px;" value=%deadzoneZoom%>
        </p>
        <p>
            <label>max Zoom</label>
            <input type="number" step="0.1" style="margin: 0px 25px;" name="maxZoom" value=%maxZoom%>
        </p>
        <hr>
        <h3>Connection</h3>
        <p>
            <label>camera IP-Address</label>
            <input style="margin: 0px 25px;" name="cameraIP" value=%cameraIP%>
        </p>
        <p>
            <label>udp-port for camera control</label>
            <input style="margin: 0px 25px;" name="sendPort" value=%sendPort%>
        </p>
        <p>
            <label>udp-port for camera feedback</label>
            <input style="margin: 0px 25px;" name="receivePort" value=%receivePort%>
        </p>
        <hr>
        <p>
            <input type="submit" value="save">
            <input type="button" value="load" onclick="reloadPage()">
            <input type="button" value="reboot" onclick="reboot()">
        </p>
    </form>
    <h4>SpaceBat V1.0.6</h4>
    <div id="result" style=" color: grey;
  font-size: 70%;"></div>

    <iframe style="display:none" name="hidden-form"></iframe>
</body>

</html>
)rawliteral";

void calibrateAngle(AsyncWebServerRequest *request)
{
  Serial.println("calibrateAngle");
  angleOffset = atan2(mpu.getAngleX(), mpu.getAccAngleY()) * RAD_2_DEG;
  write_file(SPIFFS, "/angleOffset.txt", String(angleOffset).c_str());
  request->send(200, "text/plain", "calibrated offset: " + String(angleOffset));
}
void reCenter(AsyncWebServerRequest *request)
{
  mpu.calcOffsets();
  Serial.println("reCenter");
  angleOffsetExtra = -mpu.getAngleZ();
  request->send(200, "text/plain", "reCentered");
}
void reBoot(AsyncWebServerRequest *request)
{
  Serial.println("reBooting");
  request->send(200, "text/plain", "reBooting");
  ESP.restart();
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}
String read_file(fs::FS &fs, const char *path)
{
  File file = fs.open(path, "r");
  if (!file || file.isDirectory())
  {
    Serial.println("Empty file/Failed to open file");
    return String();
  }
  String fileContent;
  while (file.available())
  {
    fileContent += String((char)file.read());
  }
  file.close();
  Serial.printf("- %s: %s\r\n", path, fileContent);
  return fileContent;
}

void write_file(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    //  Serial.println("SUCCESS in writing file");
  }
  else
  {
    Serial.println("FAILED to write file");
  }
  file.close();
}

String processor(const String &var)
{
  if (var == "pan")
  {
    if (read_file(SPIFFS, "/pan.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "tilt")
  {
    if (read_file(SPIFFS, "/tilt.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "zoom")
  {
    if (read_file(SPIFFS, "/zoom.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "invertPan")
  {
    if (read_file(SPIFFS, "/invertPan.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "invertTilt")
  {
    if (read_file(SPIFFS, "/invertTilt.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "invertZoom")
  {
    if (read_file(SPIFFS, "/invertZoom.txt") == "active")
      return "checked";
    else
      return "";
  }
  if (var == "angleOffset")
  {
    return read_file(SPIFFS, "/angleOffset.txt");
  }
  else if (var == "deadzonePanTilt")
  {
    return read_file(SPIFFS, "/deadzonePanTilt.txt");
  }
  else if (var == "maxPanTilt")
  {
    return read_file(SPIFFS, "/maxPanTilt.txt");
  }
  else if (var == "deadzoneZoom")
  {
    return read_file(SPIFFS, "/deadzoneZoom.txt");
  }
  else if (var == "maxZoom")
  {
    return read_file(SPIFFS, "/maxZoom.txt");
  }
  else if (var == "cameraIP")
  {
    return read_file(SPIFFS, "/cameraIP.txt");
  }
  else if (var == "sendPort")
  {
    return read_file(SPIFFS, "/sendPort.txt");
  }
  else if (var == "receivePort")
  {
    return read_file(SPIFFS, "/receivePort.txt");
  }
  return String();
}
void setup()
{
  SetupBasics();
  SetupMpu();
  SetupSpiffsAndParameters();
  SetupWifi();
  SetupServer();
  SetupUdp();
}
void SetupBasics()
{
  Serial.begin(115200);
  Wire.setPins(13, 15); // M5stamp default pins sda,scĺ
  Wire.begin();
  pinMode(PIN_BUTTON, INPUT);

  FastLED.addLeds<WS2812, PIN_LED, GRB>(leds, NUM_LEDS);
  // leds[0] = CHSV(led_ih, 255, 255);
  leds[0] = CRGB::Red;
  FastLED.show();
  delay(1000); // M5Stamp has no reset-button. Give time to init
}
void SetupSpiffsAndParameters()
{
#ifdef ESP32
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#else
  if (!SPIFFS.begin())
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#endif
  ReadParameters();
}
void SetupWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  leds[0] = CRGB::Magenta;
  FastLED.show();
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
void SetupServer()
{
  if (MDNS.begin("PTZ_joystick"))
  {
    Serial.println("MDNS responder started (PTZ_joystick)");
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html, processor); });
  server.on("/calibrateAngle", calibrateAngle);
  server.on("/reboot", reBoot);
  server.on("/reCenter", reCenter);

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String inputMessage;
      
    if (request->hasParam(_angleOffset)) {
      inputMessage = request->getParam(_angleOffset)->value();
      write_file(SPIFFS, "/angleOffset.txt", inputMessage.c_str());
    }
    if (request->hasParam(_pan)) {
      inputMessage = request->getParam(_pan)->value();
      write_file(SPIFFS, "/pan.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/pan.txt", "disabled");
    if (request->hasParam(_tilt)) {
      inputMessage = request->getParam(_tilt)->value();
      write_file(SPIFFS, "/tilt.txt", inputMessage.c_str());
    }
    if (request->hasParam(_zoom)) {
      inputMessage = request->getParam(_zoom)->value();
      write_file(SPIFFS, "/zoom.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/zoom.txt", "disabled");
    if (request->hasParam(_invertPan)) {
      inputMessage = request->getParam(_invertPan)->value();
      write_file(SPIFFS, "/invertPan.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/invertPan.txt", "disabled");
    if (request->hasParam(_invertTilt)) {
      inputMessage = request->getParam(_invertTilt)->value();
      write_file(SPIFFS, "/invertTilt.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/invertTilt.txt", "disabled");
    if (request->hasParam(_invertZoom)) {
      inputMessage = request->getParam(_invertZoom)->value();
      write_file(SPIFFS, "/invertZoom.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/invertZoom.txt", "disabled");
    if (request->hasParam(_swap)) {
      inputMessage = request->getParam(_swap)->value();
      write_file(SPIFFS, "/swap.txt", inputMessage.c_str());
    }
    else write_file(SPIFFS, "/swap.txt", "disabled");
    if (request->hasParam(_deadzonePT)) {
      inputMessage = request->getParam(_deadzonePT)->value();
      write_file(SPIFFS, "/deadzonePanTilt.txt", inputMessage.c_str());
    }
    if (request->hasParam(_maxPanTilt)) {
      inputMessage = request->getParam(_maxPanTilt)->value();
      write_file(SPIFFS, "/maxPanTilt.txt", inputMessage.c_str());
    }
    if (request->hasParam(_deadzoneZoom)) {
      inputMessage = request->getParam(_deadzoneZoom)->value();
      write_file(SPIFFS, "/deadzoneZoom.txt", inputMessage.c_str());
    }
    if (request->hasParam(_maxZoom)) {
      inputMessage = request->getParam(_maxZoom)->value();
      write_file(SPIFFS, "/maxZoom.txt", inputMessage.c_str());
    }
    if (request->hasParam(_cameraIP)) {
      inputMessage = request->getParam(_cameraIP)->value();
      write_file(SPIFFS, "/cameraIP.txt", inputMessage.c_str());
    }
    if (request->hasParam(_sendPort)) {
      inputMessage = request->getParam(_sendPort)->value();
      write_file(SPIFFS, "/sendPort.txt", inputMessage.c_str());
    }
    if (request->hasParam(_receivePort)) {
      inputMessage = request->getParam(_receivePort)->value();
      write_file(SPIFFS, "/receivePort.txt", inputMessage.c_str());
    }
    else {
      inputMessage = "Empty!";
      }
    Serial.println(inputMessage);
    ReadParameters();
    request->send(200, "text/text", inputMessage); });
  server.onNotFound(notFound);
  server.begin();
  Serial.println("HTTP server started");
}
void SetupMpu()
{
  byte status = mpu.begin();
  Serial.print(F("MPU6050 status: "));
  Serial.println(status);
  while (status != 0)
  {
  } // stop everything if could not connect to MPU6050
  leds[0] = CRGB::DarkOrange;
  FastLED.show();
  Serial.println(F("Calculating offsets, do not move MPU6050"));
  delay(1000);
  // mpu.upsideDownMounting = true; // uncomment this line if the MPU6050 is mounted upside-down
  angleOffsetExtra = 0;
  mpu.calcOffsets(); // gyro and accelero
  Serial.println("Done!\n");
  leds[0] = CRGB::Blue;
  FastLED.show();
}
void SetupUdp()
{
  cameraAddress.fromString(cameraIP);
  if (udp.listen(11000))
  {
    Serial.print("UDP Listening on port: ");
    Serial.println(receivePort);
    Serial.print("UDP Sending on port: ");
    Serial.println(sendPort);
    udp.onPacket([](AsyncUDPPacket packet)
                 {
      if (packet.remoteIP() != cameraAddress)
        return;
      if (packet.length()!= 3) {
        Serial.print("CameraERROR: ");
        Serial.write(packet.data(), packet.length());
        leds[0] = CRGB::Orange;
        FastLED.show(); } 
      else {
          if (packet.data()[1]>>4==4){
            if(packet.data()[1]&0x0F==1) socket1=busy;
            else if(packet.data()[1]&0x0F==2) socket2=busy;
            else {
              Serial.print("MessageERROR: ");
              Serial.write(packet.data(), packet.length());
            }
            }
          else if (packet.data()[1]>>4==5){
            if(packet.data()[1]&0x0F==1) socket1=ready;
            else if(packet.data()[1]&0x0F==2) socket2=ready;
            else {
              Serial.print("MessageERROR: ");
              Serial.write(packet.data(), packet.length());
            }
          }
          else {
              Serial.print("MessageERROR: ");
              Serial.write(packet.data(), packet.length());
            }
            leds[0] = CRGB::Green; // confirm connection
            FastLED.show();
                     } });
  }
}
void loop()
{
  mpu.update();
  LogCompensated();
  /* if (!digitalRead(PIN_BUTTON))
  {
    delay(5);
    if (!digitalRead(PIN_BUTTON))
    {
      led_status++;
      if (led_status > 3)
        led_status = 0;
      while (!digitalRead(PIN_BUTTON))
        ;
      USBSerial.print("LED status updated: ");
      USBSerial.println(led_status_string[led_status]);
    }
  } */
}
void LogCompensated()
{
  if ((millis() - timer) > interval)
  {

    // compensate angle
    float x = mpu.getAngleX();
    float y = mpu.getAngleY();
    float z = mpu.getAngleZ();
    float offsetRad = angleOffset / RAD_2_DEG;
    float x2 = cos(offsetRad) * x - sin(offsetRad) * y;
    float y2 = sin(offsetRad) * x + cos(offsetRad) * y;
    if (swap)
    {
      float temp = x2;
      x2 = y2;
      y2 = temp;
    }
    float z2 = z + angleOffsetExtra;
    int x3 = 0;
    int y3 = 0;
    int z3 = 0;
    // remap Pan/Tilt to Visca-speeds
    byte commandPanTilt[] = {0x81, 0x01, 0x06, 0x01, 0x00, 0x00, 0x03, 0x03, 0xff};
    // buf[0] = CamId;
    if ((abs(x2) > deadzonePT) && pan)
    {
      suspendPT = false;
      if (invertPan)
        x2 *= -1;
      x3 = (mapFloat(abs(x2), deadzonePT, maxPanTilt, 1, 24));    // pan (max 0x18)
      commandPanTilt[6] = (x2 > 0) ? (byte)(0x02) : (byte)(0x01); // pan  1 1eft 2 right 3 stop
    }
    x3 = min(x3, 24);
    if ((abs(y2) > deadzonePT) && tilt)
    {
      suspendPT = false;
      if (invertTilt)
        y2 *= -1;
      y3 = (mapFloat(abs(y2), deadzonePT, maxPanTilt, 1, 20));    // tilt
      commandPanTilt[7] = (y2 > 0) ? (byte)(0x01) : (byte)(0x02); // tilt 1 up 2 down 3 stop
    }
    y3 = min(y3, 20);
    commandPanTilt[4] = (x3 == 0) ? (byte)0x01 : (byte)x3;
    commandPanTilt[5] = (y3 == 0) ? (byte)0x01 : (byte)y3;

    // remap Zoom to Visca-speeds
    byte commandZoom[] = {0x81, 0x01, 0x04, 0x07, 0x00, 0xff};

    if ((abs(z2) > deadzoneZoom) && zoom)
    {
      suspendZoom = false;
      if (invertZoom)
        z2 *= -1;
      z3 = (mapFloat(abs(z2), deadzoneZoom, maxZoom, 0, 7)); //.toInt();//zoom (max 0x07)
      z3 = min(z3, 7);
      commandZoom[4] = (byte)z3;
    }
    if (z2 > deadzoneZoom)
      commandZoom[4] += 0x20;
    else if (z2 < -deadzoneZoom)
      commandZoom[4] += 0x30;

    if (!suspendPT)
      udp.writeTo(commandPanTilt, 9, cameraAddress, sendPort);
    if (!suspendZoom)
      udp.writeTo(commandZoom, 6, cameraAddress, sendPort);

    if ((commandPanTilt[6] == 0x03) && (commandPanTilt[7] == 0x03))
      suspendPT = true;
    if ((commandZoom[4] == 0x0))
      suspendZoom = true;
    timer = millis();
  }
}
void ReadParameters()
{
  Serial.println("parameters on Filesystem (connect via IP to adjust):");
  angleOffset = read_file(SPIFFS, "/angleOffset.txt").toFloat();
  deadzonePT = read_file(SPIFFS, "/deadzonePanTilt.txt").toFloat();
  maxPanTilt = read_file(SPIFFS, "/maxPanTilt.txt").toFloat();
  deadzoneZoom = read_file(SPIFFS, "/deadzoneZoom.txt").toFloat();
  maxZoom = read_file(SPIFFS, "/maxZoom.txt").toFloat();
  cameraIP = read_file(SPIFFS, "/cameraIP.txt");
  sendPort = read_file(SPIFFS, "/sendPort.txt").toInt();
  receivePort = read_file(SPIFFS, "/receivePort.txt").toInt();
  pan = (read_file(SPIFFS, "/pan.txt") == "active");
  tilt = (read_file(SPIFFS, "/tilt.txt") == "active");
  zoom = (read_file(SPIFFS, "/zoom.txt") == "active");
  invertPan = (read_file(SPIFFS, "/invertPan.txt") == "active");
  invertTilt = (read_file(SPIFFS, "/invertTilt.txt") == "active");
  invertZoom = (read_file(SPIFFS, "/invertZoom.txt") == "active");
  swap = (read_file(SPIFFS, "/swap.txt") == "active");
  suspendPT = false;
  suspendZoom = false;
}
long mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}