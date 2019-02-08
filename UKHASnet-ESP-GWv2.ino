#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <AddrList.h>

#include <Ticker.h>

// 
#include "radio_config.h"
#include <UKHASnetRFM69.h>

#include <Buffer.h>
#include <iso646.h>

#include "node_config.h"

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
    // Set up UKHASnet
    Serial.println("Reseting RFM69....");
    rfm_reset();
    Serial.println("Initiating RFM69....");
    rfm_status = rf69_init(rfmCSPin);
    Serial.print("RFM69 status: ");
    Serial.println(rfm_status);
    delay(200);
    ////rf69_burst_write(RFM69_REG_07_FRF_MSB, (rfm_reg_t*)"\xD9\x60\x12", 3);
    dump_rfm69_registers();

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
    return IP6_ADDR_ANY;
}

WiFiUDP Udp;
IPAddress multicast_ip;

unsigned long packet_timer;
unsigned long PACKET_INTERVAL = 15*60*1000;
void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    // Set up WiFi
    WiFi.mode(WIFI_STA); // Disable access point.
    WiFi.softAPdisconnect(true);
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable sleep (In case we want broadcast(multicast) data)

    // Set hostname: UKHASnet-ESP-{node_id}-{chip_id}
    WiFi.hostname(String("UKHASnet-ESP-") + node_id + "-" + String(ESP.getChipId(), HEX));
    Serial.print("Hostname: ");
    Serial.println(WiFi.hostname());

    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);

    // Wait for IP address
    for (bool configured = false; !configured; ) {
        for (auto iface: addrList) {
            if (configured = !iface.addr().isLocal()) {
                break;
            }
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    // Print IP configuration..
    for (auto a : addrList) {
        Serial.print(a.ifnumber());
        Serial.print(':');
        Serial.print(a.ifname());
        Serial.print(": ");
        Serial.print(a.isV4() ? "IPv4" : "IPv6");
        Serial.print(a.isLocal() ? " local " : " ");
        Serial.print(a.toString());
        if (a.isLegacy()) {
            Serial.print(" mask:");
            Serial.print(a.netmask());
            Serial.print(" gw:");
            Serial.print(a.gw());
        }
        Serial.println();
    }

    // Setup UKHASnet
    resetRadio();
    // Should make it transmit a packet right now, then next after PACKET_INTERVAL
    packet_timer = millis() - PACKET_INTERVAL;

    multicast_ip.fromString(multicast_address);
}


bool packet_received = false;
#define databuf_LEN 1032
uint8_t databuf[databuf_LEN];
uint16_t dataptr = 0;
float lastrssi = 0;

WiFiClientSecure client;

#define uploadbuf_LEN 1500
Buffer uploadbuf;
uint8_t __uploadbuf[uploadbuf_LEN];
HTTPClient http;

void upload(bool fake=false);
void upload(bool fake) {
    yield();

    if (not uploadbuf.size) {
        uploadbuf.setBuffer(__uploadbuf, uploadbuf_LEN);
    }
    if (not fake) {
        Serial.println("Uploading...");
        http.begin(api_url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
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
        http.POST((const char*)uploadbuf.buf);

        http.writeToStream(&Serial);
        http.end();
    }

    Serial.println();
}

void multicast() {
    yield();
    Serial.print("Sending multicast packet to ");
    Serial.print(multicast_ip.toString());
    Serial.print(" port ");
    Serial.print(multicast_port, DEC);
    Serial.println("...");

    if (not uploadbuf.size) {
        uploadbuf.setBuffer(__uploadbuf, uploadbuf_LEN);
    }

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

    uploadbuf.add((char)(0x80 | sizeof(node_id)));
    uploadbuf.add(node_id);

    // -----

    uploadbuf.add((char)(0x80 | 4));
    uploadbuf.add("rssi");

    uploadbuf.add((char)0x08); // int8_t
    uploadbuf.add(static_cast<int8_t>(lastrssi));

    // -----

    uploadbuf.add(0x17); // Dict end

    Udp.beginPacketMulticast(multicast_ip, multicast_port, getIP6Address(), multicast_ttl);
    Udp.write(uploadbuf.buf, uploadbuf.ptr);
    Udp.endPacket();
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
            sendbuf.add(ESP.getResetReason());
            break;
        case 0:
            // TODO: implement (M)ode flag, and submit patches upstream.
            sendbuf.add("Z2"); // Non-standard. GW. 
            break;
        case 1: // 'c'
            //sendbuf.add('L'); // TODO: Add Wifi based location.
            break;
        default:
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
    upload();
    Serial.println("Own packet uploaded, transmitting...");

    Serial.write(sendbuf.buf, sendbuf.ptr);
    Serial.println();

    // Transmit packet. 10dBm (10mW)
    rf69_send_long(sendbuf.buf, sendbuf.ptr, 10, DIO1_pin);

    // Increment sequence for next time.
    sequence = (sequence + 1) % 25;
    //* Sequence goes 'b'-'z', with 'a' transmitted as first packet after boot. 
}

rfm_status_t res;
void loop() {
    if (getTimeSince(packet_timer) >= PACKET_INTERVAL) {
        sendPacket();
        packet_timer += PACKET_INTERVAL;
    }

    // TODO: Add timeout parameter to rf69_receive_long, to allow other tasks to run (outside of yield())
    res = rf69_receive_long(databuf, &dataptr, &lastrssi, &packet_received, databuf_LEN, DIO1_pin);

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
            multicast();
            upload();
        } else {
            upload(true);
        }

    }
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
