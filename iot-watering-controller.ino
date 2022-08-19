#include <WiFi.h>
#include <WiFiUdp.h>
#include <AddrList.h>

//#include <HTTPClient.h>

#include <cyw43_stats.h>
#include <lwip/stats.h>
#define STAT(name) String(#name " rx=") + String(lwip_stats.name.recv) + String(" tx=") + String(lwip_stats.name.xmit) + String(" drop=") + String(lwip_stats.name.drop) + String("\r\n")

//#include <Ticker.h>

#ifndef HTTP_CODE_OK
#define HTTP_CODE_OK 200
#endif
#ifndef STATION_IF
#define STATION_IF 0
#endif

#include <Buffer.h>
#include <iso646.h>

#include "node_config.h"
#include "global_buffers.h"
#include "openshell_utils.h"
#include "http_server.h"

IPAddress getIP6Address(int ifn=STATION_IF) {

    // Prefer non-local address
    for (auto a: addrList) {
        if (a.ifnumber() == ifn && a.addr().isV6() && !a.addr().isLocal()) {
            return a.addr();
        }
    }

    // Fall back to local address
    for (auto a: addrList) {
        if (a.ifnumber() == ifn && a.addr().isV6()) {
            return a.addr();
        }
    }

    // Final fall-back to the IPv6 wildcard address; [::]
    return INADDR_ANY;
}

void wifiScanCallback(int found);
double latitude = NAN;
double longitude = NAN;

WiFiUDP Udp;
IPAddress multicast_ip, multicast_ip_other;

unsigned long packet_timer;

#define SERIAL_PRINT(data) {\
    Serial.print(data);\
    Serial1.print(data);\
}

#define SERIAL_PRINTLN(data) {\
    Serial.println(data);\
    Serial1.println(data);\
}

#define SERIAL_PRINT_VAR(variable) {\
    SERIAL_PRINT(#variable ": ");\
    SERIAL_PRINTLN(variable);\
}

void printNodeConfig() {
    SERIAL_PRINT_VAR(ssid);
    SERIAL_PRINT_VAR(node_id);
    SERIAL_PRINT_VAR(packet_interval);
    SERIAL_PRINT_VAR(tx_enabled);

    SERIAL_PRINT_VAR(api_enabled);
    SERIAL_PRINT_VAR(api_url);

    SERIAL_PRINT_VAR(multicast_enabled);
    SERIAL_PRINT_VAR(multicast_address);
    SERIAL_PRINT_VAR(multicast_address_other);
    SERIAL_PRINT_VAR(multicast_port);
    SERIAL_PRINT_VAR(multicast_ttl);
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(115200);
    delay(2000);
    Serial.println();
    Serial1.println();

    int config_status = readConfig();
    if (config_status != 0) {
        Serial.print("Config reading error: ");
        Serial.println(config_status);
        saveConfig();
    }
    dumpConfig(Serial);

    SERIAL_PRINTLN();
    printNodeConfig();

    // Set up WiFi
    WiFi.mode(WIFI_STA); // Disable access point.
    #ifdef ESP8266
    WiFi.softAPdisconnect(true);
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable sleep (In case we want broadcast(multicast) data)
    int networks_found = WiFi.scanNetworks(false, true);
    #elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
    WiFi.noLowPowerMode();
    int networks_found = WiFi.scanNetworks();
    #endif

    // Set hostname: UKHASnet-ESP-{node_id}-{chip_id}
    //uint8_t ___foo[WL_MAC_ADDR_LENGTH] = {0};
    WiFi.setHostname((String("ESP-") + String(node_id)).c_str() /*+ "-" + String(WiFi.macAddress(*___foo) & 0xffffff, HEX)*/);
    //SERIAL_PRINT("Hostname: ");
    //SERIAL_PRINTLN(WiFi.getHostname());

    SERIAL_PRINT("Connecting to ");
    SERIAL_PRINTLN(ssid);

    WiFi.begin(ssid, password);

    // Wait for IP address
    for (bool configured = false; !configured; ) {
        for (auto iface: addrList) {
            //iface.addr().printTo(Serial);
            //Serial.println();
            if (configured = !iface.addr().isLocal()) {
                break;
            }
        }
        delay(500);
        SERIAL_PRINT(".");
    }
    SERIAL_PRINTLN();

    // Print IP configuration..
    for (auto a : addrList) {
        SERIAL_PRINT(a.ifnumber());
        SERIAL_PRINT(':');
        SERIAL_PRINT(a.ifname());
        SERIAL_PRINT(": ");
        SERIAL_PRINT(a.isV4() ? "IPv4" : "IPv6");
        SERIAL_PRINT(a.isLocal() ? " local " : " ");
        SERIAL_PRINT(a.toString());

        if (a.isLegacy()) {
            SERIAL_PRINT(" mask:");
            SERIAL_PRINT(a.netmask());
            SERIAL_PRINT(" gw:");
            SERIAL_PRINT(a.gw());
        }
        SERIAL_PRINTLN();
    }

    // Attempt to get geolocation
    wifiScanCallback(networks_found);

    http_server_setup();

    // Should make it transmit a packet right now, then next after packet_interval
    packet_timer = millis() - packet_interval;

    multicast_ip.fromString(multicast_address);
    multicast_ip_other.fromString(multicast_address_other);

    SERIAL_PRINTLN("Setup done.");
}


bool packet_received = false;
#define databuf_LEN 1032
uint8_t databuf[databuf_LEN];
uint16_t dataptr = 0;
float lastrssi = 0;

WiFiClientSecure client;

//HTTPClient http;

void upload(bool fake=false);
void upload(bool fake) {
    yield();

    if (not api_enabled) {
        fake = true;
    }

    init_global_buffers();

    if (not fake) {
        //http.setReuse(true);
        Serial.println("Uploading...");
        //http.begin(api_url);
        //http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    }

    uploadbuf.reset();
    uploadbuf.add("origin=");
    uploadbuf.add(node_id); //gateway nodes (our) ID
    uploadbuf.add("&data=");

    // Escape packet for upload
    char c;
    for (unsigned int i=0; i<dataptr; i++) {
        c = databuf[i];
        if (c == 45 or c == 46 or c == 95 or c == 126
            or (48 <= c and c <= 58)
            or (65 <= c and c <= 90)
            or (97 <= c and c <= 122)) {
            uploadbuf.add(c);
        } else {
            uploadbuf.add('%');
            uploadbuf.add(BASE16[(c & 0xf0) >> 4]);
            uploadbuf.add(BASE16[ c & 0x0f]);
        }
    }

    if (lastrssi < -0.5) {
        uploadbuf.add("&rssi=");
        uploadbuf.addNumber(lastrssi, 1, true);
    }
    uploadbuf.add('\0'); //null terminate the string for safe keeping

    Serial.write(uploadbuf.buf, uploadbuf.ptr-1);
    Serial.println();

    if (not fake) {
        //http.POST((const char*)uploadbuf.buf);

        //http.writeToStream(&Serial);
        //http.end();
    }

    Serial.println();
}

void multicast(IPAddress target) {
    if (!multicast_enabled) {
        return;
    }
    yield();
    Serial.print("Sending multicast packet to ");
    Serial.print(target.toString());
    Serial.print(" port ");
    Serial.print(multicast_port, DEC);
    Serial.println("...");

    init_global_buffers();

    // https://oddstr13.openshell.no/paste/VnOnqpUa/
    uploadbuf.reset();

    uploadbuf.add(0x16); // Dict start

    // -----

    uploadbuf.add((char)(0x80 | 6)); // UTF-8 string, 6 characters
    uploadbuf.add("packet");

    if (dataptr > 63) {
        uploadbuf.add(0xC1); // Bytestring, uint16_t n characters
        uploadbuf.addBytes((uint16_t)dataptr); // n
    } else {
        uploadbuf.add((char)0x40 | dataptr); // bytestring, 0-63 characters
    }
    uploadbuf.add((char*)databuf, dataptr);

    // -----

    uploadbuf.add((char)(0x80 | 2));
    uploadbuf.add("gw");

    uploadbuf.add((char)(0x80 | strlen(node_id)));
    uploadbuf.add(node_id);

    // -----

    uploadbuf.add((char)(0x80 | 4));
    uploadbuf.add("rssi");

    uploadbuf.add((char)0x08); // int8_t
    uploadbuf.add(static_cast<int8_t>(lastrssi));

    // -----

    uploadbuf.add(0x17); // Dict end

    //IPAddress addr = IPAddress();
    //addr.fromString("10.0.0.1");
    //Udp.beginPacket(addr, multicast_port);
    Udp.beginPacketMulticast(target, multicast_port, getIP6Address(), multicast_ttl);
    Udp.write(uploadbuf.buf, uploadbuf.ptr);
    Udp.endPacket();
}


int sequence = -1;
Buffer sendbuf;
uint8_t __sendbuf[databuf_LEN];
void sendPacket() {
    if (not sendbuf.size) {
        sendbuf.setBuffer(__sendbuf, databuf_LEN);
    }

    sendbuf.reset();

    sendbuf.add('9');
    sendbuf.add(sequence + 98);

    switch (sequence) {
        case -1: // 'a', boot packet.
            sendbuf.add(":reset=");
            #ifdef ESP8266
            sendbuf.add(ESP.getResetReason());
            #endif
            break;
        case 0:
            // TODO: implement (M)ode flag, and submit patches upstream.
            sendbuf.add("Z2"); // Non-standard. GW.
            //break;
        case 1: // 'c'
            //break;
        default:
            if (!isnan(latitude) and !isnan(longitude)) {
                sendbuf.add('L'); // TODO: Add Wifi based location.
                sendbuf.add(String(latitude, 5));
                sendbuf.add(',');
                sendbuf.add(String(longitude, 5));
            }
            break;
    }

    sendbuf.add('[');
    sendbuf.add(node_id);
    sendbuf.add(']');

    // Copy buffer to databuf for upload.
    for (uint16_t i=0; i < sendbuf.ptr; i++) {
        databuf[i] = sendbuf.buf[i];
    }
    dataptr = sendbuf.ptr;
    Serial.print("dataptr=");
    Serial.println(dataptr, DEC);

    // Upload packet.
    lastrssi = 1;
    multicast(multicast_ip);
    upload();
    Serial.println(F("Own packet uploaded, transmitting..."));

    Serial.write(sendbuf.buf, sendbuf.ptr);
    Serial.println();

    // Increment sequence for next time.
    sequence = (sequence + 1) % 25;
    //* Sequence goes 'b'-'z', with 'a' transmitted as first packet after boot.
}

void loop() {
    if (getTimeSince(packet_timer) >= packet_interval) {
        sendPacket();
        packet_timer += packet_interval;

        Serial.println();
        /*
        Serial.println(String("cyw43 in=") + String(cyw43_stats[CYW43_STAT_PACKET_IN_COUNT])
          + String(" out=") + String(cyw43_stats[CYW43_STAT_PACKET_OUT_COUNT]));
        */
        Serial.print(STAT(etharp));
        Serial.print(STAT(ip));
        Serial.print(STAT(ip6));
        Serial.print(STAT(tcp));
        Serial.print(STAT(udp));
        Serial.print(STAT(icmp));
        Serial.print(STAT(icmp6));
        Serial.print(STAT(nd6));
        Serial.print(STAT(mld6));
    }

    http_server_update();

    /*
            if (isUkhasnetPacket(databuf, dataptr)) {
                Serial.write(databuf, dataptr);
                Serial.println();
                multicast(multicast_ip);
                upload();
            } else {
                multicast(multicast_ip_other);
                upload(true);
            }
    */
}

void wifiScanCallback(int found) {
    Serial.printf("%d network(s) found\r\n", found);
    Serial1.printf("%d network(s) found\r\n", found);
    if (found < 2) { // Mozilla Location Services wants at least 2 access points
        return;
    }

    init_global_buffers();

    uploadbuf.reset();
    uploadbuf.add(F("{\"considerIp\":false,\"fallbacks\":{\"lacf\":false,\"ipf\":false},\"wifiAccessPoints\":["));
    for (int i = 0; i < found; i++) {
        if (i != 0) {
            uploadbuf.add(",");
        }
        uploadbuf.add(F("{\"macAddress\":\""));

        uint8_t bssid[6] = {0,0,0,0,0,0};
        WiFi.BSSID(i, bssid);
        for (int j=0;j<6;j++) {
            if (j) {
                uploadbuf.add(':');
            }
            if (bssid[j] < 0x10) {
                uploadbuf.add('0');
            }
            uploadbuf.add(String(bssid[j], HEX));
        }

        uploadbuf.add(F("\",\"signalStrength\":"));
        uploadbuf.add(String(WiFi.RSSI(i)));
        uploadbuf.add(F(",\"channel\":"));
        uploadbuf.add(String(WiFi.channel(i)));
        uploadbuf.add(F("}"));
        Serial.printf("%d: %s, Ch:%d (%ddBm) %s\r\n", i + 1, WiFi.SSID(i), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
        Serial1.printf("%d: %s, Ch:%d (%ddBm) %s\r\n", i + 1, WiFi.SSID(i), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
        //WiFi.BSSIDstr(i)
    }
    uploadbuf.add("]}");
    Serial.write(uploadbuf.buf, uploadbuf.ptr);
    Serial.println();
    Serial1.write(uploadbuf.buf, uploadbuf.ptr);
    Serial1.println();

    /*
    WiFiClientSecure client; // , String("E5:FC:B7:1A:DD:08:DF:B0:E7:D8:7A:7C:62:92:E1:07:EF:26:96:C7")
    client.setInsecure(); //! TODO: Verify SSL key.

    HTTPClient geoclient;
    int res = geoclient.begin(client, String("https://location.services.mozilla.com/v1/geolocate?key=test"));
    Serial.println(res);
    res = geoclient.POST(uploadbuf.buf, uploadbuf.ptr);
    Serial.println(res);
    Serial1.println(res);
    String response = geoclient.getString();
    Serial.println(response);
    Serial1.println(response);
    geoclient.end();


    double lat = NAN;
    double lon = NAN;
    // {"location": {"lat": 48.4167949, "lng": 18.9420294}, "accuracy": 10.0}
    int pos, pos2, pos3, start, end;
    if (res == HTTP_CODE_OK) {
        pos = response.indexOf("location");
        if (pos >= 0) {
            start = response.indexOf('{', pos);
            end = response.indexOf('}', start);
            if (start > 0 and end > 0) {
                // Parse lat
                pos = response.indexOf("lat", start);
                if (pos > end) {
                    return;
                }
                pos = response.indexOf(':', pos) + 1;
                pos2 = response.indexOf(',', pos);
                pos3 = response.indexOf('}', pos);
                pos2 = min(pos2, pos3);
                String lat_s = response.substring(pos, pos2);
                lat_s.trim();
                Serial1.print("lat_s:");
                Serial1.println(lat_s);
                if (lat_s.charAt(0) == '-' or isdigit(lat_s.charAt(0))) {
                    lat = lat_s.toFloat();
                }

                // Parse lon
                pos = response.indexOf("lng", start);
                if (pos > end) {
                    return;
                }
                pos = response.indexOf(':', pos) + 1;
                pos2 = response.indexOf(',', pos);
                pos3 = response.indexOf('}', pos);
                pos2 = min(pos2, pos3);
                String lon_s = response.substring(pos, pos2);
                lon_s.trim();
                Serial1.print("lon_s:");
                Serial1.println(lon_s);
                if (lon_s.charAt(0) == '-' or isdigit(lon_s.charAt(0))) {
                    lon = lon_s.toFloat();
                }


            }
        }
    }
    if (lat != NAN and lon != NAN) {
        Serial.println("Updating coordinates");
        Serial1.println("Updating coordinates");
        latitude = lat;
        longitude = lon;
    }
    */
}
