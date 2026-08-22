#include <SPI.h>
#include <RF24.h>
#include <Arduino_RouterBridge.h>

#define CE_PIN 9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);
const byte rxAddress[6] = "RNG01";  
const byte txActAddress[6] = "ACT01"; 

typedef struct __attribute__((packed)) {
    uint16_t device_id; uint32_t timestamp; unsigned char temp;
    unsigned char hum; unsigned short sun_pct; unsigned short moist_pct;
    unsigned short n_val; unsigned short p_val; unsigned short k_val;
    unsigned char pump_state; unsigned char health_score;
    unsigned char rec_id; unsigned short pred_mins;
} AgriData;

// Modified struct to match the Actuator
typedef struct __attribute__((packed)) {
    uint16_t n_sec;
    uint16_t p_sec;
    uint16_t k_sec;
    uint16_t w_sec;
} PumpCommand;

AgriData rxPayload;

int latest_n = 0; int latest_p = 0; int latest_k = 0;
int latest_moist = 0; int latest_temp = 0; int latest_hum = 0;

String get_node1_data() {
    return String(latest_n) + "," + String(latest_p) + "," + 
           String(latest_k) + "," + String(latest_moist) + "," + 
           String(latest_temp) + "," + String(latest_hum);
}

// Format expected from Python: "N_SEC,P_SEC,K_SEC,W_SEC"
String trigger_dosing(String args) {
    // Hardware safety check (Silently return error to Python)
    if (!radio.isChipConnected()) {
        return "ERR_NRF24_DISCONNECTED";
    }

    PumpCommand cmd;
    
    // Parse the comma-separated values
    int idx1 = args.indexOf(',');
    int idx2 = args.indexOf(',', idx1 + 1);
    int idx3 = args.indexOf(',', idx2 + 1);
    
    cmd.n_sec = args.substring(0, idx1).toInt();
    cmd.p_sec = args.substring(idx1 + 1, idx2).toInt();
    cmd.k_sec = args.substring(idx2 + 1, idx3).toInt();
    cmd.w_sec = args.substring(idx3 + 1).toInt();
    
    // 1. Switch to Transmit Mode
    radio.stopListening();
    radio.setPayloadSize(sizeof(PumpCommand)); // Match Actuator payload size
    radio.openWritingPipe(txActAddress);
    
    // Hardware AutoACK validates delivery automatically
    bool ok = radio.write(&cmd, sizeof(PumpCommand)); 
    
    // 2. INSTANTLY REVERT TO LISTENING TO NODE 1
    radio.setPayloadSize(sizeof(AgriData));    // CRITICAL: Revert to Sensor payload size
    radio.openReadingPipe(1, rxAddress);       // CRITICAL: Re-open Sensor pipe
    radio.startListening(); 
    
    // Return to Python
    if (ok) {
        return "OK,3300,1"; // OK, mock voltage, mock packet count
    } else {
        return "FAIL";
    }
}

void setup() {
    // Initialize the Bridge for Python communication
    Bridge.begin();
    Bridge.provide("get_node1_data", get_node1_data);
    Bridge.provide("trigger_dosing", trigger_dosing);

    // Initialize Radio
    if (radio.begin()) {
        // Exact settings from your working code
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_1MBPS);
        radio.setChannel(108);                  
        radio.setRetries(15, 15);               
        radio.setPayloadSize(sizeof(AgriData)); 
        
        radio.openReadingPipe(1, rxAddress);
        radio.startListening();
        
        radio.flush_rx(); 
        radio.flush_tx(); 
    }
}

void loop() {
    // Zero-delay Smart Polling (Runs silently)
    if (radio.available()) {
        radio.read(&rxPayload, sizeof(AgriData));
        latest_n = rxPayload.n_val;
        latest_p = rxPayload.p_val;
        latest_k = rxPayload.k_val;
        latest_moist = rxPayload.moist_pct;
        latest_temp = rxPayload.temp;
        latest_hum = rxPayload.hum;
    }
    delay(10);
}