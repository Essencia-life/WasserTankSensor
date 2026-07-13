#include <Arduino.h>
#include <U8g2lib.h>

// Konstanten des ESP-Verkabelung-Ultrasonic-Sensor JSP-SR04T-V33
const int echoPin = 5;
const int trigPin = 4;
const int cycle = 2000; // cycle of sensing waterlevel in milliseconds

// Konstanten des JSP-SR04T-Ultraschall-Sensors
const unsigned int distance_deadzone = 22;  // Deadzone des Sensors (bei dem Ultraschallsensor von JSN-SR04T ists wohl 22cm)
const long max_TOF_sens = 30000; // grenze des Sensors (maximale Mess-Reichweite in Mikrosekunden)

// Konstanten der Installation in Essencia Wassertank Sensor. Angaben in Zentimeter
const unsigned int distance_max_depth_watertank = 150; // maximaler, sinnvoller zu messender Wert zwischen Sensor und Tankboden (bzw.) der Wassertank-Tiefe (bis zum Sensor)
const unsigned int height_watertank_0percent = 10;  // bei 10cm (in Essencia) fängt das Wasserrohr an, darunterliegende Wasserstände können nicht gepumpt werden.
const unsigned int height_watertank_100percent = 120; // vom Boden, 120cm (in Essencia) is der Wassertank mit 100% voll betitelt. da der Sensor jedoch 22cm Deadzone hat, ist das mal hier so früh auf 100% definiert. Man müsste den Sensor höher montieren (wie chatgpt/gemini schon gesagt hatte)
const unsigned int distance_watertank_0percent = distance_max_depth_watertank - height_watertank_0percent; // 150 - 10 = 140cm Abstand zum Sensor bei leerem Tank
const unsigned int distance_watertank_100percent = distance_max_depth_watertank - height_watertank_100percent; // 150 - 120 = 30cm Abstand zum Sensor bei vollem Tank

const unsigned int liter_per_cm = 125; // Liter Inhalt pro centimeter: Pi*R*R(dezimeter)*1/10
                               // in Essencia ausmessen! aktuelle Schätzung: 20*20*3,14159/10 = 125

enum SensorState {
  STATE_OK,                     // Sensor and Distance: all good.
  STATE_TIMEOUT,                // Sensor could not detect any object. Is Object too far (more than 2,5meters) or Sensor fully Covered and Object laying on the sensor?
  STATE_DEADZONE,               // Kinda okey. waterlevel is close to sensor. Sensor-Deadzone is <22cm...3cm?.
  STATE_DRIFT_ERROR,            // too much sensor-drift in short time (did someone opening/closing the lit or did sensor fallen apart)
  STATE_INIT,                   // (at restart, no valid measure yet)
  //STATE_NEEDS_SERVICE,          // Sensor and System needs manual Service or Reset (unplug power (USB-C-Charger/Powersupply), wait 1 min, replug it. Or Search for further failures, if this did not help)
      // NEEDS SERVICE als status obsolet, weil INIT dann aufleuchtet. Und wenn Init mehr als x-Minuten beim Empfänger registriert wird. dann brauchts service. Sonst darf das system sich hier selbst heilen.
  //STATE_BELOW_PUMP_RESTART_LVL, // Usually pump would have started to pump more water in again, but waterlevel is below this level already
      // dann gäbe es auch ein "above pump stop level".
      // und generell wären die dann alle okey. Und irgendwie könnte man die pumprestartlevel vielleicht anderwo hinterlegen?!
  STATE_OUT_OF_RANGE,           // happens when lit opened, waterlevel seems to be deeper then the tank actually is
};


bool toggle_var = true;
String SensorTextPrint = "";    // Variable für Textausgabe deklariert
String SensorStatus = "";       // Variable für SensorStatus deklariert
SensorState currentSensorState = STATE_INIT;  // Sensorstatus auf Init-State schicken
float distance_filtered = 50.0;  // Globaler Filterwert, init-wert bei 50 cm damit keine 0Divisionen entstehen
bool is_first_run = true;       // Flag für Erstinitialisierung des Filters
unsigned int err_info_ctr = 1;
// float acc_usage_today = 0;      // Auffaddierter Verbrauch / Tag -> bräuchte Uhrzeit. Und will ich den verbrauch hier addieren?

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

int percentage_watertank (unsigned int distance) 
{
  // Schutz vor Werten außerhalb der definierten Tank-Geometrie
  if (distance >= distance_watertank_0percent) return 0;       // Abstand zu groß -> Tank leer
  if (distance <= distance_watertank_100percent) return 100;   // Abstand zu klein -> Tank voll
  
  // Nutzbarer Bereich (z.B. 140cm - 30cm = 110cm)
  long nutzbare_hoehe = distance_watertank_0percent - distance_watertank_100percent; 
  
  // Aktuelle Wasserhöhe über dem Nullpunkt (z.B. 140cm - 85cm = 55cm)
  long aktuelle_wasserhoehe = distance_watertank_0percent - distance;               
  
  // Erst multiplizieren, dann teilen, um Ganzzahl-Divisionsfehler (0 %) zu vermeiden
  return (aktuelle_wasserhoehe * 100) / nutzbare_hoehe; 
}

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

  if (distance == 0) {
    // Sensor somehow disconnected? not sensing anymore!
    currentSensorState = STATE_TIMEOUT;
    SensorStatus = "Error (Timeout)";
    SensorTextPrint = "Sensor Plugged?";
  } else if (distance <= distance_deadzone) {
    // if out of range, dead-zone Sensor (Tank Full?)
    currentSensorState = STATE_DEADZONE;
    SensorStatus = "OK (Min. Distance)";
    SensorTextPrint = "dist>22cm. If Tank full,is ok"; 
  } else if (distance > distance_max_depth_watertank) {
    // if out of distance, too far away, error
    currentSensorState = STATE_OUT_OF_RANGE;
    SensorStatus = "Error (> Max. Distance)";
    SensorTextPrint = "dist. > 150cm"; // SensorTextPrint = "dist. > int2str(distance_max_depth_watertank) cm".
  } else if ( ( abs( distance/distance_filtered - 1.0 ) > 0.05 ) && (is_first_run == false) ) 
    {  
    // Filter for opening Lit to have a look.
    // max accepted change 5% / cycle
    // would it be better to filter this later? on collected data?
    currentSensorState = STATE_DRIFT_ERROR;
    SensorStatus = "Error (high Sens drift)";
    SensorTextPrint = "Lit Opened?";
  } else if ( distance < distance_max_depth_watertank && distance > distance_deadzone ) // Also Sensor innerhalb der normalen, erwarteten Arbeitsbedingungen
    {
    if ( currentSensorState == STATE_INIT ) 
    {// Lowpass-Filter (90% Altwert, 10% Neuwert)
    // erster Startup Filterwert direkt setzten (Init)
      distance_filtered = distance;
      is_first_run = false;
    } else {
      distance_filtered = (distance_filtered * 0.9) + (distance * 0.1);
    }
    currentSensorState = STATE_OK;
    SensorStatus = "OK :-)"; 
    SensorTextPrint = String(distance) + " cm"; // funktioniert das hier? distance is ja float Und SensorTextPrint ein String...?
  } else {
    currentSensorState = STATE_INIT; // Ist doch wie init...
    SensorStatus = " ... starting up";
    SensorTextPrint = "lit closed? cable conected?";
  }
    
  
  
  // TODOs:
  // Lora aus Chat von Steffen integrieren
    // Data to send via Lora:
      // currentSensorState (Type ENUM SensorState)
      // WaterTankLevel (or Liters?) ()
  // Architektur-Bild zeichnen Sensor -> JSN -> ESP -> ESP -> Cloud? -> Telegramm? bzw. was sagt Steffen dazu?
  
  // Wasserstand:
  // vermutlich ist jetzt das meiste integriert.
  // Tests (Restart Sender, Restart Empfänger)
  // Wenn Restart Wassersensor, dann wird neu aufintegriert (Verbrauch vom Tag)
      // Empfänger schaut ob integrierter Tageswert? oder forlaufend Integrierendes? (Überlaufendes) kleiner als letzter Wert ist?
      // Wenn Überlauf und Reset gültig waren (z.B. binnen erwarteten 100 Litern) dann wird überlauf vom Sender beim Empfänger ernstgenommen
      // Wenn Überlauf unerwartet ist, dann wird von einem Restart vom Sender ausgegangen und neu, fortlaufend aufaddiert.
      // Empfänger kennt Tageszeit/Uhrzeit
  // Wenn Restart Empfänger, dann ?? (erstmal Tageswerte für Verbrauch verloren?) Oder man könnte sie sich aus der Cloud/USB-Stick oder so holen.

  // Wasserverbrauch
  // Bei eingeschaltener Pumpe UND Ablauf, wird die Differenz bzw. der Verbrauch währenddessen nicht erfasst. Wäre es sinnvoll "bei steigendem Wasserspiegel" den vorherigen Wasserverbrauch zu verlängern?
  // Kann man die Pumpgeschwindigkeit "eichen" und dann bei geringerer Pumpgeschwindigkeit auf Ablauf Rückschließen?
    // leider gibt es zwei Pumpen. Eine läuft bei Solar licht verfügbar -> Bohrloch -> kleine Pumpgeschwindigkeit
    // zweite Pumpe pumpt heftig, ist ein Dieselgenerator.

  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 12, "Wassertank-Sensor");
    u8g2.drawHLine(0, 16, 128);
    u8g2.setCursor(0, 35);
    u8g2.print("STATUS:");
    u8g2.print(SensorStatus);
    
    u8g2.setCursor(0, 55);
    if ( currentSensorState == STATE_OK )
    {
      u8g2.setCursor(0, 55);
      u8g2.print(SensorTextPrint);
    } else // switch-case for currentSensorState != OK
    {
      switch (err_info_ctr) // 
      {
      case 0: // print currentSensorStatus (is some Error)
        {
          u8g2.print(SensorStatus);
          err_info_ctr++;
          break;
        }
      case 1: // error solve description
        {
          u8g2.print(SensorTextPrint);
          err_info_ctr++;
          break;
        }
      case 2: // TOF Sens
      case 3: // distance Sensed
      case 4: // theoretically water level percentage
      case 5: // Lora state
      }
    }

    
    u8g2.setCursor(0, 55);
    toggle_var = false; // für testzwecke
    if ( currentSensorState == STATE_OK )
    {
      u8g2.print("Distanz: ");
      u8g2.print(distance);
      u8g2.print(" cm");
    }
    else 
    {
      if (toggle_var == true) {
        u8g2.print("UltraSonicTOF?: ");
        u8g2.print(duration);
        u8g2.print(" us");
      } else 
      {
        u8g2.print("Distanz?: ");
        u8g2.print(distance);
        u8g2.print(" cm");
      }
    }
      if (toggle_var == true) {
        u8g2.print("UltraSonicTOF: ");
        u8g2.print(duration);
        u8g2.print(" us");
      } else 
      
    }
    else
    {
      toggle_var != toggle_var;
    }
  } while ( u8g2.nextPage() );

  delay(cycle); 
}