/*
  Aries Micro v1.0 - Node 1 (Primary Sensor Node)
  Features: WDT-Safe Sleep, Anti-Brownout, Radio Amnesia Protection
*/

#include <Wire.h>          
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DFRobot_DHT11.h> 
#include <Timer.h>          

// --- VEGA Core Explicit Linker Overrides ---
TwoWire Wire(1); 
SPIClass SPI(1); 
Timer wakeTimer(1);         
HardwareSerial Serial1(1);

// --- Pin Definitions ---
#define SENSOR_EN_PIN 2     
#define MOIST_PIN    A0
#define LDR_PIN      A2
#define BATT_PIN     A1
#define DHT_PIN      11  
#define NRF_CE       9
#define NRF_CSN      10

// --- Global State ---
volatile bool sleepTimerFired = false; 

// --- Objects ---
DFRobot_DHT11 DHT;       
RF24 radio(NRF_CE, NRF_CSN);

// --- Data Structure ---
typedef struct __attribute__((packed)) {
    uint16_t device_id; uint32_t timestamp; unsigned char temp;
    unsigned char hum; unsigned short sun_pct; unsigned short moist_pct;
    unsigned short n_val; unsigned short p_val; unsigned short k_val;
    unsigned char pump_state; unsigned char health_score;
    unsigned char rec_id; unsigned short pred_mins;
} AgriData;

AgriData payload;
const byte npkQuery[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x03, 0x65, 0xCD};
byte npkResponse[11];
const byte address[6] = "RNG01"; 

// ==========================================
// RADIO CONFIGURATION
// ==========================================
void configureRadio() {
  SPI.begin(); 
  
  if (!radio.begin()) {
    Serial.println("Warning: Radio not responding during wake-up.");
  }

  radio.setPALevel(RF24_PA_MAX);    
  radio.setDataRate(RF24_1MBPS); 
  radio.setChannel(108);         
  radio.setRetries(15, 15);         
  radio.setPayloadSize(sizeof(AgriData)); 
  radio.openWritingPipe(address);
  radio.stopListening();
}

// ==========================================
// POWER MANAGEMENT
// ==========================================
void wakeUpSensors() {
  Serial.println("Powering sensors ON (Soft-Start)...");
  
  analogWrite(SENSOR_EN_PIN, 50);  
  delay(15);
  analogWrite(SENSOR_EN_PIN, 128); 
  delay(15);
  digitalWrite(SENSOR_EN_PIN, HIGH); 
  
  delay(2000); // Stabilization time
  pinMode(LDR_PIN, INPUT); 
  pinMode(MOIST_PIN, INPUT);
}

void powerDownSensors() {
  Serial.println("Powering sensors OFF...");
  radio.powerDown(); 
  pinMode(LDR_PIN, INPUT);
  pinMode(MOIST_PIN, INPUT);
  pinMode(DHT_PIN, INPUT);
  digitalWrite(SENSOR_EN_PIN, LOW); 
}

// ==========================================
// SETUP 
// ==========================================
void setup() {
  Serial.begin(115200);   
  delay(100);
  Serial.println("\n[DEBUG] 1. Serial Started! Entering Setup...");
  
  Serial1.begin(4800);    
  pinMode(SENSOR_EN_PIN, OUTPUT);
  digitalWrite(SENSOR_EN_PIN, LOW);
  
  Wire.begin();           
  wakeUpSensors();
  configureRadio();
  
  payload.device_id = 101; 
  payload.rec_id = 1;
  Serial.println("\n--- Node 1 Initialized ---");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  wakeUpSensors();
  configureRadio();
  
  radio.powerUp();
  delay(10); 

  Serial.println("\n--- Gathering Node 1 Data ---");
  payload.timestamp = millis() / 1000; 

  DHT.read(DHT_PIN);
  payload.temp = (unsigned char)DHT.temperature;
  payload.hum  = (unsigned char)DHT.humidity;

  payload.moist_pct = constrain(map(analogRead(MOIST_PIN), 1550, 550, 0, 100), 0, 100);
  payload.sun_pct = constrain(map(analogRead(LDR_PIN), 200, 1700, 100, 0), 0, 100);

  readNPKSensor();

  // Print nicely to monitor
  printSensorData(payload);

  Serial.print("Transmitting to Relay... ");
  if (radio.write(&payload, sizeof(AgriData))) {
    Serial.println("[SUCCESS] ACK from Relay.");
  } else {
    Serial.println("[FAILED] No ACK. Flushing TX...");
    radio.flush_tx(); 
  }

  enterDeepSleep();
}

// ==========================================
// UTILITY: PRINT DATA
// ==========================================
void printSensorData(AgriData &data) {
    Serial.println("\n=================================");
    Serial.println("   CURRENT SENSOR READINGS");
    Serial.println("=================================");
    Serial.print("🌡️ Temperature : "); Serial.print(data.temp); Serial.println(" °C");
    Serial.print("💧 Humidity    : "); Serial.print(data.hum); Serial.println(" %");
    Serial.print("🌱 Moisture    : "); Serial.print(data.moist_pct); Serial.println(" %");
    Serial.print("☀️ Sunlight    : "); Serial.print(data.sun_pct); Serial.println(" %");
    Serial.println("--- Soil Nutrients (NPK) ---");
    Serial.print("N (Nitrogen)  : "); Serial.print(data.n_val); Serial.println(" mg/kg");
    Serial.print("P (Phosphorus): "); Serial.print(data.p_val); Serial.println(" mg/kg");
    Serial.print("K (Potassium) : "); Serial.print(data.k_val); Serial.println(" mg/kg");
    Serial.println("=================================\n");
}

// ==========================================
// WDT-SAFE DEEP SLEEP
// ==========================================
void enterDeepSleep() {
  Serial.println("Entering Sleep for 1 Minute...");
  Serial.flush(); 
  powerDownSensors();

  wakeTimer.attachInterrupt(timerWakeISR, 10000000UL); // 10s (Loop 6 times for 1 min)

  for (int i = 0; i < 6; i++) {
    __asm__ volatile("csrc mie, %0" :: "r"(1 << 7)); 
    sleepTimerFired = false;
    while (!sleepTimerFired) {
      __asm__ volatile ("wfi"); 
    }
    __asm__ volatile("csrs mie, %0" :: "r"(1 << 7)); 
    delay(10); 
  }

  wakeTimer.detachInterrupt();
}

void timerWakeISR() {
  sleepTimerFired = true; 
}

// ==========================================
// NPK SENSOR READ
// ==========================================
void readNPKSensor() {
  while (Serial1.available()) Serial1.read();
  Serial1.write(npkQuery, sizeof(npkQuery));
  Serial1.flush(); 
  
  unsigned long startTime = millis();
  int byteCount = 0;
  
  while ((millis() - startTime < 200) && (byteCount < 11)) {
    if (Serial1.available()) {
      npkResponse[byteCount++] = Serial1.read();
    }
  }
  
  if (byteCount >= 9 && npkResponse[0] == 0x01) {
    payload.n_val = (uint16_t)((npkResponse[3] << 8) | npkResponse[4]);
    payload.p_val = (uint16_t)((npkResponse[5] << 8) | npkResponse[6]);
    payload.k_val = (uint16_t)((npkResponse[7] << 8) | npkResponse[8]);
  } else {
    payload.n_val = 0; payload.p_val = 0; payload.k_val = 0;
  }
}