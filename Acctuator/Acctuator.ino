/*
  Aries Micro - Node 4 (Actuation Node)
  Features: Hardware AutoACK, Concurrent non-blocking pump control, Serial Countdowns
  Relay Type: ACTIVE LOW (LOW = ON, HIGH = OFF)
*/
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// VEGA Core Explicit Linker Override for SPI 1
SPIClass SPI(1); 

// --- Pin Definitions ---
#define NRF_CE    9
#define NRF_CSN   10

#define RELAY_N   5   // Nitrogen Pump
#define RELAY_P   3   // Phosphorus Pump
#define RELAY_K   14  // Potassium Pump
#define RELAY_W   16  // Water Pump

RF24 radio(NRF_CE, NRF_CSN);
const byte rxAddress[6] = "ACT01"; 

// Command Structure (Must match Gateway exactly)
typedef struct __attribute__((packed)) {
    uint16_t n_sec;
    uint16_t p_sec;
    uint16_t k_sec;
    uint16_t w_sec;
} PumpCommand;

// State Tracking
uint32_t end_N = 0, end_P = 0, end_K = 0, end_W = 0;
bool active_N = false, active_P = false, active_K = false, active_W = false;
uint32_t last_print_time = 0;

void setup() {
    Serial.begin(115200);
    delay(1000); // Give Serial and MCU time to stabilize
    
    Serial.println("\n========================================");
    Serial.println("   SICHAI Actuator Node (Aries Micro)   ");
    Serial.println("========================================");
    
    // SAFETY FIX: Set HIGH (OFF) before configuring as OUTPUT to prevent boot-up splashing
    digitalWrite(RELAY_N, HIGH); digitalWrite(RELAY_P, HIGH);
    digitalWrite(RELAY_K, HIGH); digitalWrite(RELAY_W, HIGH);
    
    pinMode(RELAY_N, OUTPUT); pinMode(RELAY_P, OUTPUT);
    pinMode(RELAY_K, OUTPUT); pinMode(RELAY_W, OUTPUT);

    SPI.begin();
    
    if (!radio.begin()) {
        Serial.println("[CRITICAL ERROR] NRF24 Hardware NOT responding!");
    } else {
        if (radio.isChipConnected()) {
            Serial.println("[SYSTEM] NRF24 Hardware Check: PASSED");
        } else {
            Serial.println("[SYSTEM] NRF24 Hardware Check: FAILED (Check SPI wiring)");
        }

        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_1MBPS);
        radio.setChannel(108);
        radio.setPayloadSize(sizeof(PumpCommand)); 
        
        radio.openReadingPipe(1, rxAddress);
        radio.startListening();
        
        Serial.println("[SYSTEM] Radio configured. Listening for Pump Commands on ACT01...");
    }
}

void loop() {
    uint32_t current_time = millis();

    // 1. Listen for Commands
    if (radio.available()) {
        PumpCommand cmd;
        radio.read(&cmd, sizeof(PumpCommand)); // Hardware AutoACK is sent instantly upon read()
        
        Serial.println("\n[COMMAND RECEIVED] Starting Dosing Sequence...");
        
        // Start Actuation & Calculate End Times (ACTIVE LOW: LOW = ON)
        if (cmd.n_sec > 0) { 
            end_N = current_time + (cmd.n_sec * 1000UL); 
            digitalWrite(RELAY_N, LOW); 
            active_N = true; 
            Serial.println("[ACTUATION] Nitrogen relay ON");
        }
        if (cmd.p_sec > 0) { 
            end_P = current_time + (cmd.p_sec * 1000UL); 
            digitalWrite(RELAY_P, LOW); 
            active_P = true; 
            Serial.println("[ACTUATION] Phosphorus relay ON");
        }
        if (cmd.k_sec > 0) { 
            end_K = current_time + (cmd.k_sec * 1000UL); 
            digitalWrite(RELAY_K, LOW); 
            active_K = true; 
            Serial.println("[ACTUATION] Potassium relay ON");
        }
        if (cmd.w_sec > 0) { 
            end_W = current_time + (cmd.w_sec * 1000UL); 
            digitalWrite(RELAY_W, LOW); 
            active_W = true; 
            Serial.println("[ACTUATION] Water relay ON");
        }
    }

    // 2. Continuous Cutoff Check (Non-Blocking)
    // ACTIVE LOW: HIGH = OFF
    if (active_N && current_time >= end_N) { 
        digitalWrite(RELAY_N, HIGH); 
        active_N = false; 
        Serial.println("[ACTUATION] Nitrogen relay [OFF]"); 
    }
    if (active_P && current_time >= end_P) { 
        digitalWrite(RELAY_P, HIGH); 
        active_P = false; 
        Serial.println("[ACTUATION] Phosphorus relay [OFF]"); 
    }
    if (active_K && current_time >= end_K) { 
        digitalWrite(RELAY_K, HIGH); 
        active_K = false; 
        Serial.println("[ACTUATION] Potassium relay [OFF]"); 
    }
    if (active_W && current_time >= end_W) { 
        digitalWrite(RELAY_W, HIGH); 
        active_W = false; 
        Serial.println("[ACTUATION] Water relay [OFF]"); 
    }

    // 3. Clean Countdown Print (Executes once per second while any pump is running)
    if ((active_N || active_P || active_K || active_W) && (current_time - last_print_time >= 1000)) {
        last_print_time = current_time;
        Serial.print("Countdown -> ");
        if (active_N) { Serial.print("N: "); Serial.print((end_N - current_time)/1000.0, 1); Serial.print("s | "); }
        if (active_P) { Serial.print("P: "); Serial.print((end_P - current_time)/1000.0, 1); Serial.print("s | "); }
        if (active_K) { Serial.print("K: "); Serial.print((end_K - current_time)/1000.0, 1); Serial.print("s | "); }
        if (active_W) { Serial.print("W: "); Serial.print((end_W - current_time)/1000.0, 1); Serial.print("s"); }
        Serial.println();
    }
}