#include <WiFi.h>
#include <WiFiUdp.h>
#include <AddrList.h>

//#include <HTTPClient.h>


//#include <Ticker.h>

#ifndef HTTP_CODE_OK
#define HTTP_CODE_OK 200
#endif
#ifndef STATION_IF
#define STATION_IF 0
#endif

//
#include "radio_config.h"
#include <UKHASnetRFM69.h>

#include <Buffer.h>
#include <iso646.h>

#include "node_config.h"
#include "global_buffers.h"

#include "http_server.h"

volatile bool transmitting_packet = false;

int rfmCSPin = 0;
int rfmResetPin = 15;

int ukh_seq = 0;

rfm_status_t rfm_status;

void rfm_reset() {
    pinMode(rfmResetPin, OUTPUT);
    digitalWrite(rfmResetPin, HIGH);
    delay(5);
    digitalWrite(rfmResetPin, LOW);
    delay(20);
}

rfm_status_t resetRadio() {
    pinMode(DIO1_pin, INPUT_PULLDOWN);

    // Set up UKHASnet
    Serial.println("Reseting RFM69....");
    rfm_reset();
    Serial.println("Initiating RFM69....");
    rfm_status = rf69_init(rfmCSPin);
    Serial.print("RFM69 status: ");
    Serial.println(rfm_status);

    if (rfm_status == RFM_OK) {
        delay(200);
        ////rf69_burst_write(RFM69_REG_07_FRF_MSB, (rfm_reg_t*)"\xD9\x60\x12", 3);
        dump_rfm69_registers();
    }

    return rfm_status;
}

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
    #else
    int networks_found = WiFi.scanNetworks();
    #endif

    // Set hostname: UKHASnet-ESP-{node_id}-{chip_id}
    //uint8_t ___foo[WL_MAC_ADDR_LENGTH] = {0};
    WiFi.setHostname((String("UKHASnet-ESP-") + String(node_id)).c_str() /*+ "-" + String(WiFi.macAddress(*___foo) & 0xffffff, HEX)*/);
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

    // Setup UKHASnet
    rfm_status = resetRadio();
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

    //Udp.beginPacketMulticast(target, multicast_port, getIP6Address(), multicast_ttl);
    //Udp.write(uploadbuf.buf, uploadbuf.ptr);
    //Udp.endPacket();
}

/**
 * Checks if buf matches minimum requirements for UKHASnet packet.
 * @param buf A pointer to buffer to test content of
 * @param len Length of packet in buffer
 * @returns true if matching, false if not.
 */
bool isUkhasnetPacket(uint8_t *buf, uint16_t len) {
    //! Minimum packet to pass: 0a[Q]
    //* Path can contain any character and be any length.
    //* Data field validity (or existence) not tested.
    // Minimum length
    if (len < 5) { // 0a[Q]
        return false;
    }

    // TTL:
    if (buf[0] < '0' or buf[0] > '9') {
        return false;
    }

    // Sequence:
    if (buf[1] < 'a' or buf[1] > 'z') {
        return false;
    }

    /*
     Packet:    9aV3.3[Q]
     i:         012345678
     len:       123456789
    */
    // Hops
    if (buf[len-1] != ']') {
        return false;
    }
    // 9a V3.3[ Q]
    bool found = false;
    for (uint16_t i=len-2; i>1; i--) {
        if (buf[i] == '[') {
            found = true;
            break;
        }
    }
    if (not found) {
        return false;
    }

    return true;
}

bool _ratelimit_firstrun = true;
unsigned long _last_reset_radio = 0;
rfm_status_t resetRadioRatelimited() {
    if (_ratelimit_firstrun || rfm_status == RFM_OK || getTimeSince(_last_reset_radio) >= 15000) {
        resetRadio();
        _last_reset_radio = millis();
        _ratelimit_firstrun = false;
    }
    return rfm_status;
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
            if (latitude != NAN and longitude != NAN) {
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

    // Transmit packet. 10dBm (10mW)
    if (tx_enabled) {
        if (rfm_status == RFM_OK || resetRadio() == RFM_OK) {
            rf69_send_long(sendbuf.buf, sendbuf.ptr, 10, DIO1_pin);
        } else {
            SERIAL_PRINTLN("rfm_status is not RFM_OK, even after radio reset, skipping TX.");
        }
    }

    // Increment sequence for next time.
    sequence = (sequence + 1) % 25;
    //* Sequence goes 'b'-'z', with 'a' transmitted as first packet after boot.
}

rfm_status_t res;
void loop() {
    if (getTimeSince(packet_timer) >= packet_interval) {
        //sendPacket();
        packet_timer += packet_interval;
    }

    http_server_update();

    if (rfm_status == RFM_OK || resetRadioRatelimited() == RFM_OK) {
        // TODO: Add timeout parameter to rf69_receive_long, to allow other tasks to run (outside of yield())
        res = rf69_receive_long(databuf, &dataptr, &lastrssi, &packet_received, databuf_LEN, DIO1_pin, 100);

        if (res == RFM_OK) {
            Serial.print("Result: ");
            Serial.println(res, DEC);

            Serial.print("RSSI: ");
            Serial.println(lastrssi);

            Serial.print("Bytes: ");
            Serial.println(dataptr, DEC);

            Serial.print("Waiting: ");
            Serial.println(packet_received ? "true" : "false");

            if (isUkhasnetPacket(databuf, dataptr)) {
                Serial.write(databuf, dataptr);
                Serial.println();
                multicast(multicast_ip);
                upload();
            } else {
                multicast(multicast_ip_other);
                upload(true);
            }

        } else if (res == RFM_TIMEOUT) {
        } else {
            Serial.print(F("RFM RX Error: "));
        }

        switch (res) {
            case RFM_OK:
                Serial1.println(F("RFM_OK"));
                break;
            case RFM_FAIL:
                SERIAL_PRINTLN(F("RFM_FAIL"));
                break;
            case RFM_TIMEOUT:
                Serial1.println(F("RFM_TIMEOUT"));
                break;
            case RFM_CRC_ERROR:
                SERIAL_PRINTLN(F("RFM_CRC_ERROR"));
                break;
            case RFM_BUFFER_OVERFLOW:
                SERIAL_PRINTLN(F("RFM_BUFFER_OVERFLOW"));
                break;
            default:
                Serial1.print(F("Unknown RFM return code: "));
                Serial1.println(res);
                Serial.println(res);
        }
    } else {
        Serial1.println("rfm_status is not RFM_OK, even after radio reset, skipping RX.");
    }
/*
    uint32_t free;
    uint16_t max;
    uint8_t frag;
    ESP.getHeapStats(&free, &max, &frag);
    Serial.print(F("free: "));
    Serial.print(free);
    Serial.print(F(", max: "));
    Serial.print(max);
    Serial.print(F(", frag: "));
    Serial.print(frag);
    Serial.print(F(", stack: "));
    Serial.println(ESP.getFreeContStack());
    */
}

void dump_rfm69_registers() {
    rfm_reg_t result;

    rf69_read(RFM69_REG_01_OPMODE, &result);
    Serial.print(F("REG_01_OPMODE: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_02_DATA_MODUL, &result);
    Serial.print(F("REG_02_DATA_MODUL: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_03_BITRATE_MSB, &result);
    Serial.print(F("REG_03_BITRATE_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_04_BITRATE_LSB, &result);
    Serial.print(F("REG_04_BITRATE_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_05_FDEV_MSB, &result);
    Serial.print(F("REG_05_FDEV_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_06_FDEV_LSB, &result);
    Serial.print(F("REG_06_FDEV_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_07_FRF_MSB, &result);
    Serial.print(F("REG_07_FRF_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_08_FRF_MID, &result);
    Serial.print(F("REG_08_FRF_MID: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_09_FRF_LSB, &result);
    Serial.print(F("REG_09_FRF_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_0A_OSC1, &result);
    Serial.print(F("REG_0A_OSC1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_0B_AFC_CTRL, &result);
    Serial.print(F("REG_0B_AFC_CTRL: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_0D_LISTEN1, &result);
    Serial.print(F("REG_0D_LISTEN1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_0E_LISTEN2, &result);
    Serial.print(F("REG_0E_LISTEN2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_0F_LISTEN3, &result);
    Serial.print(F("REG_0F_LISTEN3: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_10_VERSION, &result);
    Serial.print(F("REG_10_VERSION: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_11_PA_LEVEL, &result);
    Serial.print(F("REG_11_PA_LEVEL: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_12_PA_RAMP, &result);
    Serial.print(F("REG_12_PA_RAMP: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_13_OCP, &result);
    Serial.print(F("REG_13_OCP: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_18_LNA, &result);
    Serial.print(F("REG_18_LNA: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_19_RX_BW, &result);
    Serial.print(F("REG_19_RX_BW: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1A_AFC_BW, &result);
    Serial.print(F("REG_1A_AFC_BW: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1B_OOK_PEAK, &result);
    Serial.print(F("REG_1B_OOK_PEAK: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1C_OOK_AVG, &result);
    Serial.print(F("REG_1C_OOK_AVG: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1D_OOF_FIX, &result);
    Serial.print(F("REG_1D_OOF_FIX: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1E_AFC_FEI, &result);
    Serial.print(F("REG_1E_AFC_FEI: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_1F_AFC_MSB, &result);
    Serial.print(F("REG_1F_AFC_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_20_AFC_LSB, &result);
    Serial.print(F("REG_20_AFC_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_21_FEI_MSB, &result);
    Serial.print(F("REG_21_FEI_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_22_FEI_LSB, &result);
    Serial.print(F("REG_22_FEI_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_23_RSSI_CONFIG, &result);
    Serial.print(F("REG_23_RSSI_CONFIG: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_24_RSSI_VALUE, &result);
    Serial.print(F("REG_24_RSSI_VALUE: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_25_DIO_MAPPING1, &result);
    Serial.print(F("REG_25_DIO_MAPPING1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_26_DIO_MAPPING2, &result);
    Serial.print(F("REG_26_DIO_MAPPING2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_27_IRQ_FLAGS1, &result);
    Serial.print(F("REG_27_IRQ_FLAGS1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_28_IRQ_FLAGS2, &result);
    Serial.print(F("REG_28_IRQ_FLAGS2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_29_RSSI_THRESHOLD, &result);
    Serial.print(F("REG_29_RSSI_THRESHOLD: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2A_RX_TIMEOUT1, &result);
    Serial.print(F("REG_2A_RX_TIMEOUT1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2B_RX_TIMEOUT2, &result);
    Serial.print(F("REG_2B_RX_TIMEOUT2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2C_PREAMBLE_MSB, &result);
    Serial.print(F("REG_2C_PREAMBLE_MSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2D_PREAMBLE_LSB, &result);
    Serial.print(F("REG_2D_PREAMBLE_LSB: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2E_SYNC_CONFIG, &result);
    Serial.print(F("REG_2E_SYNC_CONFIG: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_2F_SYNCVALUE1, &result);
    Serial.print(F("REG_2F_SYNCVALUE1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_30_SYNCVALUE2, &result);
    Serial.print(F("REG_30_SYNCVALUE2: 0x"));
    Serial.println(result, HEX);

    /* Sync values 1-8 go here */
    rf69_read(RFM69_REG_37_PACKET_CONFIG1, &result);
    Serial.print(F("REG_37_PACKET_CONFIG1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_38_PAYLOAD_LENGTH, &result);
    Serial.print(F("REG_38_PAYLOAD_LENGTH: 0x"));
    Serial.println(result, HEX);

    /* Node address, broadcast address go here */
    rf69_read(RFM69_REG_3B_AUTOMODES, &result);
    Serial.print(F("REG_3B_AUTOMODES: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_3C_FIFO_THRESHOLD, &result);
    Serial.print(F("REG_3C_FIFO_THRESHOLD: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_3D_PACKET_CONFIG2, &result);
    Serial.print(F("REG_3D_PACKET_CONFIG2: 0x"));
    Serial.println(result, HEX);

    /* AES Key 1-16 go here */
    rf69_read(RFM69_REG_4E_TEMP1, &result);
    Serial.print(F("REG_4E_TEMP1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_4F_TEMP2, &result);
    Serial.print(F("REG_4F_TEMP2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_58_TEST_LNA, &result);
    Serial.print(F("REG_58_TEST_LNA: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_5A_TEST_PA1, &result);
    Serial.print(F("REG_5A_TEST_PA1: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_5C_TEST_PA2, &result);
    Serial.print(F("REG_5C_TEST_PA2: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_6F_TEST_DAGC, &result);
    Serial.print(F("REG_6F_TEST_DAGC: 0x"));
    Serial.println(result, HEX);

    rf69_read(RFM69_REG_71_TEST_AFC, &result);
    Serial.print(F("REG_71_TEST_AFC: 0x"));
    Serial.println(result, HEX);
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
