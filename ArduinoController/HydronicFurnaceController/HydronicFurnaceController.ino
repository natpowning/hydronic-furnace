/**
 * A bunch of initial code to get temperatures came from https://learn.adafruit.com/thermistor/using-a-thermistor.
 * Thank you lady ada, Tony DiCola and Adafruit - hope that's ok!
 * 
 * Hydronic heating system controller with single zone using a Bosch US3 tankless waterheater as the heat source.
 * The waterheater has two coils that are originally controlled by a water-flow based switch which has been replaced
 * with two T92P11D22-12 relays.  A Seeed Studio 4-Relay Shield controls these coils on pins 6 and 7 as well as the
 * hydronic loop pump directly on pin 5.
 * 
 */

#include <SPI.h>
#include <Ethernet.h>

// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 5
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
// the value of the 'other' resistor
#define SERIESRESISTOR 10000

#define COIL_THERMO_PIN    8  // 10ktherm & 10k resistor as divider.
#define ZONE1_DEMAND_PIN  46  // Digital from thermostat

#define HYDPUMP_PIN        5  // Hydronic Loop Circulation Pump Control
#define COIL1_PIN          6  // Electric Heat Coil #1 Control
#define COIL2_PIN          7  // Electric Heat Coil #2 Control (disabled)

#define HYDPUMP_TEMP_LOW 120.0    // Pump will keep running while above this

#define COIL1_TEMP_LOW   140.0    // Electric heat coil 1 on
#define COIL1_TEMP_HIGH  165.0    // Electric heat coil 1 off

#define COIL2_TEMP_LOW   150.0    // Electric heat Coil 2 on
#define COIL2_TEMP_HIGH  170.0    // Electric heat Coil 2 off

#define COOLDOWN_PERIOD  20000   // Keep pump running 1 minute after demand stops

int iteration = 0;

float fahrenheit;

bool electricHeatEnable = 1;

// BEGIN get MAC address from Microchip 24AA125E48 I2C ROM
#define I2C_ADDRESS 0x50
#include <Wire.h>

static uint8_t mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// END get MAC address from Microchip 24AA125E48 I2C ROM


EthernetServer server(80);

// Prometheus Scrape Endpoint
EthernetServer promServer(9100);

void setup() {
  Serial.begin(19200);
  
  Serial.println();
  Serial.println("Initializing HydronicFurnace for Mega2650 based Freetronics EtherMega.");
  Serial.println("Version: 2");
  Serial.println();
  
  delay( 50 );
  
  Wire.begin();
  mac[0] = readRegister(0xFA);
  mac[1] = readRegister(0xFB);
  mac[2] = readRegister(0xFC);
  mac[3] = readRegister(0xFD);
  mac[4] = readRegister(0xFE);
  mac[5] = readRegister(0xFF);
  
  Ethernet.begin(mac);
  
  Serial.print("Ethernet MAC: ");
  byte macBuffer[6];  // create a buffer to hold the MAC address
  Ethernet.MACAddress(macBuffer); // fill the buffer
  for (byte octet = 0; octet < 6; octet++) {
    Serial.print(macBuffer[octet], HEX);
    if (octet < 5) {
      Serial.print('-');
    }
  }
  Serial.println();
  
  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());
  Serial.println();
  
  pinMode(HYDPUMP_PIN , OUTPUT);
  pinMode(COIL1_PIN   , OUTPUT);
  pinMode(COIL2_PIN   , OUTPUT);
  
  pinMode(COIL_THERMO_PIN  , INPUT);
  pinMode(ZONE1_DEMAND_PIN , INPUT);
}

void loop() {
  fahrenheit = getTemperature(COIL_THERMO_PIN);
  
  stats();
  
  if(digitalRead(COIL1_PIN) == 1 || digitalRead(COIL2_PIN) == 1 || fahrenheit >= HYDPUMP_TEMP_LOW) {
    hydronicPump(1);
  } else {
    hydronicPump(0);
  }

  if(digitalRead(ZONE1_DEMAND_PIN) == 1) {
    if(electricHeatEnable) { 
      electricHeat(1);
    }
  } else {
    electricHeat(0);
  }

  stats();

  delay(500);

  iteration++;
}

void hydronicPump(boolean toggle) {
  if(toggle) {
    digitalWrite(HYDPUMP_PIN, 1);
  } else {
	  // Do not shut off pump if any non-passive heating sources are active
    if(digitalRead(HYDPUMP_PIN) == 1 && digitalRead(COIL1_PIN) == 0 && digitalRead(COIL2_PIN) == 0) {
      delay(COOLDOWN_PERIOD);

      digitalWrite(HYDPUMP_PIN, 0);
    }
  }
}


void electricHeat(boolean toggle) {
  if( ! toggle) {
    digitalWrite(COIL1_PIN,0);
    digitalWrite(COIL2_PIN,0);

    return;
  }

  int statusCoil1 = digitalRead(COIL1_PIN);
  int statusCoil2 = digitalRead(COIL2_PIN);

  if(fahrenheit >= COIL1_TEMP_HIGH) {
    statusCoil1 = 0;
  }
  if(fahrenheit >= COIL2_TEMP_HIGH) {
    statusCoil2 = 0;
  }

  if(fahrenheit <= COIL1_TEMP_LOW) {
    statusCoil1 = 1;
  }

  if(fahrenheit <= COIL2_TEMP_LOW) {
    statusCoil2 = 1;
  }

  if(statusCoil1 == 1 || statusCoil2 == 1) {
    hydronicPump(1);
  }

  digitalWrite(COIL1_PIN,statusCoil1);
  digitalWrite(COIL2_PIN,statusCoil2);
}

void stats() {
  String statsJSON = getStatsJSON();
  
  Serial.println(statsJSON);
  
  EthernetClient client = server.available();
  if(client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println();
    client.println(statsJSON);
    
    client.stop();
  }

  // Prometheus scrape endpoint
  EthernetClient promClient = promServer.available();
  if(promClient) {
    Serial.println("prom client");

    promClient.println("HTTP/1.1 200 OK");
    promClient.println("Content-Type: text/plain");
    promClient.println();

    promClient.print("# TYPE node_uname_info gauge\n");
    promClient.print("node_uname_info{nodename=\"furnace\",sysname=\"Arduino\"} 1\n");

    promClient.print("# TYPE node_furnace_coil_state gauge\n");
    promClient.print("node_furnace_coil_state{nodename=\"furnace\", coil=\"1\"} ");
    promClient.print(digitalRead(COIL1_PIN));
    promClient.print("\n");
    promClient.print("node_furnace_coil_state{nodename=\"furnace\", coil=\"2\"} ");
    promClient.print(digitalRead(COIL2_PIN));
    promClient.print("\n");

    promClient.print("# TYPE node_furnace_demand gauge\n");
    promClient.print("node_furnace_demand{nodename=\"furnace\", zone=\"1\"} ");
    promClient.print(digitalRead(ZONE1_DEMAND_PIN));
    promClient.print("\n");

    promClient.print("# TYPE node_furnace_coil_temp gauge");
    promClient.print("\n");
    promClient.print("node_furnace_coil_temp{nodename=\"furnace\"} ");
    promClient.print(fahrenheit);
    promClient.print("\n");

    promClient.print("# TYPE node_furnace_hydronic_pump gauge");
    promClient.print("\n");
    promClient.print("node_furnace_hydronic_pump{nodename=\"furnace\"} ");
    promClient.print(digitalRead(HYDPUMP_PIN));
    promClient.print("\n");

    promClient.stop();
  }
}


String getStatsJSON() {  
  String statsJSON = "{";

  statsJSON += "\"iteration\":";
  statsJSON += iteration;

  statsJSON += ",\"electric_coil_temp\":";
  statsJSON += fahrenheit;

  statsJSON += ",\"zone1_demand_status\":";
  statsJSON += digitalRead(ZONE1_DEMAND_PIN);

  statsJSON += ",\"hydronic_pump_status\":";
  statsJSON += digitalRead(HYDPUMP_PIN);

  statsJSON += ",\"electric_coil1_status\":";
  statsJSON += digitalRead(COIL1_PIN);

  statsJSON += ",\"electric_coil2_status\":";
  statsJSON += digitalRead(COIL2_PIN);

  statsJSON += "}";
 
  return(statsJSON);
}


float getTemperature(int pin) {
  uint8_t i;
  float average;
  int samples[NUMSAMPLES];

  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = analogRead(pin);
   delay(10);
  }

  // average all the samples out
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  average /= NUMSAMPLES;

  // convert the value to resistance
  average = 1023 / average - 1;
  average = SERIESRESISTOR / average;

  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C

  return celsius2fahrenheit(steinhart);
}

float celsius2fahrenheit(float celsius) {
  return celsius * 9/5 + 32;
}

byte readRegister(byte r)
{
  unsigned char v;
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(r);  // Register to read
  Wire.endTransmission();

  Wire.requestFrom(I2C_ADDRESS, 1); // Read a byte
  while(!Wire.available())
  {
    // Wait
  }
  v = Wire.read();
  return v;
}
