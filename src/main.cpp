#include <Arduino.h>
#include <U8g2lib.h>


// Konstanten des ESP-Verkabelung-Ultrasonic-Sensor JSP-SR04T-V33
const int echoPin = 5;
const int trigPin = 4;

// Konstanten des JSP-SR04T-Ultraschall-Sensors
const uint distance_deadzone = 22;  // Deadzone des Sensors (bei dem Ultraschallsensor von JSN-SR04T ists wohl 22cm)
const long max_TOF_sens = 30000; // grenze des Sensors (maximale Mess-Reichweite in Mikrosekunden)

// Konstanten der Installation in Essencia Wassertank Sensor. Angaben in Zentimeter
const uint distance_max_depth_watertank = 150; // maximaler, sinnvoller zu messender Wert zwischen Sensor und Tankboden (bzw.) der Wassertank-Tiefe (bis zum Sensor)
const uint height_watertank_0percent = 10;  // bei 10cm (in Essencia) fängt das Wasserrohr an, darunterliegende Wasserstände können nicht gepumpt werden.
const uint height_watertank_100percent = 120; // vom Boden, 140cm (in Essencia) ist der Wassertank mit 100% voll betitelt. da der Sensor jedoch 22cm Deadzone hat, ist das mal hier so früh auf 100% definiert. Man müsste den Sensor höher montieren (wie chatgpt/gemini schon gesagt hatte)
const uint distance_watertank_0percent = height_watertank_100percent-height_watertank_0percent; // 140-10 = 130cm
const uint distance_watertank_100percent = height_watertank_100percent; // 140cm

const uint liter_per_cm = 125; // Liter Inhalt pro centimeter: Pi*R*R(dezimeter)*1/10
                               // in Essencia ausmessen! aktuelle Schätzung: 20*20*3,14159/10 = 125


bool toggle_var = true;
unsigned long lastTestTriggerTime = 0;

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

func percentage_watertank (uint distance) //return-value geht hier wie?
{
  long percent = ( distance / distance_watertank_100percent );
  return percent
};

void setup() {
  Serial.begin(115200);
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
}

void loop() {
  toggle_var = !toggle_var;

  // 1. MESSUNG (Zuerst ausführen, damit der I2C-Bus nicht stört)
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20); // mit 18 läufts nicht. mit 20 läufts (board jsn-sr04t empfängt dann den trigger, bei kürzeren Zeiten irgendwie nicht. obwohl 10 reichen sollten, laut doku)
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, max_TOF_sens);
  long distance = duration * 0.034 / 2;

  
  // if out of range, sonderbehandlung: // auf gefiltertem wert
    // if out of range, dead-zone Sensor (Tank Full?)
    text_string = "Sensor Deadzone (Closer 22cm). If Tank full, then okey"

    
    // if out of range, too far away, error
    text_string = "detected Range to far";

  if out_of_range(distance) // Funktion muss noch definiert werden.
  {
    long distance_filtered = (distance_filtered/9*10) + distance*(1/10);
  }
    
  // TODOs:
  // Lora aus Chat von Steffen integrieren
  // Filter (22cm distance? und lowpass-filter generell interieren)
  // Boundaries, die kritisch sind eintragen:
     // Distance grösser 150cm - kann nicht sein.
     // Distance 22cm = Full, Min-Sensor-Value (DeadZone)
     // Distance 0cm == TimeOut -> verbessern!


  // 2. DISPLAY-AUSGABE (Erst nach der Messung starten)
  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 12, "Wassertank-Sensor");
    u8g2.drawHLine(0, 16, 128);

    if (duration == 0) {
      u8g2.setCursor(0, 35);
      u8g2.print("STATUS: TIMEOUT!");
      u8g2.setCursor(0, 55);
      u8g2.print("Sensor unplugged?");
    } else {
      u8g2.setCursor(0, 35);
      u8g2.print("STATUS: OKEY");
      
      u8g2.setCursor(0, 55);
      toggle_var = false; // für testzwecke

      if (toggle_var == true) {
        u8g2.print("UltraSonicTOF: ");
        u8g2.print(duration);
        u8g2.print(" us");
      } else {
        u8g2.print("Distanz: ");
        u8g2.print(distance);
        u8g2.print(" cm");
      }
    } 
  } while ( u8g2.nextPage() );

  delay(2000); 
}