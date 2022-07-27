#include "http_server.h"

#include "node_config.h"

#define _WIFI_LOGLEVEL_ 4
#define USE_WIFI_NINA false
#include <WiFiWebServer.h>

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

    F("asdf");
    server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"), PSTR(INDEX_PAGE));
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
