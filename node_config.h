#include <wifi_credentials.h>

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* node_id  = "NODENAME";
const unsigned long packet_interval = 1*60*1000;
const bool tx_enabled = false;

const bool api_enabled = false;
const char* api_url  = "http://www.ukhas.net/api/upload";

const bool multicast_enabled = true;
const char* multicast_address = "ff18::554b:4841:536e:6574:1";
const char* multicast_address_other = "ff18::554b:4841:536e:6574:2";
const uint16_t multicast_port = 20750;
const int multicast_ttl = 5;
