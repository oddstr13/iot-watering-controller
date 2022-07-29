#include <Arduino.h>
#include "http_server.h"

#include "node_config.h"

#define _WIFI_LOGLEVEL_ 4
#define USE_WIFI_NINA false
#include <WiFiWebServer.h>
#include <AddrList.h>

#include <ArduinoJson.h>
#include <StreamUtils.h>

WiFiWebServer server(80);

#include <FS.h>
#if ESP8266
    FS* filesystem = &SPIFFS;
    #define fs(M) filesystem->M
#else
    #include <LittleFS.h>
    #define fs(M) LittleFS.M
#endif

// Black magic!
#define STRINGIFY(X) #X
static const char INDEX_PAGE[] PROGMEM = ""
#include "index.html.h"
;

String emptyString = String();

String getContentType(String filename) {
    String ext = filename.substring(filename.lastIndexOf('.'));

    if (server.hasArg("download")) {
        return "application/octet-stream";
    } else if (ext == ".html" || ext == ".htm") {
        return "text/html";
    } else if (ext == (".css")) {
        return "text/css";
    } else if (ext == (".js")) {
        return "application/javascript";
    } else if (ext == (".png")) {
        return "image/png";
    } else if (ext == (".gif")) {
        return "image/gif";
    } else if (ext == (".jpg")) {
        return "image/jpeg";
    } else if (ext == (".ico")) {
        return "image/x-icon";
    } else if (ext == (".xml")) {
        return "text/xml";
    } else if (ext == (".pdf")) {
        return "application/x-pdf";
    } else if (ext == (".zip")) {
        return "application/x-zip";
    } else if (ext == (".gz")) {
        return "application/x-gzip";
    }
    return "text/plain";
}

bool handleFileRead(String path) {
    Serial.print("handleFileRead: ");
    Serial.println(path);

    if (path.endsWith("/")) {
        path += "index.h";
    }

    if (fs(exists)(path + ".gz")) {
        path += ".gz";
    } else if (!fs(exists)(path)) {
        return false;
    }

    String contentType = getContentType(path);
    File file = fs(open)(path, "r");
    server.streamFile(file, contentType);
    file.close();

    return true;
}

void handleConfig_GET() {
    auto doc = getConfigDoc(false);

    server.setContentLength(measureJson(doc));

    server.send(200, F("application/json"), emptyString);

    auto client = server.client();
    WriteBufferingStream bufferedStream(client, 64);
    serializeJson(doc, bufferedStream);
    bufferedStream.flush();
}

void handleConfig_PATCH() {
    DynamicJsonDocument doc(512);
    auto client = server.client();

    DeserializationError err = deserializeJson(doc, server.arg("plain"));

    int code = 200;

    switch (err.code()) {
        case err.Ok:
            break;
        case err.EmptyInput:
        case err.IncompleteInput:
        case err.InvalidInput:
        case err.TooDeep:
            code = 400;
            break;
        case err.NoMemory:
        default:
            code = 500;
    }

    if (err) {
        doc.clear();
        doc["error"] = err.c_str();
    } else {
        int res = parseConfig(doc.as<JsonObject>());

        doc.clear();
        if (res == 0) {
            doc["ok"] = "Config set";
        } else {
            doc["error"] = "Validation error";
            code = 422;
        }
    }
    server.setContentLength(measureJson(doc));
    server.send(code, F("application/json"), emptyString);
    WriteBufferingStream bufferedStream(client, 64);
    serializeJson(doc, bufferedStream);
    bufferedStream.flush();
}

void http_server_setup() {
    fs(begin)();


    server.on("/favicon.ico", HTTP_GET, []() {
        if (!handleFileRead(server.uri())) {
            server.send(204);
        }
    });

    server.on("/", HTTP_GET, []() {
        size_t page_size = strlen(INDEX_PAGE);
        server.setContentLength(page_size);
        server.send(200, F("text/html"), emptyString);

        // WiFiClient does not know to chop up large socket writes,
        // and the chip on the PicoW doesn't seem to like larger
        // chunks than TCP_MSS(1460) * 2. Packet(s) are sent right away,
        // limited in size by TCP_MSS.
        /*
            01:25:37.570 -> :wr 85 0
            01:25:37.570 -> :wrc 85 85 0
            01:25:37.570 -> :ref 2
            01:25:37.570 -> :wr 2048 0
            01:25:37.570 -> :wrc 2048 2048 -1   // Error?
            01:25:37.604 -> :ack 85
            01:25:37.604 -> :wr 2048 0
            01:25:37.604 -> :wrc 2048 2048 0    // 2048 bytes written to WiFi
            01:25:37.637 -> :ack 1460           // TCP ACK
            01:25:37.637 -> :ack 588
            01:25:37.637 -> :wr 1803 0
            01:25:37.637 -> :wrc 1803 1803 0
            01:25:37.703 -> :ack 1460
            01:25:37.703 -> :ack 343
            01:25:37.703 -> :rcl pb=0 sz=-1
            01:25:37.703 -> :abort
        */
        WiFiClient client = server.client();
        const int chunksize = 2920;
        int chunks = ceil((double)page_size / chunksize);
        for (int i=0;i<chunks;i++) {
            int position = i*chunksize;
            int bytes_left = page_size - position;

            int written = client.write(&INDEX_PAGE[position], min(chunksize, bytes_left));
            Serial.printf("%i bytes written.", written);
            Serial.println();
            client.flush();
        }
    });

    server.on("/ipconfig", HTTP_GET, []() {

        String ipconfig;

        // Print IP configuration..
        for (auto a : addrList) {
            ipconfig.concat(a.ifnumber());
            ipconfig.concat(':');
            ipconfig.concat(a.ifname());
            ipconfig.concat(": ");
            ipconfig.concat(a.isV4() ? "IPv4" : "IPv6");
            ipconfig.concat(a.isLocal() ? " local " : " ");
            ipconfig.concat(a.toString());

            if (a.isLegacy()) {
                ipconfig.concat(" mask:");
                ipconfig.concat(a.netmask().toString());
                ipconfig.concat(" gw:");
                ipconfig.concat(a.gw().toString());
            }
            ipconfig.concat("\n");
        }
        server.send(200, F("text/plain"), ipconfig);
    });


    server.on("/config.json", HTTP_GET, handleConfig_GET);
    server.on("/config.json", HTTP_PATCH, handleConfig_PATCH);

    server.onNotFound([]() {
        if (!handleFileRead(server.uri())) {
            server.send(404, "text/plain", "FileNotFound");
        }
    });

    server.begin();
}

void http_server_update() {
    server.handleClient();
}
