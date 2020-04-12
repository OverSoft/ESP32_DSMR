/*
 * Dutch smart meter reader with LCD visualization
 * Created by OverSoft (Laurens Leemans)
 * 
 * Displays current power usage in kW and shows your current daily usage.
 * Also provides a TCP port for Domoticz to connect to, to log the data.
 * Includes an OTA update function, via HTTP.
 * 
 * The webserver also provides easy access to current usage, curreny daily usage and last received message.
 * http://IP/        (Last received message, current usage and current daily usage and link to update page)
 * http://IP/current (Current usage in kW)
 * http://IP/day     (Current daily usage in kWh)
 * 
 * Domoticz can connect via the "P1 Smart Meter with LAN interface" plugin.
 * Just add the plugin, fill in the IP and port (8088).
 * 
 * 
 * I created this project for use on the Xinyuan TTGO-T
 * https://github.com/Xinyuan-LilyGO/TTGO-T-Display
 * It's an ESP32 dev-board with a 240x135 LCD attached.
 * 
 * It should run on any ESP32 with a LCD screen connected over SPI that's supported by the TFT_eSPI.
 * If you use an LCD screen with a different resolution, you might need to tweak a lot of positions and the dial JPG.
 * 
 * The dial is calibrated to -6kW max return power and +18kW max consumption power. 
 * This is the max consumption for a standard 3x25A Dutch power connection.
 * 6kW return power is based on a 20 panel solar panel setup.
 * If you want to adjust this, make a new dial JPG and alter the values below.
 * 
 * Configure your SSID, password and serial rx pin (tx pin is not used, but still left in) below.
 * The serial data should be inverted as with any DSMR connection. See README.MD for an example circuit. (TODO)
 *
 * 
 * To use, install the following libraries in the Arduino IDE:
 * - The ESP32 libraries
 * - TFT_eSPI
 * - TJpg_Decoder
 * - arduino-dmsr (https://github.com/matthijskooijman/arduino-dsmr)
 * 
 * If you're having trouble flashing this to an ESP32, set the partition scheme to "Minimal SPIFFS with OTA".
 * 
 * 
 * Used libraries:
 * 
 * arduino-dmsr by Matthijs Kooijman (parses the smart meter messages)
 * https://github.com/matthijskooijman/arduino-dsmr
 * 
 * TFT_eSPI by Bodmer (LCD library to draw things to a LCD screen)
 * https://github.com/Bodmer/TFT_eSPI
 * 
 * TJpg_Decoder by Bodmer (decodes JPGs in program memory)
 * https://github.com/Bodmer/TJpg_Decoder
 * 
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <Update.h>
#include <TJpg_Decoder.h>
#include <dsmr.h>
#include "dial.h"

// Set this up!
#define PIN_ACTIVATE          27  // Pin number that activates DSMR reader
#define PIN_RXD               25  // Pin number that is connected to DSMR serial output (via inverter)
#define PIN_TXD               26  // Pin number that send data (not used, but must be assigned)
#define MAX_POWER_CONSUMPTION 18
#define MAX_POWER_RETURN       6

// WiFi network settings
const char* ssid     = "xxx";
const char* password = "xxx";

// Dial settings
#define NEEDLE_LENGTH  35
#define NEEDLE_WIDTH    5
#define NEEDLE_RADIUS  90
#define DIAL_CENTER_X 120
#define DIAL_CENTER_Y 134

// Variables
String   lastBuf         = "";
int16_t  dialAngle       = -45;
float    currentUsage    = 0;
String   currentUsageStr = "0kW";
uint32_t currentColor    = TFT_GREEN;
String   lastTimestamp   = "";
float    dayUsage        = 0;
float    lastDelivered   = -1;
float    lastReturned    = -1;
String   dayUsageStr     = "0kW";

using MyData = ParsedData<
  /* String */ identification,
  /* String */ p1_version,
  /* String */ timestamp,
  /* String */ equipment_id,
  /* FixedValue */ energy_delivered_tariff1,
  /* FixedValue */ energy_delivered_tariff2,
  /* FixedValue */ energy_returned_tariff1,
  /* FixedValue */ energy_returned_tariff2,
  /* String */ electricity_tariff,
  /* FixedValue */ power_delivered,
  /* FixedValue */ power_returned,
  /* FixedValue */ electricity_threshold,
  /* uint8_t */ electricity_switch_position,
  /* uint32_t */ electricity_failures,
  /* uint32_t */ electricity_long_failures,
  /* String */ electricity_failure_log,
  /* uint32_t */ electricity_sags_l1,
  /* uint32_t */ electricity_sags_l2,
  /* uint32_t */ electricity_sags_l3,
  /* uint32_t */ electricity_swells_l1,
  /* uint32_t */ electricity_swells_l2,
  /* uint32_t */ electricity_swells_l3,
  /* String */ message_short,
  /* String */ message_long,
  /* FixedValue */ voltage_l1,
  /* FixedValue */ voltage_l2,
  /* FixedValue */ voltage_l3,
  /* FixedValue */ current_l1,
  /* FixedValue */ current_l2,
  /* FixedValue */ current_l3,
  /* FixedValue */ power_delivered_l1,
  /* FixedValue */ power_delivered_l2,
  /* FixedValue */ power_delivered_l3,
  /* FixedValue */ power_returned_l1,
  /* FixedValue */ power_returned_l2,
  /* FixedValue */ power_returned_l3,
  /* uint16_t */ gas_device_type,
  /* String */ gas_equipment_id,
  /* uint8_t */ gas_valve_position,
  /* TimestampedFixedValue */ gas_delivered,
  /* uint16_t */ thermal_device_type,
  /* String */ thermal_equipment_id,
  /* uint8_t */ thermal_valve_position,
  /* TimestampedFixedValue */ thermal_delivered,
  /* uint16_t */ water_device_type,
  /* String */ water_equipment_id,
  /* uint8_t */ water_valve_position,
  /* TimestampedFixedValue */ water_delivered,
  /* uint16_t */ slave_device_type,
  /* String */ slave_equipment_id,
  /* uint8_t */ slave_valve_position,
  /* TimestampedFixedValue */ slave_delivered
>;

// HTML pages
const String cssIndex = "";
const String indexStart = "<html><head><title>OverSoft smartmeter reader</title></head><body><div class='lastReading'><b>Last output:</b><br /><pre>";
const String indexEnd = "</div><br /><div class='updateLink'><a href='/updateIndex'>Update firmware</a></div></body></html>";
 
const char* updateIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({url: '/update', type: 'POST', data: data, contentType: false, processData:false,"
    "xhr: function() {"
      "var xhr = new window.XMLHttpRequest();"
      "xhr.upload.addEventListener('progress', function(evt) {"
        "if (evt.lengthComputable) {var per = evt.loaded / evt.total; $('#prg').html('progress: ' + Math.round(per*100) + '%');"
      "}}, false);"
    "return xhr;"
  "},"
  "success:function(d, s) {console.log('success!')},"
  "error: function (a, b, c) {}});"
 "});"
 "</script>";

// Setup hardware and arduino libraries
TFT_eSPI tft       = TFT_eSPI(); 
TFT_eSprite bg     = TFT_eSprite(&tft); // Sprite object for background
TFT_eSprite needle = TFT_eSprite(&tft); // Sprite object for needle
TFT_eSprite spr    = TFT_eSprite(&tft); // Sprite for meter reading

P1Reader reader(&Serial2, PIN_ACTIVATE);
WiFiServer Server(8088);
WiFiClient RemoteClient;

WebServer http(80);

/*****************/
/* LCD functions */
/*****************/
// Callback for JPG loading function, pushes JPG data to the background sprite
bool bg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if ( y >= bg.height() ) return 0;
  bg.pushImage(x, y, w, h, bitmap);
  return 1;
}
// Create the needle sprite, blatantly copied from the TFT_eSPI library examples
void createNeedle() {
  needle.setColorDepth(16);
  needle.createSprite(NEEDLE_WIDTH, NEEDLE_LENGTH);

  needle.fillSprite(TFT_BLACK);
  uint16_t piv_x = NEEDLE_WIDTH / 2;
  uint16_t piv_y = NEEDLE_RADIUS;
  needle.setPivot(piv_x, piv_y);
  needle.fillRect(0, 0, NEEDLE_WIDTH, NEEDLE_LENGTH, TFT_MAROON);
  needle.fillRect(1, 1, NEEDLE_WIDTH-2, NEEDLE_LENGTH-2, TFT_RED);
}
// Clear screen, put cursor back at the top
void cls() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0,0,2);
}
// Update the display
// Because there is not support for double buffering on the TFT_eSPI library,
// We alter the background sprite and push that the screen
// This dramatically cuts down on flicker
void drawScreen() {
  // Black out the center of the dial, overwriting the needle and current usage value
  bg.fillCircle(DIAL_CENTER_X, DIAL_CENTER_Y, 94, TFT_BLACK);

  // Push the needle to the background sprite
  needle.pushRotated(&bg, dialAngle);

  // Draw the circle surrounding the current usage value
  bg.drawCircle(DIAL_CENTER_X, DIAL_CENTER_Y, NEEDLE_RADIUS-NEEDLE_LENGTH, TFT_DARKGREY);

  // Black out the daily usage block and print the daily usage value
  bg.fillRect(0, 15, 59, 15, TFT_BLACK);
  bg.setCursor(0, 15, 2);
  bg.setTextColor(TFT_WHITE, TFT_BLACK);
  bg.print(dayUsageStr);

  // Write the current usage to the sprite
  if(currentUsageStr.length() == 5) {
    bg.setCursor(85, 110, 4);
  } else {
    bg.setCursor(77, 110, 4);
  }
  bg.setTextColor(currentColor, TFT_BLACK);
  bg.print(currentUsageStr);

  // Finally, push the sprite to the screen, effectively "flipping the buffer"
  bg.pushSprite(0, 0);
}

/********************************************************/
/* TCP socket server functions (for Domoticz interface) */
/********************************************************/
// Check current connection list, drop old connections to accept new ones
void checkForConnections() {
  if (Server.hasClient()) {
    if(RemoteClient) RemoteClient.stop();
    RemoteClient = Server.available();
    RemoteClient.setTimeout(100);
  }
}
// Calculates message CRC
String calculateCrc16(String buf) {
  uint16_t crcCalc = _crc16_update(0, '/');
  for(int i=0; i<buf.length(); i++) {
    crcCalc = _crc16_update(crcCalc, buf.charAt(i));
  }
  crcCalc = _crc16_update(crcCalc, '!');
  char formatted[4];
  sprintf(formatted, "%04X", crcCalc);
  return String(formatted);
}
// Mirror the serial telegram to the TCP socket server
void sendTelegram(String buf) {
  if(RemoteClient.connected()) {
    String crc = calculateCrc16(buf);
    RemoteClient.print("/");
    RemoteClient.print(buf);
    RemoteClient.print("!");
    RemoteClient.print(crc);
    RemoteClient.print("\r\n");
    uint8_t nullString = 0;
    RemoteClient.write(nullString);
    RemoteClient.flush();
  }
}

/******************/
/* Value updaters */
/******************/
// Set current power usage in kW
// Positive values indicate usage
// Negative values indicate returned power (for example with solarpanels)
void setCurrentValue(float currentValue) {
  currentUsage = currentValue;
  
  // Set rotation of the needle (-6kW is far left, +18kW is far right)
  dialAngle = -90 + (((currentValue + MAX_POWER_RETURN) / (MAX_POWER_RETURN + MAX_POWER_CONSUMPTION)) * 180);

  // Set the dial text color 
  if(currentValue < 0) {
    currentColor = TFT_GREEN;
  } else {
    currentColor = TFT_RED;
  }

  // Convert value to string with 1 digit after the decimal point
  char outstr[4];
  dtostrf(abs(round(currentValue * 10) / 10), currentValue >= 10 ? 4 : 3, 1, outstr);
  String convstr(outstr);
  currentUsageStr = convstr + "kW";
}

// Log daily power usage
// The timestamp is the data given by the meter, it's formatted like YYMMDDHHmmSS
// del1 and del2 are power consumed (delivered by power company) in both tarriffs in kWHs
// ret1 and ret2 are power returned (delivered by your house) in both tarriffs in kWHs
void updateUsage(String timestamp, float del1, float del2, float ret1, float ret2) {
  // Check if we're in a new day
  String currentTimestamp = timestamp.substring(0,6);
  if(!lastTimestamp.equals(currentTimestamp)) {
    lastTimestamp = currentTimestamp;
    dayUsage = 0;
  }
  
  // Calculate current day usage
  if(lastDelivered >= 0) {
    dayUsage += ((del1 + del2) - lastDelivered) - ((ret1 + ret2) - lastReturned);
    
    // Convert value to string with 1 digit after the decimal point
    char outstr[4];
    dtostrf(round(dayUsage * 10) / 10, abs(dayUsage) >= 10 ? 4 : 3, 1, outstr);
    String convstr(outstr);
    dayUsageStr = convstr + "kW";
  }

  // Update internal counter
  lastDelivered = del1 + del2;
  lastReturned  = ret1 + ret2;
}

/******************************/
/* Standard Arduino functions */
/******************************/
void setup() {
  // Setup JPG library
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(bg_output);

  // Setup TFT screen
  tft.init();
  tft.setRotation(3);
  cls();
  bg.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("Booting...");
  createNeedle();

  // Start WiFi and connect to the configured WiFi network
  cls();
  tft.println("Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }

  // Start the TCP socket server
  Server.begin();

  // Setup HTTP server
  http.on("/", HTTP_GET, []() {
    http.sendHeader("Connection", "close");
    String out = "";
    out += indexStart;
    out += lastBuf;
    out += "</pre><br /><b>Current usage: </b>" + String(currentUsage) + "kW<br /><b>Usage today:</b> " + dayUsage + "kWh";
    out += indexEnd;    
    http.send(200, "text/html", out);
  });
  http.on("/current", HTTP_GET, []() {
    http.sendHeader("Connection", "close");
    http.send(200, "text/plain", String(currentUsage));
  });
  http.on("/day", HTTP_GET, []() {
    http.sendHeader("Connection", "close");
    http.send(200, "text/plain", String(dayUsage));
  });
  http.on("/style.css", HTTP_GET, []() {
    http.sendHeader("Connection", "close");
    http.send(200, "text/css", cssIndex);
  });
  http.on("/updateIndex", HTTP_GET, []() {
    http.sendHeader("Connection", "close");
    http.send(200, "text/html", updateIndex);
  });
  http.on("/update", HTTP_POST, []() {
    http.sendHeader("Connection", "close");
    http.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = http.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        //Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  http.begin();

  // Show WiFi details to make it easy to configure Domoticz
  cls();
  tft.print("Connected to ");
  tft.println(ssid);
  tft.print("IP address: ");
  tft.println(WiFi.localIP());
  delay(5000);

  cls();
  // Create and load dial sprite
  bg.setColorDepth(16);
  bg.createSprite(240, 135);
  bg.fillSprite(TFT_BLACK);
  bg.setSwapBytes(true);
  TJpgDec.drawJpg(0, 0, dial, sizeof(dial));
  bg.setSwapBytes(false);

  // Write always visible text to bg sprite
  bg.setTextColor(TFT_WHITE, TFT_BLACK);
  bg.setCursor(0, 0, 2);
  bg.setTextSize(1);
  bg.print("Used today:");

  // Set needle pivot point
  bg.setPivot(DIAL_CENTER_X, DIAL_CENTER_Y);

  // Initialize all data
  setCurrentValue(0);
  drawScreen();

  // Open up the third hardware serial port for receiving (transmit pin is not used)
  Serial2.begin(115200, SERIAL_8N1, PIN_RXD, PIN_TXD);
  reader.enable(false);
}

void loop () {
  // Check for TCP socket connections
  checkForConnections();
  
  // Handle HTTP requests
  http.handleClient();
  
  // Handle DSMR serial data
  reader.loop();
  
  // If there's a new telegram, parse it
  if (reader.available()) {
    MyData data;
    // Store the raw data from the message to forward to Domoticz
    lastBuf = reader.raw();
    //String crc = reader.crcRaw();
    String err;
    if (reader.parse(&data, &err)) {
      // Send data to Domoticz
      sendTelegram(lastBuf);
      
      // Update daily usage
      updateUsage(
        data.timestamp,
        data.energy_delivered_tariff1.val(), 
        data.energy_delivered_tariff2.val(), 
        data.energy_returned_tariff1.val(), 
        data.energy_returned_tariff2.val()
      );
      
      // Update current usage
      setCurrentValue(data.power_delivered.val() - data.power_returned.val());
      
      // Update the screen
      drawScreen();
    } else {
      // Parser error, print error
      //tft.println(err);
    }
  }
}
