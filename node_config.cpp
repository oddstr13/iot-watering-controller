#include <wifi_credentials.h>
#include <LittleFS.h>

#include <ArduinoJson.hpp>
using namespace ArduinoJson;

#ifndef SPI_FLASH_SEC_SIZE
    #define SPI_FLASH_SEC_SIZE 4096
#endif

#include "node_config.h"
#include "global_buffers.h"

char ssid[33] = WIFI_SSID;
char password[65] = WIFI_PASSWORD;

char node_id[17] = "NODENAME";

unsigned long packet_interval = 1*60*1000;
bool tx_enabled = false;

bool api_enabled = false;
char api_url[256] = "http://www.ukhas.net/api/upload";

bool multicast_enabled = false;
char multicast_address[41] = "ff18::554b:4841:536e:6574:1";
char multicast_address_other[41] = "ff18::554b:4841:536e:6574:2";
uint16_t multicast_port = 20750;
uint8_t multicast_ttl = 8;

#define JTYPE2STR(T) if (obj.is<T>()) {return #T;}

char* jsonObjectType(const JsonVariant obj) {
    JTYPE2STR(char*);
    JTYPE2STR(String);
    JTYPE2STR(byte);
    JTYPE2STR(char);
    JTYPE2STR(unsigned short);
    JTYPE2STR(short);
    JTYPE2STR(unsigned int);
    JTYPE2STR(int);
    JTYPE2STR(unsigned long);
    JTYPE2STR(long);
    JTYPE2STR(float);
    JTYPE2STR(double);
    JTYPE2STR(bool);
    JTYPE2STR(const char*);
    JTYPE2STR(JsonArray);
    JTYPE2STR(JsonObject);
    return "I have no idea of what this is.";
}

#define VALIDATE_CHAR_LEN(KEY, LEN) {\
    Serial.println("Validating " #KEY);\
    if (obj.containsKey(#KEY)) {\
        if (obj[#KEY].is<char*>()) {\
            if (strlen(obj[#KEY].as<char*>()) > LEN) {\
                Serial.println("validation:ERR: " #KEY " too long.");\
                return false;\
            }\
        } else {\
            Serial.print("validation:ERR: " #KEY " type missmatch: ");\
            Serial.println(jsonObjectType(obj[#KEY]));\
            return false;\
        }\
    }\
}

#define VALIDATE_TYPE(KEY, T) {\
    Serial.println("Validating " #KEY);\
    if (obj.containsKey(#KEY) && !obj[#KEY].is<T>()) {\
        Serial.print("validation:ERR: " #KEY " type missmatch: ");\
        Serial.println(jsonObjectType(obj[#KEY]));\
        return false;\
    }\
}

bool validateConfig(const JsonObject obj) {
    Serial.println("validateConfig");

    if (obj.isNull()) {
        Serial.println("validation:ERR: obj is null.");
        return false;
    }

    VALIDATE_CHAR_LEN(ssid, 31);
    VALIDATE_CHAR_LEN(password, 64);
    VALIDATE_TYPE(clear_password, bool);
    VALIDATE_CHAR_LEN(node_id, 16);
    VALIDATE_CHAR_LEN(api_url, 255);
    VALIDATE_CHAR_LEN(multicast_address, 40);
    VALIDATE_CHAR_LEN(multicast_address_other, 40);

    VALIDATE_TYPE(tx_enabled, bool);
    VALIDATE_TYPE(api_enabled, bool);
    VALIDATE_TYPE(multicast_enabled, bool);
    VALIDATE_TYPE(multicast_port, uint16_t);
    VALIDATE_TYPE(multicast_ttl, uint8_t);

    VALIDATE_TYPE(write_flash, bool);

    return true;
}

#define LOAD_CHAR_IF_PRESENT(KEY) {\
    if (obj.containsKey(#KEY)) {\
        strcpy(KEY, obj[#KEY].as<char*>());\
    }\
}

#define LOAD_IF_PRESENT(KEY, T) {\
    if (obj.containsKey(#KEY)) {\
        KEY = obj[#KEY].as<T>();\
    }\
}

int parseConfig(const JsonObject obj) {
    if (validateConfig(obj)) {
        LOAD_CHAR_IF_PRESENT(ssid);
        if (obj.containsKey("clear_password") && obj["clear_password"]) {
            password[0] = 0; // Clear password
        } else {
            LOAD_CHAR_IF_PRESENT(password);
        }
        LOAD_CHAR_IF_PRESENT(node_id);
        LOAD_CHAR_IF_PRESENT(api_url);
        LOAD_CHAR_IF_PRESENT(multicast_address);
        LOAD_CHAR_IF_PRESENT(multicast_address_other);

        LOAD_IF_PRESENT(tx_enabled, bool);
        LOAD_IF_PRESENT(api_enabled, bool);
        LOAD_IF_PRESENT(multicast_enabled, bool);

        LOAD_IF_PRESENT(multicast_port, uint16_t);
        LOAD_IF_PRESENT(multicast_ttl, uint8_t);
        LOAD_IF_PRESENT(packet_interval, unsigned long);

        if (obj.containsKey("write_flash") && obj["write_flash"]) {
            saveConfig();
        }

        return 0;
    } else {
        Serial.println("parseConfig failed to validate");
        return -1;
    }
}

int readConfig() {
    LittleFS.begin();
    auto filehandle = LittleFS.open("/config.json", "r");

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, filehandle);
    filehandle.close();

    if (err) {
        return err.code();
    }

    if (!doc.is<JsonObject>()) {
        return -1;
    }

    return parseConfig(doc.as<JsonObject>());
}

#define SET_CHAR(KEY) {\
    obj[#KEY] = (const char*)KEY;\
}

/*
 * Using macro here gives the benefit of ensuring
 * that the string key is the same as the variable name.
 *
 * Linter will yell if it's miss-spelled,
 * something that doesn't usually happen for strings.
 */
#define SET_KEY(KEY) {\
    obj[#KEY] = KEY;\
}

void setConfig(JsonObject obj, bool include_password) {
    SET_CHAR(ssid);
    if (include_password) {
        SET_CHAR(password);
    } else {
        if (strlen(password) > 0) {
            obj["password_set"] = true;
        }
    }
    SET_CHAR(node_id);
    SET_CHAR(api_url);
    SET_CHAR(multicast_address);
    SET_CHAR(multicast_address_other);

    SET_KEY(tx_enabled);
    SET_KEY(api_enabled);
    SET_KEY(multicast_enabled);

    SET_KEY(multicast_port);
    SET_KEY(multicast_ttl);
    SET_KEY(packet_interval);
}

bool saveConfig() {
    Serial.print(F("Writing config to LittleFS..."));
    LittleFS.begin();

    StaticJsonDocument<512> doc;
    setConfig(doc.to<JsonObject>());

    auto filehandle = LittleFS.open("/config.json", "w");

    serializeJson(doc, filehandle);
    filehandle.close();
    Serial.println(F(" OK"));
    return true;
}

DynamicJsonDocument getConfigDoc(bool include_password) {
    DynamicJsonDocument doc = DynamicJsonDocument(512);
    doc.to<JsonObject>();
    setConfig(doc.as<JsonObject>(), include_password);
    return doc;
}

size_t dumpConfig(Stream &stream, bool include_password) {
    auto doc = getConfigDoc(include_password);
    return serializeJsonPretty(doc, stream);
}
