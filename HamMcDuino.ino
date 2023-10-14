/*
 * This project is built using the Heltec ESP32 Wifi Kit
 * It is intended for ARES/RACES as a TNC / MMC
 * The device supports
 * - Rechargable battery controller 
 * - PRG push button
 * - OLED monochrome display
 * - PWM output
 * - Analog input
 * - WiFi server modes
 *
 * The project requires some external circuitry in order to adapt the external signal (RX -1V to +1V; TX 0mv to 500mV)
 * to the Arduino's 0V to 5V range.
 * The (hopefully) supported audio modes are WinLink packet, APRS packet, RTTY, and MFSK-2K (NBEMS)
 * 
 * Step 1: Add the following URL to "Additional Boards" under File->Preferences:
 *      https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 * Step 2: Under Tools -> Board -> ESP32, select "Heltec ESP32 Wifi Kit"
 * 
 */

#include <Arduino.h>
#include "heltec.h"
#include <WiFi.h>

#include "definitions.h"

// These variables can either be changed here in the source to be loaded to a device on startup
// or changed via the web interface after the device has started.
char ssid[12]     = "RACES";   // The Wi-Fi network to create or attach to
char wifipw[32]   = "";        // The password associated with the Wi-Fi network
char mycall[12] = "MYCALL";    // The default sender callsign in all modes
char myloc[6]  = "";           // For modes that transmit location data, in Maidenhead format

void setup() {
  oneLineBuffer = "";
	pinMode(LED,OUTPUT);
  pinMode(TNC_IN,INPUT);
	digitalWrite(LED,LOW);
 	Heltec.begin(true /*DisplayEnable Enable*/, false /*LoRa Enable*/, true /*Serial Enable*/);
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(0, 0, "HamMcDuino");
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(0, 16, "by KB3GJT");
	Heltec.display->display();
  Heltec.display->setFont(ArialMT_Plain_16);
  sleep(5);
  pinMode(PRG_BUTTON, INPUT);
  last_button_change = millis();
  last_battery_check = last_button_change;
  time_ctr = last_button_change;
  check_battery = analogRead(BATTERY_PIN);
  check_battery = 2.03 * (-0.000000000000016 * pow(check_battery,4) + 0.000000000118171 * pow(check_battery,3)- 0.000000301211691 * pow(check_battery,2)+ 0.001109019271794 * check_battery + 0.034143524634089);
  WiFi.softAP(ssid);
  myIP = WiFi.softAPIP();
  http.begin();
  smtp.begin();
  Serial.begin(115200);
}

void loop() {
  // Button & Battery check
  if (time_ctr - last_battery_check > 2000 || time_ctr < last_battery_check) {
    check_battery = analogRead(BATTERY_PIN);
    check_battery = 2.08 * (-0.000000000000016 * pow(check_battery,4) + 0.000000000118171 * pow(check_battery,3)- 0.000000301211691 * pow(check_battery,2)+ 0.001109019271794 * check_battery + 0.034143524634089);
    last_battery_check = time_ctr;
  }
  int check_button = digitalRead(PRG_BUTTON);
  if (check_button == 0) {
    if (time_ctr - last_button_change > 500 || time_ctr < last_button_change) {
      svc_mode = (svc_mode + 1) % NUM_MODES;
      last_button_change = time_ctr;
    } 
  }

  // Check for HTTP traffic on WiFi
  WiFiClient http_cxn = http.available();
  if (http_cxn) {
    process_http(http_cxn);
  }

  // Check for SMTP traffic on WiFi
  WiFiClient smtp_cxn = smtp.available();
  if (smtp_cxn) {
    process_smtp(smtp_cxn);
  }

  // Check for Radio traffic, depending on mode
  int signal_in = analogRead(TNC_IN);
  if (signal_in > SQL) {
    process_radio();
  }
  
  if (winlink_in_msg > 0) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }

  // Process outbound Radio traffic.
  signal_in = analogRead(TNC_IN);
  if (signal_in < SQL) {
    if (svc_mode == 4 && winlink_out_msg > 0) {
      send_winlink();
    } else if (svc_mode != 0) {
      send_oneline();
    }
  }
  
  // Update the OLED display
  Heltec.display->clear();
  if (display_set == 0) {
    Heltec.display->drawString(0,10,"Battery: ");
    Heltec.display->drawString(60,10,((String)check_battery)+"V");
    Heltec.display->drawString(0,26,MODES[svc_mode]);
    Heltec.display->drawString(0,42,((String)myIP[0])+"."+((String)myIP[1])+"."+((String)myIP[2])+"."+((String)myIP[3]));
    Heltec.display->display();  
  } else if (display_set == 1) {
    Heltec.display->drawString(0,10,"TNC_IN: ");
    Heltec.display->drawString(70,10,((String)signal_in));
    Heltec.display->display();
  }
  time_ctr = millis();
} // loop()

void process_http(WiFiClient http_cxn) {
  String request = "";
  char c = http_cxn.read();
  bool started = false;
  long waiting = millis();
 
  while (c > 0) {
    if (c < 255) { 
      Serial.print(c);
      started = true;
    } else if (started) {
      break;
    } else {
      if (millis() - waiting >= HTTP_TIMEOUT) {
        break;
      }
    }
    if (c==13 || c==10) {
      String rr = request;
      rr.toUpperCase();
      if (rr.substring(0,3)=="GET") {
        break;
      }
    } else if (c < 255) {
      request += c;
    }
    c = http_cxn.read();
  }
  Serial.println("Request: "+request);

  String pagename = request;
  String query = "";
  pagename = pagename.substring(4);
  int cpos = pagename.indexOf(' ');
  if (cpos > 0) {
    pagename = pagename.substring(0,cpos);
  }
  cpos = pagename.indexOf('?');
  if (cpos > 0) {
    query = pagename;
    query = query.substring(cpos+1);
    pagename = pagename.substring(0,cpos);
  }

  Serial.println("Updated request: "+pagename);
  int response_page = 0;
  if (pagename=="/mcw") response_page = 1;
  if (pagename=="/aprs") response_page = 2;
  if (pagename=="/winlink") response_page = 3;
  if (pagename=="/mcwmsg") response_page = 4;
  String response = "";
  switch (response_page) {
    case 0: response = http_page_0(); break;
    case 1: response = http_page_1(); break;
    case 2: response = http_page_2(); break;
    case 3: response = http_page_3(); break;
    case 4: response = queue_mcw_msg(query); break;
  }
  http_cxn.println("HTTP/1.1 200 OK");
  http_cxn.println("Content-type:text/html");
  http_cxn.println();
  http_cxn.print(response);
  http_cxn.println();
  http_cxn.stop();
} // process_http()

String http_page_0() {
  String html = "<!DOCTYPE HTML><HTML><HEAD><TITLE>RACES HamMcDuino</TITLE><STYLE>body { font-size: 2.0em; }</STYLE><META charset=\"UTF-8\" /><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" /></HEAD><BODY>";
  html += "Welcome to the RACES HamMcDuino system.<BR />";
  html += "<B>Current status:</B>";
  html += "<UL><LI>Battery: "+(String)check_battery+"V</LI>";
  html += "<LI>SSID: "+(String)ssid+"</LI>";
  html += "<LI>IP: "+((String)myIP[0])+"."+((String)myIP[1])+"."+((String)myIP[2])+"."+((String)myIP[3])+"</LI>";
  html += "<LI>Current mode: "+MODES[svc_mode]+"</LI></UL>";
  html += "<BR /><B>Queues and services</B><BR />";
  html += "<A href=\"/mcw\">Modulated CW</A>: "+(String)mcw_msg+" messages<BR />";
  html += "<A href=\"/aprs\">APRS</A>: "+(String)aprs_msg+" messages<BR />";
  html += "<A href=\"/winlink\">WinLink</A>: Messages In ("+(String)winlink_in_msg+") Out ("+(String)winlink_out_msg+") Sent ("+(String)winlink_sent_msg+")<BR />";
  html += "</BODY></HTML>";
  return html;
}
String http_page_1() {
  String html = "<!DOCTYPE HTML><HTML><HEAD><TITLE>RACES HamMcDuino [MCW]</TITLE><STYLE>body { font-size: 2.0em; }</STYLE><META charset=\"UTF-8\" /><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" /></HEAD><BODY>";
  html += "<INPUT type=\"text\" style=\"width: 95vw;\" placeholder=\"Type your message here\" id=\"message\" /><BR /><BUTTON onClick='window.location.href=\"/mcwmsg?\"+encodeURIComponent(document.getElementById(\"message\").value);'>Send</BUTTON>";
  html += "</BODY></HTML>";
  if (mcw_msg==0) return html;
  int qlen = mcw_msg;
  if (mcw_msg==BUFFER-1) qlen = mcw_msg + 1;
  for (int i=qlen-1;i>=0;i--) {
    char tmp[MCW[i].length()+13];
    sprintf(tmp,"<P>%d: %s</P>",i,MCW[i]);
    html += String(tmp);
  }
  return html;
}
String http_page_2() {
  return "APRS";
}
String http_page_3() {
  return "WinLink";
}

String queue_mcw_msg(String querystring) {
  String cleanquery = "";
  String tmp = "";
  char tmp0x[5] = "0x00";
  for (int cptr=0;cptr<querystring.length();cptr++) {
    char c = querystring.charAt(cptr);
    if (tmp.length()==2) {
      tmp0x[0] = '0';
      tmp0x[1] = 'x';
      tmp0x[2] = tmp.charAt(1);
      tmp0x[3] = c;
      tmp0x[4] = 0;
      unsigned long cl = strtoul(tmp0x,NULL,16);
      c = (char)(cl & 0xff);
      cleanquery += c;
      tmp = "";
    } else if (tmp.length()==1) {
      tmp += c;
    } else if (c=='%') {
      tmp += c;
    } else if (c=='+') {
      cleanquery += ' ';
    } else {
      cleanquery += c;
    }
  }
  oneLineBuffer = oneLineBuffer + cleanquery;
  if (mcw_msg >= BUFFER - 1) {
    for (int i=0;i<BUFFER - 2; i++) {
      MCW[i] = MCW[i+1];
    }
    MCW[mcw_msg] = cleanquery;
  } else {
    MCW[mcw_msg++] = cleanquery;
  }
  return http_page_1();
}

void process_smtp(WiFiClient smtp_cxn) {
  bool timeout_or_quit = false;
  String request = "";
  String from = "";
  String to = "";
  char c = smtp_cxn.read();
  bool started = false;
  long waiting = millis();
  if (winlink_out_msg >= 100) {
    smtp_cxn.println("450 Message Queue is full");
    smtp_cxn.stop();
    return;
  }
  smtp_cxn.println("220 WinLink ESMTP");
  while (!timeout_or_quit) {
    while (c > 0) {
      if (c < 255) { 
        Serial.print(c);
        started = true;
      } else if (started) {
        break;
      } else {
        if (millis() - waiting >= HTTP_TIMEOUT) {
          timeout_or_quit = true;
          break;
        }
      }
      if (c==13 || c==10) {
        if (request.substring(0,4)=="HELO" || request.substring(0,4)=="EHLO") {
          smtp_cxn.println("250 Ready");
          Serial.println("HELLO: "+request);
          request = "";
        }
        if (request.substring(0,4)=="QUIT") {
          smtp_cxn.println("221 Bye");
          request = "";
          timeout_or_quit = true;
          break;
        }
        if (request.substring(0,4)=="MAIL") {
          if (request.substring(6,11)=="FROM:") {
            from = request.substring(11);
          } else {
            from = request.substring(5);
          }
          smtp_cxn.println("250 OK");
          Serial.println("FROM: "+request);
          request = "";
        }
        if (request.substring(0,4)=="RCPT") {
          if (request.substring(6,9)=="TO:") {
            to = request.substring(9);
          } else {
            to = request.substring(5);
          }
          smtp_cxn.println("250 OK");
          Serial.println("TO: "+request);
          request = "";
        }
        if (request.substring(0,4)=="DATA") {
          smtp_cxn.println("354 Ready");
          request = "";
          break;
        }
      } else if (c < 255) {
        request += c;
      }
      c = smtp_cxn.read();
    }
    if (timeout_or_quit) break;
    String endOfMessage = "\r\n.\r\n";
    while (!timeout_or_quit && c > 0 && c < 255) {
      c = smtp_cxn.read();
      if (c > 0 && c < 255) request += c;
      Serial.print(c);
      if (request.substring(request.length()-5)==endOfMessage) break;
    }
    smtp_cxn.println("250 OK");
    // TODO: Convert message to WinLink Format & QUEUE
    WinLinkOut[winlink_out_msg++] = 
      "From: "+from+"\r\n"+"To: "+to+"\r\n"+request.substring(request.length()-3);
    request = "";
  }
  smtp_cxn.stop();
} // process_smtp()

void process_radio() {
  int samples[20000];
  int rec = 0;
  long start = millis();
  while (millis() - start < 20) {
    samples[rec++] = analogRead(TNC_IN);
  }
  Serial.println(rec+" samples recorded");
  int highest = 0;
  int lowest = 4096;
  for (int ctr=0;ctr<rec;ctr++) {
    if (samples[ctr] > highest) highest = samples[ctr];
    if (samples[ctr] < lowest) lowest = samples[ctr];
  }
  Serial.println("Highest: "+highest);
  Serial.println("Lowest: "+lowest);
} // process_radio()

void send_winlink() {
  for (int i=0; i<winlink_out_msg; i++) {
    
  }
} // send_winlink()

void send_oneline() {
  if (oneLineBuffer.length()==0) return;
  switch(svc_mode) {
    case 0:
    case 4: break;
    case 1: send_mcw(); break;
    case 2: send_aprs1200(); break;
    case 3: send_aprs9600(); break;
    case 5: send_rtty(); break;
    case 6: send_mfsk2k(); break;
  }
} // send_oneline()
void playDot(int dotlen) {
    digitalWrite(LED,HIGH);
    ledcAttachPin(TNC_OUT,0);
    ledcWriteTone(0,CWTONE);
    delayMicroseconds(1000*dotlen);
    digitalWrite(LED,LOW);
    ledcDetachPin(TNC_OUT);
    ledcWriteTone(0,0);
    delayMicroseconds(1000*dotlen);
}
void playDash(int dotlen) {
    digitalWrite(LED,HIGH);
    ledcAttachPin(TNC_OUT,0);
    ledcWriteTone(0,CWTONE);
    delayMicroseconds(3000*dotlen);
    digitalWrite(LED,LOW);
    ledcDetachPin(TNC_OUT);
    ledcWriteTone(0,0);
    delayMicroseconds(1000*dotlen);
}
void send_mcw() {
  int dotlen = 1200/CWSPEED; // milliseconds
  char c = oneLineBuffer.charAt(0);
  String list;
  if (c<=32) {
    delayMicroseconds(3000*dotlen);
  }
  // First dot
  list = "eEaAiIsSuUrRwWhHvVfFlLpPjJ12345.+@?'&_\"$";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // First dash
  list = "tTnNmMdDkKgGoObBxXzZyYqQcC67890=/,!():;-";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Second dot
  list = "iIsSuUhHvVfF2345nNdDkKbBxXcCyY6=/?()!;-$_";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // Second dash
  list = "aArRwWlLpPjJ+1@mMgGoOzZqQ7890.,'&:\"";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Third dot
  list = "sShHvV543dDbBxX6=/rRlLgGzZqQ7.,&-\"$";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // Third dash
  list = "uUfF2wWpPjJ1kKcCyYoO890@?'!():;_";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Fourth dot
  list = "hHfF54@lLpPbBcCzZ678,!&:;-\"";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // Fourth dash
  list = "vVjJ123+xXyYqQ/90.?'()_$";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Fifth dot
  list = "56789+/.?()&:-_$";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // Fifth dash
  list = "12340@=,'!;\"";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Sixth dot
  list = "@?':;-\"$";
  if (list.indexOf(c)>=0) playDot(dotlen);
  // Sixth dash
  list = ".,!)_";
  if (list.indexOf(c)>=0) playDash(dotlen);
  // Seventh dash
  list = "$";
  if (list.indexOf(c)>=0) playDash(dotlen);
  
  oneLineBuffer = oneLineBuffer.substring(1);
  delayMicroseconds(2000*dotlen);
}
void send_aprs1200() {
  
}
void send_aprs9600() {
  
}
void send_rtty() {
  
}
void send_mfsk2k() {
  
}
