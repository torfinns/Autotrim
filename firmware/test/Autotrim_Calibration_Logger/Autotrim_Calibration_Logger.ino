// ============================================================================
//  AUTOTRIM — CALIBRATION LOGGER  (8-talls sving-analyse)   [Arduino IDE]
// ----------------------------------------------------------------------------
//  Logger IMU (accel + gyro, alle akser) + GPS (SOG/COG) til intern flash
//  (LittleFS) under kjøring. Ikke-flyktig: kutt strøm på sjøen, dump CSV hjemme
//  over USB. RØRER IKKE reléer.
//
//  Formål: kalibrerings-/testkjøring i 8-tall for å analysere sving-artefakt —
//  verifisere at aY ≈ SOG·yawRate (gX) og bestemme sving-kompensasjon/-inhibering.
//
//  Auto-starter logging ved boot (ingen laptop nødvendig på sjøen).
//  Flusher til flash hvert 1 s -> trygt mot brått strømkutt (mister maks ~1 s).
//
//  KOMMANDOER over USB (Serial Monitor @ 115200, send tegn):
//     d = dump CSV til serieporten     i = info (filstørrelse/fri plass)
//     p = pause/fortsett logging        e = slett logg (start på nytt)
//
//  Montering (config.h): IMU SDA=21 SCL=22 (RST=-1) @0x28.  GPS UART2 16/17 @38400.
//  KREVER BIBLIOTEK: "Adafruit BNO055" (+ Unified Sensor + BusIO). LittleFS følger ESP32-core.
//  Board = "ESP32 Dev Module".
//
//  Kjøreplan 8-tall: varier fart og svingradius. Kjør et par runder hver vei.
//  Analyser CSV med analysis/analyze_calibration.py.
// ============================================================================
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <LittleFS.h>

// ---- pinner ----
static const int PIN_I2C_SDA = 21, PIN_I2C_SCL = 22, PIN_BNO_RST = -1;
static const uint8_t BNO_ADDR = 0x28;
static const int PIN_GPS_RX = 16, PIN_GPS_TX = 17;
static const uint32_t GPS_BAUD = 38400;
#define GPSSerial Serial2

static const uint32_t SAMPLE_MS = 40;      // 25 Hz
static const uint32_t FLUSH_MS  = 1000;    // flush til flash hvert 1 s
static const char* LOGPATH = "/calib.csv";
static const char* HEADER  = "t_ms,aX,aY,aZ,gX_yaw,gY,gZ_roll,sog_kn,cog_deg,roll_raw,amag";

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
static bool imuOk = false;

// GPS-parser
static char gline[120]; static uint8_t glen = 0;
static char gRmc='V'; static int gSats=0; static float gSog=0.0f, gCog=0.0f;

File   logFile;
bool   logging = true;
uint32_t tSample = 0, tFlush = 0;

// ---------- GPS ----------
static String field(const char* s, int idx){
  int f=0; String o="";
  for(const char* p=s; *p && *p!='*'; ++p){ if(*p==','){f++;continue;} if(f==idx)o+=*p; if(f>idx)break; }
  return o;
}
static void gpsParse(const char* s){
  if(strlen(s)<6||s[0]!='$') return;
  const char* t=s+3;
  if(!strncmp(t,"GGA",3)){ String n=field(s,7); if(n.length()) gSats=n.toInt(); }
  else if(!strncmp(t,"RMC",3)){
    String st=field(s,2), sp=field(s,7), co=field(s,8);
    gRmc = st.length()? st[0]:'V';
    gSog = (gRmc=='A' && sp.length())? sp.toFloat() : 0.0f;
    if(co.length()) gCog = co.toFloat();
  }
}
static void gpsPoll(){
  while(GPSSerial.available()>0){
    char c=(char)GPSSerial.read();
    if(c=='\n'||c=='\r'){ if(glen>0){ gline[glen]='\0'; gpsParse(gline); glen=0; } }
    else if(glen<sizeof(gline)-1) gline[glen++]=c;
  }
}

// ---------- logg-fil ----------
static void openLog(){
  bool fresh = !LittleFS.exists(LOGPATH);
  logFile = LittleFS.open(LOGPATH, fresh ? "w" : "a");
  if(logFile){
    if(fresh) logFile.println(HEADER);
    logFile.printf("# BOOT millis=%lu\n", (unsigned long)millis());  // separator mellom kjøringer
    logFile.flush();
  }
}
static void dumpLog(){
  if(logFile) logFile.flush();
  File r = LittleFS.open(LOGPATH, "r");
  if(!r){ Serial.println("(ingen logg)"); return; }
  Serial.println("----- CSV START -----");
  while(r.available()) Serial.write(r.read());
  Serial.println("\n----- CSV SLUTT -----");
  r.close();
}
static void eraseLog(){
  if(logFile) logFile.close();
  LittleFS.remove(LOGPATH);
  openLog();
  Serial.println("Logg slettet, ny startet.");
}
static void info(){
  size_t sz = logFile ? logFile.size() : 0;
  Serial.printf("Logg: %u byte | LittleFS brukt %u / total %u byte | logging=%s\n",
                (unsigned)sz, (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes(),
                logging?"PÅ":"pause");
}

void setup(){
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== Autotrim CALIBRATION LOGGER (LittleFS) ==="));

  if(!LittleFS.begin(true)){ Serial.println(F("LittleFS FEIL")); }

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); Wire.setClock(100000); delay(150);
  imuOk = bno.begin(OPERATION_MODE_ACCGYRO);
  if(imuOk){ delay(20); bno.setExtCrystalUse(true); Serial.println(F("IMU OK")); }
  else       Serial.println(F("IMU mangler - logger 0"));

  openLog();
  Serial.println(F("Logging startet (25 Hz). Kommandoer: d=dump  i=info  p=pause  e=slett"));
  info();
  tSample = tFlush = millis();
}

void loop(){
  gpsPoll();

  if(Serial.available()){
    char c=Serial.read();
    if(c=='d') dumpLog();
    else if(c=='e') eraseLog();
    else if(c=='i') info();
    else if(c=='p'){ logging=!logging; Serial.printf("Logging %s\n", logging?"PÅ":"pause"); }
  }

  uint32_t now=millis();
  if(logging && imuOk && (now - tSample >= SAMPLE_MS)){
    tSample = now;
    imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    float aX=a.x(),aY=a.y(),aZ=a.z(), gX=g.x(),gY=g.y(),gZ=g.z();
    float amag=sqrtf(aX*aX+aY*aY+aZ*aZ);
    float rollRaw=atan2f(aY,aX)*57.2957795f*(-1.0f);   // rollSign=-1 (verifisert)
    if(logFile){
      logFile.printf("%lu,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.2f\n",
        (unsigned long)now, aX,aY,aZ, gX,gY,gZ, gSog,gCog, rollRaw, amag);
    }
  }

  if(now - tFlush >= FLUSH_MS){ tFlush=now; if(logFile) logFile.flush(); }
}
