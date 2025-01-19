#include <Arduino.h>
#include "BluetoothKeyboardLibrary.h"
#include <ESP32QRCodeReader.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string>

// Debug ausgaben Ein-/Ausschalten
#define DEBUG_ENABLED 0
#if DEBUG_ENABLED
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_INITIAL(x) Serial.begin(x)
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT(x)
#define DEBUG_INITIAL(x)
#endif

//Deklaration ESPQRCode Reader
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

// Pin - Definitionen
const int PIN_TASTER_LED = 3;

//Zustände
const int Z_BETRIEBS_MODUS = 0;
const int Z_EINSTELLUNGEN = 1;
const int Z_EINLESE_EINSTELLUNG = 2;
const int Z_LESEN_BLUETOOTH = 3;
const int Z_LESEN_SD = 4;
const int Z_VORHANDENE_DATEI = 5;
const int Z_NEUE_DATEI = 6;
const int Z_SPEICHERVERWALTUNG = 7;
const int Z_DATEI_AUSGEBEN = 8;

//Variablen für Ergebnise
int tasterDruck;


//Variable für Zustand
int zustand;
int zustand_alt;
int config = 0;
int letztesArchiv = 1;
TickType_t Delay20;
TickType_t Delay100;
TaskHandle_t QR_Code_Handel = NULL;
TaskHandle_t State_M_Handel = NULL;

//QR-Code Task (Behandelt die Ausgelesenen QR-Codes.)
void onQrCodeTask(void *pvParameters)
{
  struct QRCodeData qrCodeData;
  
  while (true)
  {
    if (reader.receiveQrCode(&qrCodeData, 100)) //Überprüft ob eingelesener QR-Code in Queue ist
    {
      DEBUG_PRINTLN("Found QRCode");
      if (qrCodeData.valid) //Überprüft ob die Prüfsumme des QR-Code's passt.
      { 
        DEBUG_PRINT("Payload: ");
        DEBUG_PRINTLN((const char *)qrCodeData.payload);

        if(config==0)  //QR-Code übermitteln per Bluetooth
        {
          typeText((const char *)qrCodeData.payload);
          typeText("\n");
        } else if(config == 1){ //QR-Code auf SD-Karte speichern.
          appendToFile(SD_MMC, "/aktuellQR.txt", (const char *)qrCodeData.payload);
          vTaskDelay( Delay100 );
          appendToFile(SD_MMC, "/aktuellQR.txt", "\n");
        }
        if(State_M_Handel != NULL) //LED 2 Sekunden läuchten lassen.
        {
          vTaskSuspend(State_M_Handel);
          pinMode(PIN_TASTER_LED, OUTPUT);
          digitalWrite(PIN_TASTER_LED, LOW);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
          digitalWrite(PIN_TASTER_LED, HIGH);
          pinMode(PIN_TASTER_LED, INPUT_PULLUP);
          vTaskResume(State_M_Handel);
        }

        while(reader.receiveQrCode(&qrCodeData, 100)) //QR-Code Queue lehren (verhindert das doppelte einlesen.)
        {
          delay(1);
        }

      }
      else //QR-Code ist nicht Valliede
      {
        DEBUG_PRINT("Invalid: ");
        DEBUG_PRINTLN((const char *)qrCodeData.payload);
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


//Einlesen des Tasters
void tasterTime(int startTime)
{
  tasterDruck = 0;
  int restTime = startTime;

  while(restTime > 0){ //Liest den Taster für die Anzahl der Millisekunden in: restTimer ein.
    if(digitalRead(PIN_TASTER_LED) == LOW) //Wenn Taster gedrückt wurde.
    {
      tasterDruck++;
      DEBUG_PRINT("Taste gedrückt ");
      vTaskDelay( Delay100 );
      while(digitalRead(PIN_TASTER_LED) == LOW){ //Wartet bis Taster nicht mehr gedrückt ist.
        vTaskDelay( Delay20 );
      }

      DEBUG_PRINT("Taste losgelassen ");
      restTime = 1000; //Setzt verbleibende Einlesezeit auf 1 Sekunde
      continue;
    }
    vTaskDelay( Delay100 );
    restTime -= 100;
  }

  DEBUG_PRINTLN("Fertig");
}

//************************** State Machine zum Einstellen der verschiedenen Modi **************************************************
//Verwaltung der Zustände
void state_machine(void *pvParameters)
{
  while(true)
  {
    switch(zustand){
      case Z_BETRIEBS_MODUS :betriebs_modus();break;
      case Z_EINSTELLUNGEN :einstellung();break;
      case Z_EINLESE_EINSTELLUNG :einleseEinstellung();break;
      case Z_LESEN_BLUETOOTH :lesenBluetooth();break;
      case Z_LESEN_SD :lesenSD();break;
      case Z_VORHANDENE_DATEI :vorhandeneDatei();break;
      case Z_NEUE_DATEI :neueDatei();break;
      case Z_SPEICHERVERWALTUNG :speicherverwaltung();break;
      case Z_DATEI_AUSGEBEN :dateiAusgeben();break;
      default: fehler_in_state_machine();
    }
  }
}

//Modus in dem ganz normal QR-Cods eingelesen werden.
void betriebs_modus()
{
  //Starte QR-Code leser, falls er zuvor pausiert wurde
  if(zustand_alt != Z_BETRIEBS_MODUS && QR_Code_Handel != NULL)
  {
    vTaskResume(QR_Code_Handel);
  }

  tasterTime(30000);

  // Überprüfen der Tastereingaben
  if(tasterDruck == 1)
  {
    // Wechsel in den Einstellungsmodus
    zustand_alt = zustand;
    zustand = Z_EINSTELLUNGEN;
  } else {
    // Keine Aktion, bleibt im Betriebsmodus
    zustand_alt = zustand;
  }
}

//Erste Ebene der Einstellung
void einstellung()
{
  //QR-Code Leser Stoppen.
  if(zustand_alt == Z_BETRIEBS_MODUS && QR_Code_Handel != NULL)
  {
    vTaskSuspend(QR_Code_Handel);
  }

  //Ausgabe des Menüs:
  typeText("\nEinstellungen:");
  vTaskDelay( Delay100 );
  typeText("\n1x Taster: Einlese-");
  vTaskDelay( Delay100 );
  typeText("Einstellung");
  vTaskDelay( Delay100 );
  typeText("\n2x Taster: ");
  vTaskDelay( Delay100 );
  typeText("Speicherverwaltung\n");
  vTaskDelay( Delay100 );
  tasterTime(8000);

  // Tastereingaben auswerten
  if(tasterDruck == 1)
  {
    // Wechsel in den Einlese-Einstellungsmodus
    zustand_alt = zustand;
    zustand = Z_EINLESE_EINSTELLUNG;
  }else if(tasterDruck == 2)
  {
    // Wechsel in die Speicherverwaltung
    zustand_alt = zustand;
    zustand = Z_SPEICHERVERWALTUNG;
  }else
  {
    // Keine Eingabe, Rückkehr in den Betriebsmodus
    zustand_alt = zustand;
    zustand = Z_BETRIEBS_MODUS;
  }
}

//Einlese Einstellung. Auswahl des einlese Modus.
void einleseEinstellung()
{
  //Ausgabe des Menüs:
  typeText("\nEinlese-Einstellung:");
  vTaskDelay( Delay100 );
  typeText("\n1x Taster: Eingelesener ");
  vTaskDelay( Delay100 );
  typeText("QR-Code direkt per ");
  vTaskDelay( Delay100 );
  typeText("Bluetooth");
  vTaskDelay( Delay100 );
  typeText(" übermitteln.");
  vTaskDelay( Delay100 );
  typeText("\n2x Taster: ");
  vTaskDelay( Delay100 );
  typeText("Eingelesener QR-Code auf");
  vTaskDelay( Delay100 );
  typeText(" SD-Karte speichern.\n");
  vTaskDelay( Delay100 );


  DEBUG_PRINTLN("Einlese-Einstellung:\n1x Taster: Eingelesener QR-Code direckt per Bluetooth übertragen.\n2x Taster: Eingelesener QR-Code auf SD-Karte speichern.");
  tasterTime(8000);

  // Tastereingaben auswerten
  if(tasterDruck == 1)
  {
    // Wechsel in den Bluetooth-Modus
    zustand_alt = zustand;
    zustand = Z_LESEN_BLUETOOTH;
  }else if(tasterDruck == 2)
  {
    // Wechsel in den SD-Karten-Modus
    zustand_alt = zustand;
    zustand = Z_LESEN_SD;
  }else
  {
    // Keine Eingabe, Rückkehr in den Betriebsmodus
    zustand_alt = zustand;
    zustand = Z_BETRIEBS_MODUS;
  }
}

//Lesen auf Bluetooth
void lesenBluetooth()
{
  typeText("\nDer Modus: ");
  vTaskDelay( Delay100 );
  typeText("Eingelesener ");
  vTaskDelay( Delay100 );
  typeText("QR-Code direkt per ");
  vTaskDelay( Delay100 );
  typeText("Bluetooth");
  vTaskDelay( Delay100 );
  typeText(" übermitteln ");
  vTaskDelay( Delay100 );
  typeText("wurde eingestellt.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Der Modus: Eingelesener QR-Code direckt per Bluetooth übertragen wurde eingestellt.");

  //Config anpassen
  if(config != 0){
    config = 0;
    writeFile(SD_MMC, "/config/config.txt", "0");
  }

  // Rückkehr in den Betriebsmodus
  zustand_alt = zustand;
  zustand = Z_BETRIEBS_MODUS;
}

void lesenSD()
{
  typeText("\nDer Modus: ");
  vTaskDelay( Delay100 );
  typeText("Eingelesener QR-Code ");
  vTaskDelay( Delay100 );
  typeText("auf SD-Karte speichern ");
  vTaskDelay( Delay100 );
  typeText("wurde eingestellt.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Der Modus: Eingelesener QR-Code auf SD-Karte speichern wurde eingestellt.");

  //Config anpassen
  if(config != 1){
    config = 1;
    writeFile(SD_MMC, "/config/config.txt", "1");
  }

  //Ausgabe des Menüs:
  typeText("\nLesen auf SD-Karte:");
  vTaskDelay( Delay100 );
  typeText("\n1x Taster: In ");
  vTaskDelay( Delay100 );
  typeText("vorhandene Datei speichern.");
  vTaskDelay( Delay100 );
  typeText("\n2x Taster: Neue ");
  vTaskDelay( Delay100 );
  typeText("Datei anlegen in ");
  vTaskDelay( Delay100 );
  typeText("die die QR-Codes ");
  vTaskDelay( Delay100 );
  typeText("gespeichert werden.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Lesen auf SD-Karte:\n1x Taster: In vorhandene Datei speichern.\n2x Taster: Neue Datei anlegen in die die QR-Codes gespeichert werden.");
  
  tasterTime(8000);

  // Auswerten der Nutzereingabe
  if(tasterDruck == 1)
  {
    //Wechsel in den Vorhandene-Datei-Modus
    zustand_alt = zustand;
    zustand = Z_VORHANDENE_DATEI;
  }else if(tasterDruck == 2)
  {
    //Wechseln in den Neuen-Datei-Modus
    zustand_alt = zustand;
    zustand = Z_NEUE_DATEI;
  }else
  {
    // Keine Eingabe, Rückkehr in den Betriebsmodus
    zustand_alt = zustand;
    zustand = Z_BETRIEBS_MODUS;
  }
}

void vorhandeneDatei()
{
  typeText("\nWird in ");
  vTaskDelay( Delay100 );
  typeText("vorhandener Datei ");
  vTaskDelay( Delay100 );
  typeText("gespeichert.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Wird in vorhandener Datei gespeichert.");

  // Übergang in den Betriebsmodus
  zustand_alt = zustand;
  zustand = Z_BETRIEBS_MODUS;
}

void neueDatei()
{
  if(letztesArchiv+1>1000000)
  {
    letztesArchiv = 0;
  }

  //Neue Datei anlegen.
  String pfad2 = String(++letztesArchiv) + ".txt";
  String pfad = "/archiv/" + pfad2;
  String inhalt = "";
  int zeilenAnzahl = countLines(SD_MMC, "/aktuellQR.txt");

  //Jede Zeile einzelt in neue Datei schreiben 
  for(int i = 1; i <= zeilenAnzahl; i++)
  {
    inhalt = readLine(SD_MMC, "/aktuellQR.txt", i) + "\n";
    vTaskDelay(200 / portTICK_PERIOD_MS);
    appendToFile(SD_MMC, pfad.c_str(), inhalt.c_str());
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }

  // Konfiguration aktualisieren und Bereinigung
  vTaskDelay( 200 / portTICK_PERIOD_MS );
  writeFile(SD_MMC, "/config/letztesArchiv.txt", String(letztesArchiv).c_str()); //Vorhandene Archivnummer in Config anpassen.
  vTaskDelay( 100 / portTICK_PERIOD_MS );

  //Löschen der Eingelesenen QR-Codes in aktuellQR.txt
  deleteFile(SD_MMC, "/aktuellQR.txt");
  vTaskDelay( 100 / portTICK_PERIOD_MS );
  writeFile(SD_MMC, "/aktuellQR.txt", "");
  vTaskDelay( 100 / portTICK_PERIOD_MS );

  typeText("\nNeue Datei ");
  vTaskDelay( Delay100 );
  typeText("wird angelegt.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Neue Datei wird angelegt.");

  // Übergang in den Betriebsmodus
  zustand_alt = zustand;
  zustand = Z_BETRIEBS_MODUS;
}

void speicherverwaltung()
{
  typeText("\nSpeicherverwaltung:");
  vTaskDelay( Delay100 );
  typeText("\n1x Taster: Neue ");
  vTaskDelay( Delay100 );
  typeText("Datei anlegen\n");
  vTaskDelay( Delay100 );
  typeText("2x Taster: Inhalt");
  vTaskDelay( Delay100 );
  typeText(" aus aktueller ");
  vTaskDelay( Delay100 );
  typeText("Datei ausgeben.\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Speicherverwaltung:\n1x Taster: Neue Datei anlegen- \n2x Taster: Inhalt aus aktueller Datei ausgeben.");
  tasterTime(8000);

  // Auswerten der Nutzereingabe
  if(tasterDruck == 1)
  {
    //Wechsel in den Neue-Datei-Modus
    zustand_alt = zustand;
    zustand = Z_NEUE_DATEI;
  }else if(tasterDruck == 2)
  {
    //Wechseln in den Datei-Ausgabe-Modus
    zustand_alt = zustand;
    zustand = Z_DATEI_AUSGEBEN;
  }else
  {
    // Keine Eingabe, Rückkehr in den Betriebsmodus
    zustand_alt = zustand;
    zustand = Z_BETRIEBS_MODUS;
  }
}

void dateiAusgeben()
{
  typeText("\nDer Inhalt ");
  vTaskDelay( Delay100 );
  typeText("der Datei wird ");
  vTaskDelay( Delay100 );
  typeText("ausgegeben sobald ");
  vTaskDelay( Delay100 );
  typeText("1x auf den Taster");
  vTaskDelay( Delay100 );
  typeText(" gedrückt wird. (");
  vTaskDelay( Delay100 );
  typeText("In den nächsten ");
  vTaskDelay( Delay100 );
  typeText("10 Sekunden)\n");
  vTaskDelay( Delay100 );
  DEBUG_PRINTLN("Der Inhalt der Datei wird ausgegeben sobald sie 1x auf den Taster drücken. (In den nächsten 10 Sekunden)");
  tasterTime(10000);

  String inhalt = "";
  int zeilenAnzahl = countLines(SD_MMC, "/aktuellQR.txt");


  if(tasterDruck == 1)
  {
    //Die Datei wird Zeilenweise per Bluetooth ausgegeben.
    for(int i = 1; i <= zeilenAnzahl; i++)
    {
      inhalt = readLine(SD_MMC, "/aktuellQR.txt", i) + "\n";
      typeText(inhalt.c_str());
      vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    
  }

  // Rückkehr in den Betriebsmodus
  zustand_alt = zustand;
  zustand = Z_BETRIEBS_MODUS;
}

void fehler_in_state_machine(){
  typeText("Fehler!");
  DEBUG_PRINTLN("Fehler in der State Machine! Undefinierter Zustand.");

  zustand_alt = zustand;
  zustand = Z_BETRIEBS_MODUS;
}

// ********************** SD Karten Funktionen **********************************************************

// Funktion zum Schreiben in eine Datei
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Datei wird erstellt: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    DEBUG_PRINTLN("Fehler beim Öffnen der Datei!");
    return;
  }

  if (file.print(message)) {
    DEBUG_PRINTLN("Datei erfolgreich geschrieben!");
  } else {
    DEBUG_PRINTLN("Fehler beim Schreiben in die Datei!");
  }

  file.close();
}

// Funktion zum Anhängen an eine Datei
void appendToFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Daten werden an Datei angehängt: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    DEBUG_PRINTLN("Fehler beim Öffnen der Datei!");
    return;
  }

  if (file.print(message)) {
    DEBUG_PRINTLN("Daten erfolgreich angehängt!");
  } else {
    DEBUG_PRINTLN("Fehler beim Anhängen an die Datei!");
  }

  file.close();
}

// Funktion zum Löschen einer Datei
void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("Datei wird gelöscht: %s\n", path);

  if (fs.remove(path)) {
    DEBUG_PRINTLN("Datei erfolgreich gelöscht!");
  } else {
    DEBUG_PRINTLN("Fehler beim Löschen der Datei!");
  }
}

// Funktion zum Lesen des Inhalts einer Datei
String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Datei wird gelesen: %s\n", path);

  File file = fs.open(path, FILE_READ);
  if (!file) {
    DEBUG_PRINTLN("Fehler beim Öffnen der Datei!");
    return "";
  }

  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }

  file.close();
  DEBUG_PRINTLN("Datei erfolgreich gelesen!");
  return content;
}

// Funktion zum Zählen der Zeilen in einer Datei
int countLines(fs::FS &fs, const char *path) {
  Serial.printf("Zeilen werden gezählt in: %s\n", path);

  File file = fs.open(path, FILE_READ);
  if (!file) {
    DEBUG_PRINTLN("Fehler beim Öffnen der Datei!");
    return -1;
  }

  int lineCount = 0;
  while (file.available()) {
    char c = file.read();
    if (c == '\n') {
      lineCount++;
    }
  }

  file.close();
  Serial.printf("Anzahl der Zeilen: %d\n", lineCount);
  return lineCount;
}

// Funktion zum Lesen einer bestimmten Zeile in einer Datei
String readLine(fs::FS &fs, const char *path, int lineNumber) {
  Serial.printf("Zeile %d wird gelesen aus: %s\n", lineNumber, path);

  File file = fs.open(path, FILE_READ);
  if (!file) {
    DEBUG_PRINTLN("Fehler beim Öffnen der Datei!");
    return "";
  }

  int currentLine = 0;
  String line = "";

  while (file.available()) {
    char c = file.read();
    if (c == '\n') {
      currentLine++;
      if (currentLine == lineNumber) {
        break;
      }
      line = "";
    } else {
      if (currentLine == lineNumber - 1) {
        line += c;
      }
    }
  }

  file.close();
  if (currentLine < lineNumber) {
    DEBUG_PRINTLN("Zeile nicht gefunden!");
    return "";
  }

  DEBUG_PRINTLN("Zeile erfolgreich gelesen!");
  return line;
}

// ********************** Setup Funktion **********************************************************
void setup() {
  DEBUG_INITIAL(115200);

  //Setup QR-Code Reader
  reader.setup();
 
  DEBUG_PRINTLN("Setup QRCode Reader");

  reader.beginOnCore(1);

  DEBUG_PRINTLN("Begin on Core 1");

  xTaskCreate(onQrCodeTask, "onQrCode", 4 * 1024, NULL, 3, &QR_Code_Handel);

  // start Bluetooth task
  xTaskCreate(bluetoothTask, "bluetooth", 20000, NULL, 3, NULL);

  
  //State Machine
  Delay20 = 20 / portTICK_PERIOD_MS; //Häufig verwendete Wartezeiten berechnen.
  Delay100 = 100 / portTICK_PERIOD_MS;

  zustand_alt = Z_BETRIEBS_MODUS; //Start zustände setzen
  zustand = Z_BETRIEBS_MODUS;

  pinMode(PIN_TASTER_LED, INPUT_PULLUP); //Taster Initialisieren

  xTaskCreate(state_machine, "state machine", 4096, NULL, 3, &State_M_Handel); //Start des State Machinen task

  
  // Initialisiere die SD-Karte
  if (!SD_MMC.begin()) {
    DEBUG_PRINTLN("SD-Karte konnte nicht initialisiert werden!");
    return;
  }
  

  //SD-Karte Config laden
  config = readFile(SD_MMC, "/config/config.txt").toInt();
  letztesArchiv = readFile(SD_MMC, "/config/letztesArchiv.txt").toInt();

  DEBUG_PRINTLN(config);
  
}

void loop() {
  
  delay(5000);
  
}
