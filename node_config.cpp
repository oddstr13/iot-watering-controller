#include <wifi_credentials.h>
#include <EEPROM.h>

#include "node_config.h"
#include "global_buffers.h"

char ssid[33] = WIFI_SSID;
char password[65] = WIFI_PASSWORD;

char node_id[17] = "NODENAME";

unsigned long packet_interval = 1*60*1000;
bool tx_enabled = false;

bool api_enabled = false;
char api_url[256] = "http://www.ukhas.net/api/upload";

bool multicast_enabled = true;
char multicast_address[41] = "ff18::554b:4841:536e:6574:1";
char multicast_address_other[41] = "ff18::554b:4841:536e:6574:2";
uint16_t multicast_port = 20750;
uint8_t multicast_ttl = 8;

#define VALIDATE_CHAR_LEN(KEY, LEN) {\
    if (obj.containsKey(#KEY)) {\
        if (obj[#KEY].is<char*>()) {\
            if (strlen(obj[#KEY].as<char*>()) > LEN) {\
                return false;\
            }\
        } else {\
            return false;\
        }\
    }\
}

#define VALIDATE_TYPE(KEY, T) {\
    if (obj.containsKey(#KEY) && !obj[#KEY].is<T>()) {\
        return false;\
    }\
}

bool validateConfig(const JsonObject obj) {
    if (obj.isNull()) {
        return false;
    }

    VALIDATE_CHAR_LEN(ssid, 31);
    VALIDATE_CHAR_LEN(password, 64);
    VALIDATE_CHAR_LEN(node_id, 16);
    VALIDATE_CHAR_LEN(api_url, 255);
    VALIDATE_CHAR_LEN(multicast_address, 40);
    VALIDATE_CHAR_LEN(multicast_address_other, 40);

    VALIDATE_TYPE(tx_enabled, bool);
    VALIDATE_TYPE(api_enabled, bool);
    VALIDATE_TYPE(multicast_enabled, bool);
    VALIDATE_TYPE(multicast_port, uint16_t);
    VALIDATE_TYPE(multicast_ttl, uint8_t);
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
        LOAD_CHAR_IF_PRESENT(password);
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

        return 0;
    } else {
        return -1;
    }
}

int readConfig() {
    EEPROM.begin(SPI_FLASH_SEC_SIZE);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, EEPROM.getConstDataPtr(), SPI_FLASH_SEC_SIZE);

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
    EEPROM.begin(SPI_FLASH_SEC_SIZE);

    StaticJsonDocument<512> doc;
    setConfig(doc.to<JsonObject>());

    serializeJson(doc, EEPROM.getDataPtr(), SPI_FLASH_SEC_SIZE);
    EEPROM.commit();
}

void dumpConfig(Stream &stream, bool include_password) {
    StaticJsonDocument<512> doc;
    doc.to<JsonObject>();
    setConfig(doc.as<JsonObject>(), include_password);

    serializeJsonPretty(doc, stream);
}
