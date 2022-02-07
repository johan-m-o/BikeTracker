/*
   Bike Tracker v2.0
   Copyright (c) 2021 Johan Oscarsson
   Released under the MIT licence

   Documentation and code: https://www.github.com/Didgeridoohan/BikeTracker
*/

#include <Adafruit_SleepyDog.h>                     // Adafruit Sleepydog Arduino library https://github.com/adafruit/Adafruit_SleepyDog
#include <ADXL345_WE.h>                              // Wolles Elektronikkiste's ADXL345 accelerometer library https://github.com/wollewald/ADXL345_WE (Tested and working on v2.0.3)
#include <ArduinoLowPower.h>                        // Arduino Low Power library https://www.arduino.cc/en/Reference/ArduinoLowPower
#include <MKRGSM.h>                                 // Arduino MKRGSM library https://www.arduino.cc/en/Reference/MKRGSM
#include <RTCZero.h>                                // Arduino RTC library https://www.arduino.cc/en/Reference/RTC
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>   // SparkFun u-blox Arduino GNSS Library https://github.com/sparkfun/SparkFun_u-blox_GNSS_Arduino_Library
#include <TimeLib.h>                                // Arduino Time Library https://www.github.com/PaulStoffregen/Time
#include <Wire.h>                                   // Arduino Wire library https://www.arduino.cc/en/Reference/Wire

#include "arduino_secrets.h"

#define ADXL345_I2CADDR 0x53
ADXL345_WE myAcc(ADXL345_I2CADDR);

GSMLocation location;
GPRS gprs;
GSM gsmAccess;
GSM_SMS sms;

RTCZero rtc;

SFE_UBLOX_GNSS myGNSS;
#define WAKEUP_PIN 7 // GPS wakeup pin

/*****************
 *   Variables   *
 *****************/

const int int1Pin = 6; // Accelerometer interrupt pin 1
const int int2Pin = 8; // Accelerometer interrupt pin 2
const int OUTPUT_PIN = 10; // Reset pin

byte WatchDogCause = Watchdog.resetCause();
unsigned int WatchDogTimer = 30000;
char *ResetCause[7] = {"Power On Reset", "Brown Out 12 Detector Reset", "Brown Out 33 Detector Reset", "External Reset", "Watchdog Reset", "System Reset Request"};
char *abbrvMonth[13] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
char *cmpsPoints[9] = {"North", "North-east", "East", "South-east", "South", "South-west", "West", "North-west"};

volatile bool in_activity = false;
bool accActive = true, mvCheck = false, sleeping = false, toCheck = false, idleMode = false, stopAll = false, shutDown = false; // Markers for activity, movement, device sleeping, GPS time-out, idle mode
byte wakeInt = 5, sleepTh, sleepTm, sleepTs; // Interval in minutes until next deep sleep after a wakeup; hour, minute and seconds for the sleep timer
unsigned long sleepT, sleepDur;

/* GPS data */
float latitude, longitude, course, speed;
byte satellites;
long accuracy;
volatile bool GPSsend; // Marker for if GPS data collection should start
bool GPSwake = false, GPSloc = false, GPScont = false; // Markers for GPS data collection and GPS location
byte GPSwCount = 0; // Counter for active rounds of GPS data collection
int dirNum;

/* RTC and timing */
byte d, i, j, m, w, y, item; // Counters
byte alarmH, alarmM, alarmS, GPStimer = 2; // Alarm hours, minutes, seconds and time between alarms
unsigned long t, gpsMillis = 0, gpsSendMillis = 0, initMoveMillis = 0, moveMillis = 0, stillMillis = 0, chargedMillis, startTime, startStopMillis = 0; // Timing
unsigned int moveTimer = 15000;  // Number of ms for activity to have been regiestered until an SMS is sent
byte stillTimer = 5; // Number of seconds for inactivity to be registered
unsigned long upSeconds, upMinutes, upHours, upDays; // Up-time variables
String compYear, compMonth, compDay, compHour, compMinute, compSecond; // Compilation date/time
String timeYear, timeMonth, timeDay, timeHour, timeMinute, timeSecond; // Device/GPS date/time
String timeString;
char *abbr;
int SMSwakeup = 99;
int sleepHour;

/* GSM */
bool connected = false;
const char PINNUMBER[] = SECRET_PINNUMBER, PHONENUMBER[] = SECRET_PHONENUMBER, PASSWORD[] = SECRET_PASSWORD, GPRS_APN[] = SECRET_GPRS_APN, GPRS_LOGIN[] = SECRET_GPRS_LOGIN, GPRS_PASSWORD[] = SECRET_GPRS_PASSWORD;
char senderNumber[20];
String SMSrec, SMStxt;
int c;

float currBattery; // Current battery level
int sensorValue; // Battery sensor value
bool bWarning = false, bCharged = false; // Battery warning and charged checks

/*****************
 *   Functions   *
 *****************/

void in_activityISR() { // Activity interrupt function
  if (!shutDown) {
    in_activity = true;
  }
}

void WakeUp() { // Exit deep sleep interrupt dummy function
  ;
}

void GPSalarmMatch() { // GPS data collecting interrupt function
  GPSsend = true;
}

void sleepTime(int addT) { // Set time for deep sleep
  sleepTh = rtc.getHours() + DSTcheck();
  sleepTm = rtc.getMinutes() + addT;
  if (sleepTm > 59) {
    sleepTm -= 60;
    sleepTh += 1;
  }
  if (sleepTh > 23) {
    sleepTh -= 24;
  }
  sleepTs = rtc.getSeconds();
}

void GPSrounds(bool chkCount) { // Perform checks every round of collecting GPS data
  if (GPSwCount > 4 && GPScont == false) { // Reset the count of rounds to send GPS data, unless continious GPS mode is enabled
    GPSwCount = 0;
    if (!accActive && !mvCheck || GPSloc) { // Stop sending GPS data if everything is quiet or a single GPS location has been requested
      GPSstop();
    }
  }
  if (GPSwake && !GPSloc) { // Set alarm for next collecting of GPS data
    alarmH = rtc.getHours() + DSTcheck();
    alarmM = rtc.getMinutes() + GPStimer;
    alarmS = rtc.getSeconds();
    if (alarmM > 59) {
      alarmH += 1;
      alarmM -= 60;
    }
    if (alarmH > 23) {
      alarmH -= 24;
    }
    rtc.setAlarmTime(alarmH, alarmM, alarmS);
    rtc.enableAlarm(rtc.MATCH_MMSS);
    rtc.attachInterrupt(GPSalarmMatch);

    if (chkCount) {
      GPSwCount++;
    }
  }
  gpsMillis = 0;
  toCheck = false;
  GPSsend = false;
}

int headingTxt(float tmp_course) { // Convert heading from degrees to cardinal points
  if (tmp_course < 360) {
    // Do nothing
  }
  else {
    tmp_course -= 360;
  }
  dirNum = ((tmp_course * 2) + 45) / (45 * 2); // Calculate the direction integer
  if (dirNum >= 8) {
    dirNum = 0;
  }
  return dirNum;
}

void GPSwakeUp() {
  digitalWrite(WAKEUP_PIN, LOW);
  delay(1000);
  digitalWrite(WAKEUP_PIN, HIGH);
  delay(1000);
  digitalWrite(WAKEUP_PIN, LOW);
}

void GPSstart() { // Start collecting GPS data
  timeCreate(true);
  GPSwake = true;
  GPSalarmMatch();
  SMStxt += "Start sending GPS data.\n" + timeString;
  sendSMS();
}

void GPSstop() { // Stop collecting GPS data
  timeCreate(true);
  GPSwake = false;
  GPScont = false;
  GPStimer = 2;
  rtc.disableAlarm();
  rtc.detachInterrupt();
  SMStxt += "Stop sending GPS data.\n" + timeString;
  sendSMS();
  if (!GPSloc) {
    sleepTime(20); // Make sure that the device is awake for 20 minutes after GPS stops sending
  }
  else {
    sleepTime(wakeInt); // Wait only the default time if a single GPS location was requested
  }
  GPSloc = false;
}

void SMScommand() { // Execute the command recieved through SMS
  timeCreate(true);
  if (SMSrec.length() != 0) {
    if (SMSrec == "gpsloc" && !shutDown) {
      if (!stopAll) {
        SMStxt += "\n\nGPS location command received.";
        GPSloc = true;
        if (GPSwCount == 0) {
          GPSwCount = 5;
        }
        GPSstart();
      }
      else {
        SMStxt += "\n\nDevice in idle mode. Can't activate GPS.";
      }
    }
    else if (SMSrec == "gpscont" && !shutDown) {
      SMStxt += "Continous GPS transmission command received.";
      GPScont = true;
    }
    else if (SMSrec == "gpsstop" && !shutDown) {
      if (!stopAll) {
        SMStxt += "\n\nGPS stop command received.";
        gpsMillis = 0;
        GPSsend = false;
      }
      else {
        SMStxt += "\n\nDevice in idle mode. GPS already stopped.";
      }
    }
    else if (SMSrec.startsWith("gpstimer") && !shutDown) {
      SMSrec.remove(0,8);
      if (!stopAll) {
        SMStxt += "\n\nGPS timer command received.";
        if (SMSrec.length() > 0 && SMSrec.toInt() != 0 && SMSrec.toInt() < 255) {
          GPStimer = SMSrec.toInt();
          SMStxt += "\nSetting GPS location sending timer to " + SMSrec + " minutes.";
        }
        else {
          SMStxt += "\nThe timer command needs a time component. Try again.";
        }
      }
      else {
        SMStxt += "\n\nDevice in idle mode. GPS isn't active.";
      }
    }
    else if (SMSrec.startsWith("gps") && !shutDown) {
      SMSrec.remove(0,3);
      if (!stopAll) {
        SMStxt += "\n\nGPS start command received.";
        if (SMSrec.length() > 0 && SMSrec.toInt() != 0 && SMSrec.toInt() < 255) {
          GPStimer = SMSrec.toInt();
          SMStxt += "\n\nSetting GPS location sending timer to " + SMSrec + " minutes.";
        }
        GPSstart();
      }
      else {
        SMStxt += "\n\nDevice in idle mode. Can't activate GPS.";
      }
    }
    else if (SMSrec == "battery" && !shutDown) {
      SMStxt += "\n\nBattery check command received.\n" + String(readBattery(true)) + "V";
    }
    else if (SMSrec == "uptime" && !shutDown) {
      upSeconds = gsmAccess.getTime() - startTime; // Calculate time since program initation, in seconds
      upMinutes = upSeconds / 60;
      upHours = upMinutes / 60;
      upDays = upHours / 24;
      SMStxt += "\n\nUptime command received.\nThe device has been active for " + String(int(upDays)) + " days, " + String(int(upHours % 24)) + " hours, " + String(int(upMinutes % 60)) + " minutes and " + String(int(upSeconds % 60)) + " seconds.";
    }
    else if (SMSrec.startsWith("idle") && !shutDown) {
      if (!idleMode) {
        sleepTime(0);
        SMSrec.remove(0, 4);
        idleMode = true;
        SMStxt += "\n\nIdle mode command received.\nPutting device in idle mode";
        if (SMSrec.length() > 0 && SMSrec.toInt() != 0 && SMSrec.toInt() < 25) {
          SMSwakeup = SMSrec.toInt();
          if (SMSwakeup == 24) {
            SMSwakeup = 0;
            SMStxt += " until midnight.";
          }
            SMStxt += " until " + String(SMSwakeup) + ":00 hours.";
        }
        SMStxt += ".";
      }
      else {
        SMSrec == "";
        SMStxt += "\n\nDevice is already in idle mode...\nStop wasting battery!";
      }
    }
    else if (SMSrec == "stopidle" && !shutDown) {
      if (idleMode) {
        idleMode = false;
        SMStxt += "\n\nStoppping idle mode.";
      }
      else {
        SMSrec == "";
        SMStxt += "\n\nDevice is already using the default sleeping mode...";
      }
    }
    else if (SMSrec == "restart" && !shutDown) {
      SMStxt += "\n\nRestart command received.\nRestarting...";
    }
    else if (SMSrec.startsWith("deepsleep") && !shutDown) {
      sleepTime(0);
      SMSrec.remove(0, 9);
      SMStxt += "\n\nDeep sleep mode command recieved. Sleeping";
      if (SMSrec.length() > 0 && SMSrec.toInt() != 0 && SMSrec.toInt() < 25) {
        SMSwakeup = SMSrec.toInt();
        if (SMSwakeup == 24) {
          SMSwakeup = 0;
          SMStxt += " until midnight.";
        }
          SMStxt += " until " + String(SMSwakeup) + ":00 hours.";
      }
      else {
        SMStxt += "...";
      }
    }
    else if (SMSrec.startsWith("setsleep") && !shutDown) {
      SMSrec.remove(0, 8);
      if (SMSrec.length() > 0 && SMSrec.toInt() != 0) {
        sleepTime(SMSrec.toInt());
        SMStxt += "\n\nSet sleep timer command recieved. Setting sleep timer to " + SMSrec + " minutes.";
      }
      else {
        SMStxt += "\n\nSleep timer command needs a time component (non-zero). Try again.";
      }
    }
    else if (SMSrec == "shutdown" && !shutDown) {
      shutDown = true;
      SMStxt += "\n\nShut-down command recieved.\nPutting device in shut-down mode.";
    }
    else if (SMSrec == "startup") {
      SMStxt += "\n\nStart-up command recieved.\nWaking up device.";
    }
    else {
      if (shutDown) {
        SMStxt += "\n\nDevice in shut-down mode, \"startup\" is the only recognised command.";
      }
      else {
        SMStxt += "\n\nUnknown command.";
      }
    }
    SMStxt += "\n" + timeString;
  }
  else {
    SMStxt += "\n\nIncorrect/no password recieved.\nCommand dismissed.\n" + timeString;
  }

  sendSMS();

  if (SMSrec == "gpsstop") {
    GPSstop();
  }

  if (SMSrec == "shutdown") {
    StartStop(true);
  }

  if (SMSrec == "stopidle" || SMSrec == "startup") {
    StartStop(false);
  }
  
  if (SMSrec == "restart") {
    digitalWrite(OUTPUT_PIN, LOW);
  }
}

void StartStop(bool mode) { // Enable or disable idle mode
  if (!mode) {
    sleeping = true;
  }
  else { // Put as much as possible into low power mode.
    startStopMillis = millis();
    stopAll = true;
    GPSstop();
    gpsMillis = 0;
    GPSsend = false;
  }
}

void sendSMS() { // Send SMS
  sms.beginSMS(PHONENUMBER);
  sms.print(SMStxt);
  sms.endSMS();
  SMStxt = "";
}

float readBattery(bool volt) { // Read battery voltage
  sensorValue = analogRead(ADC_BATTERY);
  if (volt) {
    return sensorValue * (4.2 / 1023.0);
  }
  else {
    return sensorValue;
  }
}

unsigned long tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss) { // Convert date/time string to epoch time
  tmElements_t tmSet;
  tmSet.Year = YYYY - 1970;
  tmSet.Month = MM;
  tmSet.Day = DD;
  tmSet.Hour = hh;
  tmSet.Minute = mm;
  tmSet.Second = ss;
  return makeTime(tmSet);
}

void compileTime() { // Convert the compile date and time strings to separate entries
  j = 0;
  String compTmp, tmpIn, delim;
  compTmp.reserve(6);
  tmpIn.reserve(12);
  delim.reserve(2);
  tmpIn = __DATE__;
  tmpIn.replace("  ", " ");
  delim = " ";
  for (i = 0; i < 3; i++) {
    compTmp = tmpIn.substring(j, tmpIn.indexOf(delim, j));
    j = tmpIn.indexOf(delim, j) + 1;
    if (delim == " ") {
      if (i == 0) {
        compMonth = compTmp;
      }
      else if (i == 1) {
        compDay = compTmp;
      }
      else {
        compYear = compTmp;
      }
    }
  }
  j = 0;
  tmpIn = __TIME__;
  delim = ";";
  for (i = 0; i < 3; i++) {
    compTmp = tmpIn.substring(j, tmpIn.indexOf(delim, j));
    j = tmpIn.indexOf(delim, j) + 1;
    if (delim == " ") {
      if (i == 0) {
        compHour = compTmp;
      }
      else if (i == 1) {
        compMinute = compTmp;
      }
      else {
        compSecond = compTmp;
      }
    }
  }
}

unsigned long rtcSet() { // Set RTC time from GSM network
  t = gsmAccess.getTime();
  compMonth.toCharArray(abbr, 5);
  if (t + 86400 < tmConvert_t(compYear.toInt(), monthAbbr(), compDay.toInt(), compHour.toInt(), compMinute.toInt(), compSecond.toInt())) {
    SMStxt += "\n\nDate/time from GPRS network is too old. Resetting";
    sendSMS();
    digitalWrite(OUTPUT_PIN, LOW); // Reset the board if the GSM time is older than compile time. The time from the GSM network sometimes reports as being way too old.
  }
  rtc.setTime(hour(t), minute(t), second(t));
  rtc.setDate(day(t), month(t), 1970 + year(t) - 2);
  return t;
}

byte monthAbbr() { // Find month from abbreviation
  if (strlen(abbr) > 0) {
    for (i == 0;i < 12; i++) {
      if (abbr == abbrvMonth[i]) {
        return (i + 1);
        break;
      }
    }
    return 0;
  }
}

void timeCreate(bool RTCtime) { // Create the date/time string, either from RTC or GPS
  if (RTCtime) {
    timeYear = String(2000 + rtc.getYear());
    timeMonth = rtc.getMonth();
    timeDay = rtc.getDay();
    timeHour = rtc.getHours() + DSTcheck();
    timeMinute = rtc.getMinutes();
    timeSecond = rtc.getSeconds();
  }
  else {
    timeYear = String(myGNSS.getYear());
    timeMonth = myGNSS.getMonth();
    timeDay = myGNSS.getDay();
    timeHour = myGNSS.getHour() + DSTcheck();
    timeMinute = myGNSS.getMinute();
    timeSecond = myGNSS.getSecond();
  }

  // Add leading zero
  if (timeMonth.toInt() < 10) {
    timeMonth = "0" + timeMonth;
  }
  if (timeDay.toInt() < 10) {
    timeDay = "0" + timeDay;
  }
  if (timeHour.toInt() < 10) {
    timeHour = "0" + timeHour;
  }
  if (timeMinute.toInt() < 10) {
    timeMinute = "0" + timeMinute;
  }
  if (timeSecond.toInt() < 10) {
    timeSecond = "0" + timeSecond;
  }
  
  timeString = timeYear + "-" + timeMonth + "-" + timeDay + " " + timeHour + ":" + timeMinute + ":" + timeSecond;
}

byte DSTcheck() { // Calcualte if 1 or 2 hours should be added to the time, depending on if it's daylight savings time or not (EU based, GMT+1)
  y = 2000 + rtc.getYear();
  m = rtc.getMonth();
  d = rtc.getDay();
  w = (d += m < 3 ? y-- : y - 2, 23 * m / 9 + d + 4 + y / 4 - y / 100 + y / 400) % 7; // http://stackoverflow.com/a/21235587

  if (m == 3 && d >= 25) { // Daylight savings starts on the last Sunday of March
    item = 2;
    for (i = 0; i <= 5; i++) {
      if (d == 25 + i && w > 0 + i) {
        item = 1;
        break;
      }
    }
    return item;
  }
  else if (m == 10 && d >= 25) { // Daylight savings stops on the last Sunday of October
    item = 1;
    for (i = 0; i <= 5; i++) {
      if (d == 25 + i && w > 0 + i) {
        item = 2;
        break;
      }
    }
    return item;
  }
  else if (m >= 4 && m <= 9 || m == 10 && d < 25) {
    return 2;
  }
  else {
    return 1;
  }
}

void wakeupTime() {
  t = gsmAccess.getTime() + (DSTcheck() * 3600);
      
  /* Note that the RTCZero library doesn't roll over to a new date until 02:00 in the morning. https://github.com/arduino-libraries/RTCZero/issues/39
     I might change to another library at a later point (like the Seeed Arduino RTC library: https://github.com/Seeed-Studio/Seeed_Arduino_RTC), but
     since showing the "correct" date/time isn't essential for this project, the current implementation works just fine. */
  
  if (SMSwakeup != 99) { // A custom wake-up time is set through SMS-command
    if (rtc.getHours() > 23 && SMSwakeup < 2) {
      if (rtc.getHours() == 24 && SMSwakeup == 1) {
        sleepHour = 25;
      }
      else {
        sleepHour = (SMSwakeup + 48);
      }
    }
    else if (rtc.getHours() >= SMSwakeup) {
      sleepHour = (SMSwakeup + 24);
    }
    else if (rtc.getHours() < SMSwakeup) {
      sleepHour = SMSwakeup;
    }
  }
  else if (hour(t) >= 20 || hour(t) < 2) { // 8:00 the next day
    sleepHour = (8 + 24);
  }
  else if (hour(t) < 8) { // 8:00
    sleepHour = 8;
  }
  else if (hour(t) >= 8 && hour(t) < 20) { // 20:00
    sleepHour = 20;
  }

  sleepT = tmConvert_t(2000 + rtc.getYear(), rtc.getMonth(), rtc.getDay(), 0, 00, 00) + (sleepHour * 3600); // Use todays date, at midnight, as a base for the calcualtions
  sleepDur = sleepT - t;
  
  if (sleepDur <= 1800 && SMSwakeup == 99) { // If the sleep timer is close (within 30 minutes) let the device sleep until the next wakeup event (unless set through SMS command)
    sleepDur += 12 * 3600;
    sleepT += 12 * 3600;
  }
  SMSwakeup = 99; // Reset timer
  
  timeCreate(true);
  if (idleMode) {
    SMStxt += "Idle mode";
  }
  else {
    SMStxt += "Sleeping";
  }
  SMStxt += " until " + String(hour(sleepT)) + ":00 hours";
  if (rtc.getHours() < 24 && hour(sleepT) < rtc.getHours()) {
    SMStxt += " tomorrow";
  }
  SMStxt += ".\n\nBike Tracker battery level: " + String(currBattery * (4.2 / 1023.0)) + "V\n" + timeString;
  sendSMS();
}

/*************
 *   Setup   *
 *************/

void setup(void) {
  Wire.begin();
  Serial.begin(115200);

  // Reserve memory for strings
  compYear.reserve(6);
  compMonth.reserve(4);
  compDay.reserve(4);
  compHour.reserve(4);
  compMinute.reserve(4);
  compSecond.reserve(4);
  timeYear.reserve(6);
  timeMonth.reserve(4);
  timeDay.reserve(4);
  timeHour.reserve(4);
  timeMinute.reserve(4);
  timeSecond.reserve(4);
  timeString.reserve(22);
  SMSrec.reserve(162);
  SMStxt.reserve(162);

  SMStxt += "Initialising setup\n\nLatest reset reason:\n";
  for (byte i = 0; i < 8; i++) { // Reset bit info source: https://cdn.sparkfun.com/datasheets/Dev/Arduino/Boards/Atmel-42181-SAM-D21_Datasheet.pdf
    if (i == 7) {
      SMStxt += "Power button";
    }
    else if (pow(2, i + 1) == WatchDogCause) {
      SMStxt += String(ResetCause[i]);
      break;
    }
  }

  pinMode(int1Pin, INPUT_PULLUP);
  pinMode(int2Pin, INPUT);
  pinMode(WAKEUP_PIN, OUTPUT);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(WAKEUP_PIN, LOW);
  digitalWrite(OUTPUT_PIN, HIGH);

  LowPower.attachInterruptWakeup(int1Pin, WakeUp, CHANGE);

  SMStxt += "\n\nConnecting to GSM network";
  while (!connected) {
    if (gsmAccess.begin(PINNUMBER) == GSM_READY && (gprs.attachGPRS(GPRS_APN, GPRS_LOGIN, GPRS_PASSWORD) == GPRS_READY)) {
      connected = true;
    } else {
      delay(500);
    }
  }
  gsmAccess.noLowPowerMode(); // Low-power mode is too slow fore regular use
  SMStxt += "\nConnected";
  
  sendSMS(); //Sending initial SMS so that it doesn't become more than 160 characters

  SMStxt += "Initialising accelerometer";
  if (!myAcc.init()) {
    SMStxt += "\nADXL345 not connected!";
    sendSMS();
    while (1);
  }
  myAcc.setCorrFactors(-315.0, 300.0, -322.0, 282.0, 1025.0, 1570.0);
  myAcc.setDataRate(ADXL345_DATA_RATE_25);
  myAcc.setRange(ADXL345_RANGE_8G);                                             // Setting the range to anything finer than 8G breaks the z-axis
  attachInterrupt(digitalPinToInterrupt(int2Pin), in_activityISR, RISING);
  myAcc.setActivityParameters(ADXL345_AC_MODE, ADXL345_XYZ, 0.5);               // Sensitivity, in g
  myAcc.setInactivityParameters(ADXL345_AC_MODE, ADXL345_XYZ, 0.5, stillTimer); // Sensititivy, in g, and inactivity timer in seconds
  myAcc.setLinkBit(true);                                                       // If false - multiple activity interrupts can be triggered. If true - activity interrupt can only be triggered after an inactivity interrupt
  myAcc.setInterrupt(ADXL345_ACTIVITY, INT_PIN_2);
  myAcc.setInterrupt(ADXL345_INACTIVITY, INT_PIN_2);
  myAcc.setAutoSleep(true, ADXL345_WUP_FQ_1);

  SMStxt += "\nInitialising GPS";
  if (!myGNSS.begin()) {
    SMStxt += "\nGPS not detected at default I2C address. Please check wiring.";
    sendSMS();
    while (1);
  }
  myGNSS.setI2COutput(COM_TYPE_UBX);
  myGNSS.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
  if (myGNSS.powerSaveMode()) {
    SMStxt += "\nGPS low power mode enabled.";
  }
  else {
    SMStxt += "\nFailed to enable GPS low power mode.";
  }

  compileTime();

  SMStxt += "\nInitialising RTC";
  rtc.begin();
  startTime = rtcSet(); // Set the RTC and save the startup timestamp

  sleepTime(wakeInt); // Set initial time until deep sleep

  SMStxt += "\n\nBattery voltage:\n" + String(readBattery(true)) + "V";

  currBattery = readBattery(false);

  timeCreate(true);
  SMStxt += "\n\nSetup complete\n" + timeString;
  sendSMS();
}

/************
 *   Loop   *
 ************/

void loop() {
  Watchdog.enable(WatchDogTimer);

  if (rtc.getHours() + DSTcheck() >= sleepTh && rtc.getMinutes() >= sleepTm && rtc.getSeconds() >= sleepTs && !mvCheck && !GPSwake && !sleeping && !stopAll) { // Idle/Deep sleep check
    wakeupTime();
    mvCheck = false;
    if (!idleMode) {
      gsmAccess.lowPowerMode(); // Putting the GSM modem in low power mode causes too much instability to use it in any other instance than deep sleep
    }
    myGNSS.powerOff(sleepDur * 1000 + 600000); // Shutdown the GPS module and add 10 minutes to make sure the GPS doesn't wake up before it is time to exit deep sleep
    if (idleMode) {
      StartStop(true);
    }
    else {
      sleeping = true;
      delay(500);
      Watchdog.disable();
      delay(500);
      LowPower.deepSleep(sleepDur * 1000);
    }
  }
  else if (sleeping) {
    gsmAccess.noLowPowerMode();
    rtcSet(); // Set internal clock
    sleepTime(wakeInt); // Set deep sleep timer
    currBattery = readBattery(false);
    sleeping = false;
    bWarning = false;
    startStopMillis = 0;
    stopAll = false;
    timeCreate(true);
    if (in_activity) {
      SMStxt += "Waking up, ";
    }
    else {
      SMStxt += "Scheduled wake-up, ";
    }
    if (idleMode) {
      SMStxt += "from idle mode.";
    }
    else if (shutDown) {
      shutDown = false;
      SMStxt += "from shut-down mode.";
    }
    else {
      SMStxt += "from deep sleep mode.";
    }
    if (currBattery >= 850) {
      SMStxt += "\n\nBike Tracker battery level: " + String(currBattery * (4.2 / 1023.0)) + "V\n" + timeString;
      sendSMS();
    }
    else {
      SMStxt += "\n" + timeString;
      sendSMS();
    }
    GPSwakeUp();
  }

  if (sms.available()) { // SMS reciever
    SMSrec = "";
    sms.remoteNumber(senderNumber, 20);
    SMStxt += "Message received from:\n" + String(senderNumber);

    while ((c = sms.read()) != -1) {
      SMSrec += (char)c;
    }
    if (SMSrec.startsWith(PASSWORD)) {
      SMSrec.replace(PASSWORD, "");
    }
    else {
      SMSrec = "";
    }
    sms.flush();

    SMScommand();
  }

  if (startStopMillis != 0 && millis() - startStopMillis > (sleepDur * 1000) && !shutDown || startStopMillis != 0 && in_activity && !shutDown) { // Exit idle mode
    StartStop(false);
  }
  else if (startStopMillis != 0 && millis() - startStopMillis > 7200000 && shutDown) { // Automatically disable shut-down mode after 2 hours
    StartStop(false);
  }

  if (!stopAll && !sleeping) {
    if (in_activity) { // Accelerometer activity
      byte intSource = myAcc.readAndClearInterrupts();
      if (myAcc.checkInterrupt(intSource, ADXL345_ACTIVITY)) {
        if (initMoveMillis == 0) {
          initMoveMillis = millis();
          moveMillis = initMoveMillis;
        }
        accActive = true;
        stillMillis = 0;
      }
  
      if (myAcc.checkInterrupt(intSource, ADXL345_INACTIVITY)) {
        stillMillis = millis();
      }
  
      myAcc.readAndClearInterrupts();
      in_activity = false;
    }

    if (stillMillis != 0 && moveMillis - (initMoveMillis) < (stillTimer + 1) * 1000) { // If there was an early inactivity trigger, don't register as movement
      accActive = false;
    }
  
    if (accActive && millis() - moveMillis > moveTimer) {
      if (!mvCheck) {
        mvCheck = true;
        if (!GPSwake) {
          timeCreate(true);
          SMStxt += "Movement detected!\n" + timeString;
          sendSMS();
        }
        moveMillis = millis();
      }
      else if (millis() - initMoveMillis >= 90000 && (millis() - stillMillis < 10000) && !GPSwake) { // If an inactivity interrupt triggeres more than 10 seconds before the GPS trigger point, wait.
        GPSstart();
      }
    }
    if (stillMillis != 0 && millis() - stillMillis > 30000 - (stillTimer * 1000)) { // If everything has been quiet for a set time (in ms), confirm that everything is quiet
      if (!GPSwake) {
        timeCreate(true);
        SMStxt += "Everything's quiet again.\n" + timeString;
        sendSMS();
      }
      initMoveMillis = 0;
      moveMillis = 0;
      stillMillis = 0;
      mvCheck = false;
      accActive = false;
    }
  
    if (GPSsend) { // Send GPS data
      if (gpsMillis == 0) {
        gpsMillis = millis();
      }
      if (myGNSS.getLatitude() != 0 && myGNSS.getSIV() > 2) { // Only send GPS data if there's lat-long data and a connection to 3 or more satellites
        latitude = myGNSS.getLatitude();
        longitude = myGNSS.getLongitude();
        course = myGNSS.getHeading() / 100000;
        i = headingTxt(course);
        speed = myGNSS.getGroundSpeed();
        satellites = myGNSS.getSIV();
        
        timeCreate(false);
        SMStxt += "Bike Tracker GPS Location\n-------------------------\nGPS time: " + timeString + "\nCoordinates: " + String(latitude / 10000000, 7) + "," + String(longitude / 10000000, 7);
        sendSMS();
        SMStxt += "Heading: " + String(cmpsPoints[i]) + ", " + String(course) + " degrees" + "\nSpeed: " + String(speed * 0.0036, 2) + " km/h\n# of satellites: " + String(satellites) + "\nMap: https://www.google.com/maps/place/" + String(latitude / 10000000, 7) + "," + String(longitude / 10000000, 7);
        sendSMS();
        GPSrounds(true);
      }
      else if (millis() - gpsMillis > 60000) {
        if (!toCheck) {
          SMStxt += "GPS time-out. No location data available.";
          sendSMS();
          location.begin();
          toCheck = true;
        }
  
        if (toCheck) {
          if (location.available()) {
            latitude = location.latitude();
            longitude = location.longitude();
            accuracy = location.accuracy();
  
            if (accuracy < 10000) {
              timeCreate(true);
              SMStxt += "Bike Tracker GPRS Location\n-------------------------\nDevice time: " + timeString + "\nCoordinates: " + String(latitude, 7) + "," + String(longitude, 7);
              sendSMS();
              SMStxt += "Map: https://www.google.com/maps/place/" + String(latitude, 7) + "," + String(longitude, 7) + "\nAccuracy: " + String(accuracy) + " m";
              sendSMS();
              GPSrounds(true);
            }
            else {
              SMStxt += "GPRS location data too inaccurate.";
              sendSMS();
              GPSrounds(false);
            }
          }
          else if (millis() - gpsMillis > 60000) {
            SMStxt += "GPRS time-out. No location data available.";
            sendSMS();
            GPSrounds(false);
          }
        }
      }
    }
  
    if (readBattery(false) < 850 && !bWarning) { // Low battery warning (3.5V)
      SMStxt += "Warning!\nLow battery!\n" + String(readBattery(true)) + "V";
      sendSMS();
      bWarning = true;
    }
    if (readBattery(false) > 1000) { // Charging completed
      if (!bCharged) {
        bCharged = true;
        chargedMillis = millis();
      }
      if (millis() - chargedMillis > 180000 && chargedMillis != 0) { // Wait 3 minutes before sending charging completed
        SMStxt += "Battery charging complete.\nBattery level: " + String(readBattery(true)) + "V";
        sendSMS();
        chargedMillis = 0;
      }
    }
    else {
      bCharged = false;
    }
  }

  Watchdog.reset();
  delay(500);
}
