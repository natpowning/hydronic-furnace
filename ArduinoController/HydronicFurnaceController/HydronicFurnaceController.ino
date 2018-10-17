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
#define HYDPUMP_PIN        5
#define COIL1_PIN          6
#define COIL2_PIN          7

#define HYDPUMP_TEMP_LOW 120.0    // Pump will keep running while above this

#define COIL1_TEMP_LOW   170.0    // Electric heat coil 1 on
#define COIL1_TEMP_HIGH  185.0    // Electric heat coil 1 off

#define COIL2_TEMP_LOW   160.0    // Electric heat Coil 2 on
#define COIL2_TEMP_HIGH  180.0    // Electric heat Coil 2 off

float fahrenheit;


void setup() {
  pinMode(HYDPUMP_PIN , OUTPUT);
  pinMode(COIL1_PIN   , OUTPUT);
  pinMode(COIL2_PIN   , OUTPUT);
  
  pinMode(COIL_THERMO_PIN  , INPUT);
  pinMode(ZONE1_DEMAND_PIN , INPUT);
  
  Serial.begin(19200);
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
    electricHeat(1);
  } else {
    electricHeat(0);
  }

  stats();

  delay(500);
}

void hydronicPump(boolean toggle) {
  if(toggle) {
    if(digitalRead(HYDPUMP_PIN) == 0) {
      event("HydronicPumpStatus=1");
      digitalWrite(HYDPUMP_PIN, 1);
    }
  } else {
	  // Do not shut off pump if any non-passive heating sources are active (electric/diesel boiler)
    if(digitalRead(HYDPUMP_PIN) == 1 && digitalRead(COIL1_PIN) == 0 && digitalRead(COIL2_PIN) == 0) {
      event("HydronicPumpStatus=0");
      digitalWrite(HYDPUMP_PIN, 0);
    }
  }
}

void electricHeat(boolean toggle) {
  int statusCoil1 = digitalRead(COIL1_PIN);
  int statusCoil2 = digitalRead(COIL2_PIN);

  if( ! toggle) {
    statusCoil1 = 0;
    statusCoil2 = 0;
  }

  if(fahrenheit >= COIL1_TEMP_HIGH) {
    statusCoil1 = 0;
  }
  if(fahrenheit >= COIL2_TEMP_HIGH) {
    statusCoil2 = 0;
  }

  if(fahrenheit < COIL1_TEMP_LOW && toggle) {
    statusCoil1 = 1;
  }
  
  if(fahrenheit < COIL2_TEMP_LOW && toggle) {
    statusCoil2 = 1;
  }

  if(statusCoil1 == 1 || statusCoil2 == 1) {
    hydronicPump(1);
    delay(1000);
  }

  if(statusCoil1 == 1) {
    if(digitalRead(COIL1_PIN) != 1) {
      event("ElectricCoil1Status=1");
      digitalWrite(COIL1_PIN,statusCoil1);
    }
  } else {
    if(digitalRead(COIL1_PIN) != 0) {
      event("ElectricCoil1Status=0");
      digitalWrite(COIL1_PIN,statusCoil1);
    }
  }

  if(statusCoil2 == 1) {
    if(digitalRead(COIL2_PIN) != 1) {
      event("ElectricCoil2Status=1");
      digitalWrite(COIL2_PIN,statusCoil2);
    }
  } else {
    if(digitalRead(COIL2_PIN) != 0) {
      event("ElectricCoil2Status=0");
      digitalWrite(COIL2_PIN,statusCoil2);
    }
  }
}

void event(char message[]) {
  Serial.print("[");

  Serial.print("{\"name\":\"Event\",\"");
  Serial.print(message);
  Serial.println("\"}]");
}

void stats() {
  Serial.print("[");
  
  Serial.print("{\"name\":\"ElectricCoilTemp\",\"value\":\"");
  Serial.print(fahrenheit);
  Serial.print("\"}");

  Serial.print(",{\"name\":\"Zone1DemandStatus\",\"value\":");
  Serial.print(digitalRead(ZONE1_DEMAND_PIN));
  Serial.print("}");

  Serial.print(",{\"name\":\"HydronicPumpStatus\",\"value\":");
  Serial.print(digitalRead(HYDPUMP_PIN));
  Serial.print("}");


  Serial.print(",{\"name\":\"ElectricCoil1Status\",\"value\":");
  Serial.print(digitalRead(COIL1_PIN));
  Serial.print("}");

  Serial.print(",{\"name\":\"ElectricCoil2Status\",\"value\":");
  Serial.print(digitalRead(COIL2_PIN));
  Serial.print("}");
  
  Serial.println("]");
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
