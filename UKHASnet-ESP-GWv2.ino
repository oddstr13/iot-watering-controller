#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

#include <Ticker.h>

// 
#include "radio_config.h"
#include "ukhasnet-rfm69/UKHASnetRFM69.h"

#include <Buffer.h>
#include <iso646.h>

const char* ssid = "SSID";
const char* password = "PASSWORD";

const char* node_id = "NODENAME";
const char* api_url = "http://www.ukhas.net/api/upload";

volatile bool transmitting_packet = false;
const int rfm_dio1 = 2;


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
    //rf69_burst_write(RFM69_REG_07_FRF_MSB, (rfm_reg_t*)"\xD9\x60\x12", 3);
    dump_rfm69_registers();

    return rfm_status;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    // Set up WiFi
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    // Setup UKHASnet
    resetRadio();
}


bool packet_received = false;
#define databuf_LEN 1032
uint8_t databuf[databuf_LEN];
uint8_t dataptr = 0;
int16_t lastrssi = 0;

WiFiClientSecure client;

#define uploadbuf_LEN 1500
Buffer uploadbuf;
uint8_t __uploadbuf[uploadbuf_LEN];
HTTPClient http;

void upload() {
    if (not uploadbuf.size) {
        uploadbuf.setBuffer(__uploadbuf, uploadbuf_LEN); \
        //BUFFER_INIT(uploadbuf, uploadbuf_LEN);
    }
    Serial.println("Uploading...");
    //http.begin(api_url);
    //http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    uploadbuf.reset();
    uploadbuf.addString("origin=");
    uploadbuf.addString(node_id); //gateway nodes ID
    uploadbuf.addString("&data=");

    // TODO: url escape.
    //uploadbuf.addCharArray((char*)databuf, dataptr-1);
    //for (int i = 0; i < len-1; i++){
    //    uploadPacket += char(buf[i]); //copy the packet from the buffer we got from rf69.recv into our upload string. There may be neater ways of doing this.
    //}
    char c;
    for (unsigned int i=0; i<dataptr-1; i++) {
        c = databuf[i];
        if (c == 45 or c == 46 or c == 95 or c == 126
            or (48 <= c and c <= 58)
            or (65 <= c and c <= 90)
            or (97 <= c and c <= 122)) {
            uploadbuf.addByte(c);
        } else {
            uploadbuf.addByte('%');
            uploadbuf.addByte(BASE16[(c & 0xf0) >> 4]);
            uploadbuf.addByte(BASE16[ c & 0x0f]);
        }
    }

    uploadbuf.addString("&rssi=");
    uploadbuf.addString(String(lastrssi));
    uploadbuf.addByte(0); //null terminate the string for safe keeping
    
    Serial.write(uploadbuf.buf, uploadbuf.ptr-1);
    Serial.println();

    //http.POST((const char*)uploadbuf.buf);

    //http.writeToStream(&Serial);
    //http.end();
    Serial.println();
}

void loop() {
    rf69_receive(databuf, &dataptr, &lastrssi, &packet_received);
    if (packet_received) {
        Serial.println((float)lastrssi);
        Serial.write(databuf, dataptr-1);
        Serial.println();
        upload();
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
