// FIX 1: Der Modus MUSS ganz oben definiert werden, damit alle nachfolgenden #ifdef-Blöcke synchron greifen.
// Hier den gewünschten Modus einkommentieren:
//#define IS_RECEIVER
#define IS_SENDER

#include <Arduino.h>
#include <heltec_unofficial.h> // Ersetzt Arduino.h, bringt u8g2 und radio mit
#include <U8g2lib.h>


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

// Konstanten des ESP-Verkabelung-Ultrasonic-Sensor JSP-SR04T-V33
const int echoPin = 5;
const int trigPin = 4;
const int cycle = 2000; // cycle of sensing waterlevel in milliseconds

// Konstanten des JSP-SR04T-Ultraschall-Sensors
const unsigned int distance_deadzone = 22;  // Deadzone des Sensors (bei dem Ultraschallsensor von JSN-SR04T ists wohl 22cm)
const long max_TOF_sens = 30000; // grenze des Sensors (maximale Mess-Reichweite in Mikrosekunden)

// Konstanten der Installation in Essencia Wassertank Sensor. Angaben in Zentimeter
const unsigned int distance_max_depth_watertank = 150; // maximaler, sinnvoller zu messender Wert zwischen Sensor und Tankboden (bzw.) der Wassertank-Tiefe (bis zum Sensor)
const unsigned int height_watertank_0percent = 20;  // bei 10cm (in Essencia) fängt das Wasserrohr an, darunterliegende Wasserstände können nicht gepumpt werden.
const unsigned int height_watertank_100percent = 120; // vom Boden, 120cm (in Essencia) is der Wassertank mit 100% voll betitelt. da der Sensor jedoch 22cm Deadzone hat, ist das mal hier so früh auf 100% definiert. Man müsste den Sensor höher montieren (wie chatgpt/gemini schon gesagt hatte)
const unsigned int distance_watertank_0percent = distance_max_depth_watertank - height_watertank_0percent; // 150 - 10 = 140cm Abstand zum Sensor bei leerem Tank
const unsigned int distance_watertank_100percent = distance_max_depth_watertank - height_watertank_100percent; // 150 - 120 = 30cm Abstand zum Sensor bei vollem Tank

const unsigned int liter_per_cm = 125; // Liter Inhalt pro centimeter: Pi*R*R(dezimeter)*1/10
                               // in Essencia ausmessen! aktuelle Schätzung: 20*20*3,14159/10 = 125

bool toggle_var = true; // toggle var for toggeling output of disance and percentage

// Konstanten der ESP-Ausgabe:
// siehe u8g2.setFont(u8g2_font_6x10_tf); weiter unten im text
#ifdef IS_RECEIVER
// --- EMPFÄNGER CONFIG & VARIABLEN (Muss VOR setup() stehen) ---
volatile bool rxFlag = false;
uint8_t rx_sensor_state = STATE_INIT;
int rx_water_level = -1;
String rx_status_text = "Waiting...";

// FIX 2: Vorwärtsdeklaration für den Compiler und IRAM_ATTR für die ISR auf ESP32
#if defined(ESP32)
void IRAM_ATTR rxIsr();
void IRAM_ATTR rxIsr() {
  rxFlag = true;
}
#else
void rxIsr();
void rxIsr() {
  rxFlag = true;
}
#endif

// Hilfsfunktion zur Textübersetzung der Stati
String getStatusText(uint8_t state) {
  switch(state) {
    case STATE_OK:            return "OK";
    case STATE_TIMEOUT:       return "Err (Timeout)";
    case STATE_DEADZONE:      return "OK (Min. Dist)";
    case STATE_DRIFT_ERROR:   return "Err (Drift)";
    case STATE_OUT_OF_RANGE:  return "Err (Out of Range)";
    default:                  return "Starting...";
  }
}

#endif

// Konstanten der LORA- (Long Range Radio Communication ESP)
unsigned int lora_send_sek = 10;   // lora Sending Frequency (sek) (60 = 1 minute) (10sek for development)
bool lora_send_waterconsump_ovrflw = false; // overflow-counter for waterconsumtion (integration t.b.d) just needed for reset and receiver-logic.
unsigned long int waterconsump = 0; // water-consumption not integrated yet


bool lora_state_is_ok = false;  // prepared
String SensorTextPrint = "";    // Variable für Textausgabe deklariert
String SensorStatus = "";       // Variable für SensorStatus deklariert
SensorState currentSensorState = STATE_INIT;  // Sensorstatus auf Init-State schicken
float distance_filtered = 50.0;  // Globaler Filterwert, init-wert bei 50 cm damit keine 0Divisionen entstehen
int watertank_level_percentage = -1;        // watertank_percentage (-1 init wert == eher unrealistisch bei normalem ablassen, weil das Rohr ja bei 0% ist)
// change watertank level to float or sth. which is 45.3% .. because it is toggeling too much. and it makes too much of a difference on 100 steps.
bool is_first_run = true;       // Flag für Erstinitialisierung des Filters
unsigned int err_info_ctr = 1;

unsigned long previousLoRaMillis = 0;           // Speichert den letzten Sendezeitpunkt
const unsigned long lora_send_interval = 10000; // Sendeintervall in Millisekunden (60s wären ziel-wert für konst-betrieb.)
// float acc_usage_today/waterconsumption = 0;      // Auffaddierter Verbrauch / Tag -> bräuchte Uhrzeit. Und will ich den verbrauch hier addieren?
// bool waterconsumption_ovrflw = false;        // auch noch to do für irgendwann.

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
  unsigned int result = ((aktuelle_wasserhoehe * 100) / nutzbare_hoehe ); 
  return result;
}



void setup() {
  
  // Serial USB serial output baud rate
  Serial.begin(115200);
  
  #ifdef IS_RECEIVER
  // LORA Empfänger-Teil:
  radio.setPacketReceivedAction(rxIsr);
  radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
  #endif

  #ifdef IS_SENDER
  // pin Modes of ESP-Controller and PINs for JSN-SR04T-Ultrasonic-Sensor-Board
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  #endif

  // Setup for built-in-Screen of ESP-Controller
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);    // fontsize: 6pix (max. width) + 10pix (max. heigt) for each letter/char

  // LORA Setup
  int state = radio.begin(868.0); // Startet Modul auf 868 MHz (Europa)
  if (state == RADIOLIB_ERR_NONE) {
    radio.setSpreadingFactor(10);
    radio.setCodingRate(5);
    radio.setSyncWord(0x12);      // Dein "Geheimcode", muss beim Empfänger gleich sein
    // PS: Die 4 LoRa-Filter (Sender & Empfänger)
    // 1. Frequenz (z. B. 868.0 MHz): Der physikalische Funkkanal. Sender und Empfänger müssen auf derselben Frequenz arbeiten.
    // 2. Spreading Factor (SF7 bis SF12): Bestimmt die Sendedauer und Signalspreizung. Höherer SF erhöht die Reichweite, senkt aber die Datenrate. Muss exakt übereinstimmen.
    // 3. Bandbreite (BW) & Coding Rate (CR): Die Breite des Funksignals (meist 125 kHz) und das Maß der Fehlerkorrektur (z. B. 4/5). Ohne Übereinstimmung bleibt das Signal für den Empfänger unlesbares Rauschen.
    // 4. Sync Word (Die Netzwerk-ID / 0xE5): 0xE5 für E55ENC1A. Ein Byte im Paket-Header. Der Empfänger filtert damit auf Software-Ebene: Passt das Sync Word nicht zu seiner Konfiguration, verwirft er das Paket sofort.

  } else {
    Serial.print("161: LoRa-Fehler beim Starten, Code: ");
    Serial.println(state);
    lora_state_is_ok = false;
  }
}


void loop() {

  #ifdef IS_SENDER

  // Mittelwertbildung über SensorWerte
  unsigned int valid_readings = 0;
  unsigned int ctr_errors = 0;
  float distance_acc = 0; // unsigned long um Überlauf bei der Addition zu vermeiden
  unsigned int last_duration = 0;

  // Max 10 Versuche, um exakt 5 gültige Messwerte zu sammeln
  for (int i = 0; i < 10 && valid_readings < 5; i++) 
  {
    // 1. MESSUNG 
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(20); 
    digitalWrite(trigPin, LOW);

    unsigned int duration = pulseIn(echoPin, HIGH, max_TOF_sens);
    last_duration = duration;

    float distance = duration * 0.034 / 2; 
    Serial.printf("\n192: Input-Filter run (%i) was distance %f (cm)", i, distance);
    if (distance != 0)
    {
      distance_acc += distance;
      valid_readings++;
      delay(50); // WICHTIG: Kurze Pause, damit sich das Ultraschall-Echo im Tank legt
    } 
    else 
    {
      ctr_errors++;
      delay(10);
    }
  }

  // 2. MITTELWERT BERECHNEN
  float distance = 0;
  if (valid_readings > 0) 
  {
    distance = distance_acc / valid_readings;
    Serial.printf("\n213: Distance_mean = %f ", distance);
  } 
  else
  {
    distance = 0; // Fallback, falls alle 10 Messungen fehlschlugen
  }
  
  Serial.printf("\n220: Distance = %f , distance_filtered %f,", distance, distance_filtered);
  if (distance == 0) {
    // Sensor somehow disconnected? not sensing anymore!
    currentSensorState = STATE_TIMEOUT;
    SensorStatus = "ERR (Timeout)";
    SensorTextPrint = "Sensor disconnected?";
  } else if (distance <= distance_deadzone) {
    // if out of range, dead-zone Sensor (Tank Full?)
    currentSensorState = STATE_DEADZONE;
    SensorStatus = "OK (Min. Distance)";
    SensorTextPrint = "dist>22cm. If Tank full,is ok"; 
    watertank_level_percentage = percentage_watertank(distance_filtered);
  } else if (distance > distance_max_depth_watertank) {
    // if out of distance, too far away, error
    currentSensorState = STATE_OUT_OF_RANGE;
    SensorStatus = "ERR (> Max. Distance)";
    SensorTextPrint = "dist. > 150cm"; // SensorTextPrint = "dist. > int2str(distance_max_depth_watertank) cm".
  } else if ( ( abs( distance-distance_filtered ) > 10 ) && (is_first_run == false) )
    {
    // Rate-Filter for opening Lit (to have a look or sth.).
    // max accepted change 4cm / cycle
    // would it be better to filter this later? on collected data?
    currentSensorState = STATE_DRIFT_ERROR;
    SensorStatus = "ERR (high Sens drift)";
    SensorTextPrint = "Lit Opened? close & reboot";
  } else if ( distance < distance_max_depth_watertank && distance > distance_deadzone ) // Also Sensor innerhalb der normalen, erwarteten Arbeitsbedingungen
    {
    if ( currentSensorState == STATE_INIT ) 
    {// Lowpass-Filter (90% Altwert, 10% Neuwert)
    // erster Startup Filterwert direkt setzten (Init)
      distance_filtered = distance;
      is_first_run = false;
    } else {
      distance_filtered = ( (distance_filtered * 0.8) + (distance * 0.2) );
    }
    currentSensorState = STATE_OK;
    SensorStatus = "OK";
    SensorTextPrint = String(distance_filtered) + " cm";
    watertank_level_percentage = percentage_watertank(distance_filtered);
  } else {
    currentSensorState = STATE_INIT; // Ist doch wie init...
    SensorStatus = " ... starting up";
    SensorTextPrint = "lit closed? cable conected?";
  }
    
  Serial.printf("\n265: CurrentSensorState = %i ",currentSensorState);
  
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


  
  // 2.1 LORA zykluszeit abfragen
  unsigned long currentMillis = millis(); // Aktuelle Systemzeit abfragen

  // 2.2. LORA zyklus-Zeit-Vergangen Prüfen, (ob die Differenz zwischen jetzt und der letzten Sendung größer als das Intervall ist
  if (currentMillis - previousLoRaMillis >= lora_send_interval) 
  {
    previousLoRaMillis = currentMillis;
    
    uint8_t payload[2];
    payload[0] = (uint8_t)currentSensorState;         // byte 0 = sensorstatus (enum)
    payload[1] = (uint8_t)watertank_level_percentage; // byte 1 = waterlevel (conversion to int)(to save load)
                                                      // byte 2 = waterconsumption? (t.b.d.)
                                                      // byte 3 = waterconsumption_overflow_bit? (t.b.d.)

    int lora_tx_state = radio.transmit(payload, 2);
    
    if (lora_tx_state == RADIOLIB_ERR_NONE) {
      lora_state_is_ok = true;
    } else {    // Antenne war nicht angeschlossen (im Night-Versuch) und dennoch war state_okey... versteh ich nicht.
      lora_state_is_ok = false;
    }  
    
    Serial.print("307: Lora payload");
    Serial.print(payload[0]);Serial.print(payload[1]);
  }
  

  toggle_var = !toggle_var; // toggle display with two information distance and percentage

  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 12, "Wassertank-Sensor (S)");
    u8g2.drawHLine(0, 14, 128);
    u8g2.setCursor(0, 26);
    u8g2.print("STATUS: ");
    u8g2.print(SensorStatus);
    
    //u8g2.setCursor(0, 55); // (max 64)
    if (currentSensorState == STATE_OK or currentSensorState == STATE_DEADZONE or lora_state_is_ok == true)
    {
        u8g2.setCursor(0, 38);
        u8g2.print("Distance: " + String(distance_filtered) + " cm");
        u8g2.setCursor(0, 50); // (max 64)
        u8g2.print("Water-Level: " + String(watertank_level_percentage) + " %");
        Serial.printf("\n329: Water-Level %i", watertank_level_percentage);
    } 
    else // switch-case for currentSensorState != OK
    {
      u8g2.setCursor(0, 38);
      u8g2.print(SensorTextPrint);
      Serial.println("335: \n... in Error-printout for display");
      u8g2.setCursor(0, 50);
      switch (err_info_ctr)
      {
        case 0: // print currentSensorStatus (is some Error)
          u8g2.print(SensorStatus);
          //err_info_ctr++;
          break;

        case 1: // error solve description
          u8g2.print(SensorTextPrint); 
          //err_info_ctr++;
          break;

        case 2: // raw-TOF-Wert of sensor
          u8g2.print("?TOF?: ");
          u8g2.print(last_duration);
          u8g2.print(" us");
          //err_info_ctr++;
          break;

        case 3: // calculed distance based on TOF:
          u8g2.print("?Dist?(unfilt.): ");
          u8g2.print(distance);
          u8g2.print(" cm");
          //err_info_ctr++;
          break;

        case 4: // distance filtered
          u8g2.print("?Dist?(tpf): ");
          u8g2.print(distance_filtered);
          u8g2.print(" cm");
          //err_info_ctr++;
          break;

        case 5: // theoreth. waterlevel %
          u8g2.print("percent; " + String(watertank_level_percentage) + " %"); // calc waterlevel %
          //err_info_ctr++;
          break;

        case 6: // LoRa-Status (right now only error or ok)
          u8g2.print("LoRa is ok: ");
          u8g2.print(lora_state_is_ok);
          err_info_ctr = 0; // reset err_info_ctr for start over after this last info
          break;

        default:
          err_info_ctr = 0;
          break;
      }
    }
    u8g2.setCursor(0, 62);
    u8g2.print("D:");
    u8g2.print(distance);
    u8g2.print("  ---> D(f); ");
    u8g2.print(distance_filtered);
    
  }
  while ( u8g2.nextPage() );

  err_info_ctr++;
  #endif

  #ifdef IS_RECEIVER
  // --- EMPFÄNGER LOOP (Nur Status & Waterlevel) ---
  
  // 1. Prüfen, ob ein Paket über den Interrupt registriert wurde
  if (rxFlag) {
    rxFlag = false;
    
    uint8_t payload[2];
    int state = radio.readData(payload, 2);
    
    if (state == RADIOLIB_ERR_NONE) {
      rx_sensor_state = payload[0];
      rx_water_level  = payload[1];
      rx_status_text  = getStatusText(rx_sensor_state);
    }
    
    // Radio wieder in den Empfangsmodus versetzen
    radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
  }

  // noch zu integrieren. Zeitstempel (aus Internet?)
  // display: vergangene Zeit (sek) seit letztem empfangenen Wert (oder seit startup)
  // einbindung ins WLAN.
  // daten mit Zeitstempel in cloud(? wo genau?) packen? oder was?
  // water-Consumtion auch beim Empfänger integrieren?

  // 2. Display aktualisieren

  // Display-Ausgabe optimieren:
  // 1. Reihe Infos (ok, not okey)
  // 2. Reihe Wasserlevel den Tag über verteilt (Kurve)
  // 3. Reihe Wasserverbrauch / std. Balkendiagramm (akkumuliert heute, vsl. heute, vergl. Wochendurchschnitt.)
  // (oder ist das eher die Ausgabe für die Webseite/app?)
  // waterconsumption? (water out?)
  // water in
  // put everything in 1 diagramm? (level + water in & out?)  
  // 128 pix = every 20mins
  u8g2.firstPage();
  do {
    u8g2.drawStr(0, 12, "Watertank Level (R)");
    u8g2.drawHLine(0, 14, 128);
    
    // Status-Ausgabe (Zeile 1)
    u8g2.setCursor(0, 28);
    u8g2.print("STATUS: ");
    u8g2.print(rx_status_text);
    
    // Wasserstand-Ausgabe (Zeile 2)
    u8g2.setCursor(0, 40);
    if (rx_sensor_state == STATE_OK or rx_sensor_state == STATE_DEADZONE)
    {
      u8g2.print("Water-Level: " + String(rx_water_level) + " %");
    } else {
      u8g2.print("Water-Level: -- %");
    }
  } while ( u8g2.nextPage() );
  #endif

  delay(cycle);
  
}