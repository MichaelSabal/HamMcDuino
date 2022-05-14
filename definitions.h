/*
 * HamMcDuino definitions
 */
#define PRG_BUTTON 0
#define BATTERY_PIN 37
#define BATTERY_ALT 13
// #define LED 25 // Defined in Heltec.h
#define TNC_OUT 26 // This is the only PWM / DAC pin on the Heltec board.
#define TNC_IN 27  // Pins 12,13,14,25,26,27 are connected to regular ADC.
#define PTT 14

#define HTTP_TIMEOUT 30
#define BATT_MAX_V    3700  //The default battery is 3700mv when the battery is fully charged.
#define NUM_MODES 7
#define BUFFER 100

uint16_t SQL = 1000;
uint16_t CWSPEED = 5;
uint16_t CWTONE = 600;

uint16_t MUL = 1000;
uint16_t MMUL = 100;
unsigned long last_button_change;
unsigned long last_battery_check;
unsigned long time_ctr;
double check_battery;

IPAddress myIP;
const String MODES[NUM_MODES] = {
  "TEST",
  "MCW",
  "APRS 1200",
  "APRS 9600",
  "WinLink",
  "RTTY",
  "MFSK-2K"
};

String APRSbuffer[BUFFER];
String WinLinkIn[BUFFER];
String WinLinkOut[BUFFER];
String WinLinkSent[BUFFER];
String MCW[BUFFER];
String RTTY[BUFFER];
String NBEMS[BUFFER];
String oneLineBuffer;

unsigned int aprs_msg = 0;
unsigned int winlink_in_msg = 0;
unsigned int winlink_out_msg = 0;
unsigned int winlink_sent_msg = 0;
unsigned int mcw_msg = 0;
unsigned int rtty_msg = 0;
unsigned int nbems_msg = 0;
unsigned int svc_mode = 0;
unsigned int display_set = 0;
WiFiServer http(80);
WiFiServer smtp(25);
