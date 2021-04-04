#include "http_server.h"

#include "node_config.h"

#include <ESP8266WebServer.h>
#include <FS.h>
#include <StreamUtils.h>

ESP8266WebServer server(80);
FS* filesystem = &SPIFFS;

// Black magic!
#define STRINGIFY(X) #X
static const char INDEX_PAGE[] PROGMEM = ""
#include "index.html.h"
;

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

    if (filesystem->exists(path + ".gz")) {
        path += ".gz";
    } else if (!filesystem->exists(path)) {
        return false;
    }

    String contentType = getContentType(path);
    File file = filesystem->open(path, "r");
    server.streamFile(file, contentType);
    file.close();

    return true;
}

void handleConfig_GET() {
    auto doc = getConfigDoc(false);

    server.setContentLength(measureJson(doc));

    server.send(200, F("application/json"), String());

    auto client = server.client();
    WriteBufferingStream bufferedStream(client, 64);
    serializeJson(doc, bufferedStream);
    bufferedStream.flush();
}

bool handleConfig_PATCH() {
    DynamicJsonDocument doc(512);
    auto client = server.client();

    DeserializationError err = deserializeJson(doc, server.arg("plain"));

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
        }
    }
    server.setContentLength(measureJson(doc));
    server.send(200, F("application/json"), String());
    WriteBufferingStream bufferedStream(client, 64);
    serializeJson(doc, bufferedStream);
    bufferedStream.flush();
}

void http_server_setup() {
    filesystem->begin();

    server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"), FPSTR(INDEX_PAGE));
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
