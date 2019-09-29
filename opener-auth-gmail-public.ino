#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <TM1637Display.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_MAX_DATA_WAIT 10000 //ms to wait for the client to send the request
#define HTTP_MAX_POST_WAIT 10000 //ms to wait for POST data to arrive
#define HTTP_MAX_SEND_WAIT 10000 //ms to wait for data chunk to be ACKed
#define HTTP_MAX_CLOSE_WAIT 10000 //ms to wait for the client to close the connection

const char* ssidMain = "xxx";
const char* passwordMain = "xxx";

const String authLogin = "xxx";
const String authPassword = "xxx";

const char* OTAhostname = "ESP-xxx";
const String OTAPassword = "xxx";

const int RELAY_HEAT = D7;
const int RELAY_PUMP = D8;

//pro teploměr
const int ONE_WIRE_BUS = D2;
const int TEMPERATURE_PRECISION = 10;

const int DISPLAY_CLOCK = D6;
const int DISPLAY_DATA = D5;

const int SIZE_LOG_DATA = 200; // num of logged elements -> 144
const int SAVE_LOG_SEC_DELAY = 600;  // in seconds

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress insideThermometer;

WiFiClient espClient;
ESP8266WebServer server(80);
WiFiClient client;
TM1637Display display(DISPLAY_CLOCK, DISPLAY_DATA);

long timer = millis();

int RELAY_STATUS_HEAT = LOW;
int RELAY_STATUS_PUMP = LOW;
float TEMP_C = 0.0;
String LOCAL_IP = "";

//website triggers for I/O
void handleRelayHeat();
void handleRelayPump();

String arrayLogs[SIZE_LOG_DATA];
int arrayLogsIndex = -1;
bool arrayLogsLock = false;
bool arrayLogsStarted = false;

String getHeads() {
    String content ="<!DOCTYPE html>";
    content += "<html lang=\"cs\">";
    content += "<head>";
    content += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><meta charset=\"UTF-8\">";
    content += "<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\" integrity=\"sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T\" crossorigin=\"anonymous\">";
    content += "<script src=\"https://code.jquery.com/jquery-3.4.1.min.js\" integrity=\"sha256-CSXorXvZcTkaix6Yvo6HppcZGetbYMGWSFlBw8HfCJo=\" crossorigin=\"anonymous\"></script>";
    content += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\" integrity=\"sha384-UO2eT0CpHqdSJQ6hJty5KVphtPhzWj9WO1clHTMGa3JDZwrnQq4sF86dIHNDz0W1\" crossorigin=\"anonymous\"></script>";
    content += "<script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\" integrity=\"sha384-JjSmVgyd0p3pXB1rRibZUAYoIIy6OrQ6VrjIEaFf/nJGzIxFDsf4x0xIM+B07jRM\" crossorigin=\"anonymous\"></script>";
    content += "<script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>";
    content += "</head><body>";
    content += "<div class=\"container\">";

    return content;
}

String getEnds() {
    return "</div></body></html>";
}

String getTempLogs() {
    String content = "[[\"Datum\", \"Teplota\"],";
    for (int i = 0; i <= ((sizeof(arrayLogs) / sizeof(arrayLogs[0])) - 1); i++) {
        content += arrayLogs[i];

        if (i < ((sizeof(arrayLogs) / sizeof(arrayLogs[0])) - 1) && arrayLogs[i] != "") {
            content += ",";  
        }
    }
    content += "]";

    return content;
}

String getUptimeString() {
    unsigned long runningMilis = millis() - timer;

    int runningSeconds = runningMilis / 1000;
    runningMilis %= 1000;

    int runningMinutes = runningSeconds / 60;
    runningSeconds %= 60;
    
    int runningHours = runningMinutes / 60;
    runningMinutes %= 60;   
     
    int runningDays = runningHours / 24;
    runningHours %= 24;

    return String(runningDays) + "d " + String(runningHours) + "h " + String(runningMinutes) + "m " + String(runningSeconds) + "s";
}

//Check if header is present and correct
bool isLogged() {
    Serial.println("Enter isLogged");

    if (server.hasHeader("Cookie")) {
        Serial.print("Found cookie: ");
        String cookie = server.header("Cookie");
        Serial.println(cookie);

        if (cookie.indexOf("ESPSESSIONID=1") != -1) {
            Serial.println("Authentification Successful");
            return true;
        }
    }

  Serial.println("Authentification Failed");
  return false;
}

bool accessLogged() {
    if (!isLogged()) {
        server.sendHeader("Location", "/login");
        server.sendHeader("Cache-Control", "no-cache");
        server.send(301);
        return false;
    }

    return true;
}

void redirectToBase() {
    server.sendHeader("Location", "/");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(301);  
}

//login page, also called for disconnect
void handleLogin() {
    String msg;

    if (server.hasHeader("Cookie")) {
        Serial.print("Found cookie: ");
        String cookie = server.header("Cookie");
        Serial.println(cookie);
    }

    if (server.hasArg("DISCONNECT")) {
        Serial.println("Disconnection");
        server.sendHeader("Location", "/login");
        server.sendHeader("Cache-Control", "no-cache");
        server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
        server.send(301);
        return;
    }

    if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")) {
        if (server.arg("USERNAME") == authLogin &&  server.arg("PASSWORD") == authPassword) {
            server.sendHeader("Set-Cookie", "ESPSESSIONID=1");
            redirectToBase();
            Serial.println("Log in Successful");
            return;
        }

        //Incorrect login area
        msg = "Špatné přihlašovací údaje!";
        Serial.println("Log in Failed");
    }

    //Login screen
    String content = getHeads();
    content += "<form action='/login' method='POST' class=\"mt-5\">";
    content += "<div class=\"form-group\"><input class=\"form-control form-control-lg\" type='text' name='USERNAME' placeholder='Jméno'></div>";
    content += "<div class=\"form-group\"><input class=\"form-control form-control-lg\" type='password' name='PASSWORD' placeholder='Heslo'></div>";
    content += "<div class=\"form-group\"><input class=\"form-control form-control-lg\" type='submit' name='SUBMIT' value='Přihlásit se'></div>";
    content += msg;
    content += "</form>";
    content += getEnds();
    server.send(200, "text/html", content);
}

//root page can be accessed only if authentification is ok, MAIN PAGE
void handleRoot() {
    Serial.println("Enter handleRoot");

    if (!accessLogged()) {
        return;
    }

    String heatStat = RELAY_STATUS_HEAT == LOW ? "<span class=\"text-danger font-weight-bold\">vypnuto</span>" : "<span class=\"text-success font-weight-bold\">zapnuto</span>";
    String pumpStat = RELAY_STATUS_PUMP == LOW ? "<span class=\"text-danger font-weight-bold\">vypnuto</span>" : "<span class=\"text-success font-weight-bold\">zapnuto</span>";

    String content = getHeads();
    content += "<script> var dataChart = [];</script>";
    content += "<script>var localIp = '" + LOCAL_IP + "';</script>";
    content += "<script>var hashIp = window.location.hash.substr(1); var ajaxIp = hashIp && hashIp !== '' ? hashIp : localIp;</script>";
    //content += "<meta http-equiv=refresh content= 2;/>";
    content += "<h1>TEMELÍN NA VRCHÁCH</h1>";
    content += "<hr />";
    content += "<div class=\"row d-flex align-items-center\">";
    content += "  <div class=\"col-12 col-md-3\">";
    content += "    <p>Ohřev - " + heatStat + "</p>";
    content += "    <p>Čerpadlo - " + pumpStat + "</p>";
    content += "  </div>";
    content += "  <div class=\"col-12 col-md-3\">";
    content += "    <div id=\"spinner\" class=\"spinner-border\" role=\"status\"><span class=\"sr-only\">Načítání...</span></div>";
    content += "    <div id=\"temp-result\" class=\"d-none badge badge-success p-2\">xx°C</div>";
    content += "  </div>";
    content += "  <div class=\"col-12 col-md-6\">";
    content += "    <style>.chart {     width: 100%;      max-width: 900px;      height: 300px;    }</style>";
    content += "    <div id=\"chart\" class=\"chart\"></div>";
    content += "  </div>";
    content += "</div>";
    content += "<hr />";
    content += "<a href='/heat' class=\"btn btn-info btn-block\">ZAPNOUT / VYPNOUT OHŘEV</a>";
    content += "<a href='/pump' class=\"btn btn-info btn-block\">ZAPNOUT / VYPNOUT ČERPADLO</a>";
    content += "<hr />";
    content += "<a href=\"/login?DISCONNECT=YES\"><b>Odhlásit se</b></a>";
    content += "<div class=\"mt-3\"><small>Od posledního restartu: <b id=\"uptime-string\">" + getUptimeString() + "</b></small></div>";
    content += "<div class=\"mt-1\"><small>Volná paměť: <b>" + String(system_get_free_heap_size()) + "</b></small></div>";
    content += "<script>";
    content += "var callTemp = function() { $.ajax({ url: '/get-temp', type: 'GET', complete: function(res) { $('#spinner').addClass('d-none'); $('#temp-result').removeClass('d-none').html(res.responseText); } }) };";
    content += "var callGraph = function() { $.ajax({ url: '/get-temp-logs', type: 'GET', complete: function(res) { dataChart = JSON.parse(res.responseText.replace('],]', ']]')); drawChart(); } }) };";
    content += "var callUptime = function() { $.ajax({ url: '/get-uptime', type: 'GET', complete: function(res) { $('#uptime-string').html(res.responseText); } }) };";
    content += "</script>";
    content += "<script>$(document).ready(function(){ ";
    content += "setInterval(function() { callTemp(); }, 5000);";
    content += "setInterval(function() { callGraph(); }, 5000);";
    content += "setInterval(function() { callUptime(); }, 10000);";
    content += "callTemp();callGraph();";
    content += "})</script>";
    content += "<script type=\"text/javascript\">";
    content += "  google.charts.load('current', {'packages':['corechart']});";
    content += "  google.charts.setOnLoadCallback(drawChart);";
    content += "  function drawChart() {";
    content += "    var data = google.visualization.arrayToDataTable(dataChart);";
    content += "    var options = { title: 'Teplota', curveType: 'function', legend: { position: 'none' }, };";
    content += "    var chart = new google.visualization.LineChart(document.getElementById('chart'));";
    content += "    chart.draw(data, options);";
    content += "  }";
    content += "</script>";
   
    content += getEnds();

    server.send(200, "text/html", content);
}

//no need authentification
void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";

    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }

    server.send(404, "text/plain", message);
}

//I/O TRIGGERS VIA webpage 
void handleRelayHeat() {
    if (!accessLogged()) {
        return;
    }

    if (RELAY_STATUS_HEAT == LOW) {
        digitalWrite(RELAY_HEAT, HIGH);
        RELAY_STATUS_HEAT = HIGH;
    } else {
        digitalWrite(RELAY_HEAT, LOW);
        RELAY_STATUS_HEAT = LOW;
    }

    EEPROM.write(0, RELAY_STATUS_HEAT);
    EEPROM.commit();

    redirectToBase();
    return;
}

void handleRelayPump() {
    if (!accessLogged()) {
        return;
    }

    if (RELAY_STATUS_PUMP == LOW) {
        digitalWrite(RELAY_PUMP, HIGH);
        RELAY_STATUS_PUMP = HIGH;
    } else {
        digitalWrite(RELAY_PUMP, LOW);
        RELAY_STATUS_PUMP = LOW;
    }

    EEPROM.write(1, RELAY_STATUS_PUMP);
    EEPROM.commit();

    redirectToBase();
    return;
}

void handleGetTemp() {
    float tempC = TEMP_C;
    String tempString; //size of the number
    tempString = String(tempC);
    
    String content = "Teplota vody: " + tempString + " °C";
    
    server.send(200, "text/html", content);
}

void handleGetTempLogs() {
    String content = getTempLogs();
    server.send(200, "text/html", content);
}

void handleGetUptime() {
    String content = getUptimeString();
    server.send(200, "text/html", content);
}

void setup(void) {
    EEPROM.begin(2);
    Serial.begin(115200);

    // Initialize the output variables as outputs
    pinMode(RELAY_HEAT, OUTPUT);
    pinMode(RELAY_PUMP, OUTPUT);
    
    byte heatEeprom = EEPROM.read(0);
    if (heatEeprom != 255) {
        digitalWrite(RELAY_HEAT, heatEeprom);
        RELAY_STATUS_HEAT = heatEeprom;
    } else {
        digitalWrite(RELAY_HEAT, RELAY_STATUS_HEAT);  
    }
    
    byte pumpEeprom = EEPROM.read(1);
    if (pumpEeprom != 255) {
        digitalWrite(RELAY_PUMP, pumpEeprom);
        RELAY_STATUS_PUMP = pumpEeprom;
    } else {
        digitalWrite(RELAY_PUMP, RELAY_STATUS_PUMP);  
    }

    Serial.print("Eeprom heat ");
    Serial.println(heatEeprom);

    Serial.print("Eeprom pump ");
    Serial.println(pumpEeprom);

    Serial.println("==== SET WIFI ====");
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    Serial.println("");

    WiFi.begin(ssidMain, passwordMain);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    wifi_set_sleep_type(NONE_SLEEP_T);

    Serial.println("");

    Serial.print("IP address: ");
    LOCAL_IP = WiFi.localIP().toString();
    Serial.println(LOCAL_IP);

    //EACH TRIGGER NEEDS THIS TO BE ADDED
    server.on("/", handleRoot);
    server.on("/heat", handleRelayHeat);
    server.on("/pump", handleRelayPump);
    server.on("/get-temp", handleGetTemp);
    server.on("/get-temp-logs", handleGetTempLogs);
    server.on("/get-uptime", handleGetUptime);
    server.on("/login", handleLogin);

    server.onNotFound(handleNotFound);

    //here the list of headers to be recorded
    const char * headerKeys[] = {"User-Agent", "Cookie"};
    size_t headerKeysSize = sizeof(headerKeys) / sizeof(char*);
    //ask server to track these headers
    server.collectHeaders(headerKeys, headerKeysSize );
    server.begin();
    Serial.println("HTTP server started");

      //teplom�ry
    sensors.begin();
    Serial.println("***************************************************");
    Serial.print("Pocet teplomeru: ");
    Serial.println(sensors.getDeviceCount(), DEC);
    //zjisti adresy
    oneWire.reset_search();
    if (!oneWire.search(insideThermometer)) Serial.println("Vnitrni teplomer nenalezen!");
    Serial.print("Adresa teplomeru 1: ");
    printAddress(insideThermometer);
    Serial.println();
  
    //nastav rozlišení
    sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);

    //na�ti všechny teploměry
    sensors.requestTemperatures();
  
    //vytiskni data na seriák
    printData(insideThermometer);

    display.setBrightness(0x0a);

    configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov", "ntp.nic.cz");
    Serial.println("\nWaiting for time");
    while (!time(nullptr)) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nTime done");

    ArduinoOTA.setHostname(OTAhostname);
    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void loop(void) {
    try {
        //Serial.println(WiFi.status());Serial.println(WL_CONNECTED);

        ArduinoOTA.handle();
        server.handleClient();
    
        sensors.requestTemperatures();
        TEMP_C = sensors.getTempC(insideThermometer);
    
        int displayTemp = TEMP_C * 100;
        display.showNumberDec(displayTemp);
    
        time_t now = time(nullptr);
        char formattedTime[20];
        strftime(formattedTime, 20, "%d.%m.%Y %H:%M", localtime(&now));
        String timeString = String(formattedTime);
        int timeInt = (int) now;

        if ((timeInt % SAVE_LOG_SEC_DELAY == 0 && !arrayLogsLock) || !arrayLogsStarted) {
            if (arrayLogsIndex < (SIZE_LOG_DATA - 1)) {
                arrayLogsIndex = arrayLogsIndex + 1;  
            } else {
                String revesedArrayLogs[SIZE_LOG_DATA];
                
                for(int i = 0, j = (SIZE_LOG_DATA - 1); i <= (SIZE_LOG_DATA - 1); i++, j--) {
                    revesedArrayLogs[i] = arrayLogs[j]; 
                }
        
                for(int i = 0, j = (SIZE_LOG_DATA - 2); i <= (SIZE_LOG_DATA - 2); i++, j--) {
                    arrayLogs[i] = revesedArrayLogs[j];  
                }     
            }
        
            arrayLogs[arrayLogsIndex] = "[\"" + timeString + "\"," + String(displayTemp) +"0]";
     
            arrayLogsLock = true;
            arrayLogsStarted = true;
            //yield();
        } else {
            arrayLogsLock = false;
        }
        //Serial.print("Pocet zaznamu - " + String(arrayLogsIndex) + " -- ");
        //Serial.println(timeString);
    
        //delay(500);
    } catch (...) {
        Serial.println("Na necem to chtelo spadnout...");
    }
}



//pro teploměry
void printAddress(DeviceAddress deviceAddress) {
    for (uint8_t i = 0; i < 8; i++) {
        // zero pad the address if necessary
        if (deviceAddress[i] < 16) Serial.print("0");
        Serial.print(deviceAddress[i], HEX);
    }
}

void printData(DeviceAddress deviceAddress) {
    Serial.print("Adresa teplomeru ");
    printAddress(deviceAddress);
    Serial.print(":");
    printTemperature(deviceAddress);
}

void printTemperature(DeviceAddress deviceAddress) {
    float tempC = sensors.getTempC(deviceAddress);
    Serial.print("Teplota: ");
    Serial.print(tempC);
    Serial.write(176);
    Serial.println("C");
}
