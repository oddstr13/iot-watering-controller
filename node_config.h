#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

extern char ssid[33];
extern char password[65];

extern char node_id[17];
extern unsigned long packet_interval;
extern bool tx_enabled;

extern bool api_enabled;
extern char api_url[256];

extern bool multicast_enabled;
extern char multicast_address[41];
extern char multicast_address_other[41];
extern uint16_t multicast_port;
extern uint8_t multicast_ttl;

int checkConfig();
int readConfig();
bool saveConfig();

void setConfig(JsonObject obj, bool include_password=true);

void dumpConfig();

#endif
