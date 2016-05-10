original project is here:
http://umoya-cottages.co.za/self-catering-guesthouse-automation-arduino/


#include <SPI.h>
#include <Ethernet.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SD.h>
#include <EEPROM.h>
#include <TimerThree.h>
#include <IRremote.h>


#define REQ_BUF_SZ   60    //size of buffer for HTTP resuest
#define InDoor_Bus   8    //pin for DS1820
#define IR_freq      9  //pin for 36kHz   (arduino mega, other aruinos may have other pin, see IRremote library)
#define Heater_pin   6    // pin for heater 
#define Light_pin    11    // pin for light 
#define PWM_pin      13    // pin for PWM 


#define EEPROM_setpoint 0  //addresses for storage values
#define EEPROM_heater   1
#define EEPROM_light    2
#define EEPROM_pwm      3
#define EEPROM_out1     4
#define EEPROM_out2     5
#define ir_on_len       25 //number of pulses ON command
#define ir_off_len       25 //number of pulses OFF command


byte mac[] = { 0x90, 0xA2, 0xDA, 0x0D, 0x42, 0x3E };
IPAddress ip(192,168,0,220); 
EthernetServer server(8085);  // create a server at port 8085
File webFile;               // the web page file on the SD card
char HTTP_req[REQ_BUF_SZ] = {0}; // buffered HTTP request stored as null terminated string
char num_str[7] = {0};            // buffer for storage of numerical parameters
char req_index = 0;              // pointer for HTTP_req 
boolean OUT_state[2] = {0}; // stores the states of OUTPUTs
boolean IN_state[2] = {0}; // stores the states of INPUTs
short current_temp;
int setpoint;                // setting point temperature
int seq;                      // sequence for connection trace
short pos,len;
char pwm,cycle;
char command,param;            // command and parameter in POST requests

//IR sequence for turning on\off Toshiba 
//sequence for any device can be obtained with IRrecord sketch from IRremote library
unsigned ir_on[25]={2300, 700, 1100, 650, 600, 600, 1150, 650, 600, 600, 1150, 650, 600, 600, 550, 700, 1100, 650, 550, 700, 550, 600, 600, 650, 550};  // durations of marks and spaces IR command
unsigned ir_off[25]={2300, 700, 1100, 650, 600, 600, 1150, 650, 600, 600, 1150, 650, 600, 600, 550, 700, 1100, 650, 550, 700, 550, 600, 600, 650, 550};   //

OneWire oneWire_int(InDoor_Bus);     //instance of oneWire for  DS1820
DallasTemperature DS1820_int(&oneWire_int);     //instance of DallasTemperature


IRsend irsend;          //instance of IRsend


void setup()
{
  

  
                              // disable Ethernet chip
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);
    
    Serial.begin(9600);       // for debugging////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
                                        // initialize SD card

    if (!SD.begin(4)) {
        Serial.println("ERROR - SD card initialization failed!");
        return;                                  // init failed
    }  

                                        // check for index.htm file
    if (!SD.exists("index.htm")) {
        Serial.println("ERROR - Can't find index.htm file!");
        return;                                // can't find index file
    }

    // switch on pin 2
    pinMode(2, INPUT);
    pinMode(A2, INPUT);
    pinMode(A3, INPUT);

    pinMode(3, OUTPUT);
    pinMode(5, OUTPUT);
    pinMode(6, OUTPUT);
    pinMode(7, OUTPUT);
    pinMode(8, OUTPUT);
    pinMode(9, OUTPUT);
    pinMode(10, OUTPUT);
    
    Ethernet.begin(mac, ip);          // initialize Ethernet device
    server.begin();                   // start to listen for clients
   
    setpoint=EEPROM.read(EEPROM_setpoint);  //read initial settings from EEPROM
    IN_state[0]=EEPROM.read(EEPROM_heater);
    IN_state[1]=EEPROM.read(EEPROM_light);
    pwm=EEPROM.read(EEPROM_pwm);
    analogWrite(PWM_pin,pwm);

}

void loop()
{
    DS1820_int.requestTemperatures();         // read DS1820 tempetarure every loop
    current_temp=DS1820_int.getTempCByIndex(0);
        if  ( (setpoint > current_temp) && (OUT_state[0]) ) digitalWrite(Heater_pin,HIGH);   //switch on heater if temperature lower than setpoint
        else digitalWrite(Heater_pin,LOW);


  
    EthernetClient client = server.available();  // try to get client

    if (client) {                    // got client?
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {           // client data available to read
                char c = client.read();           // read 1 byte (character) from client
                // limit the size of the stored received HTTP request
                // buffer first part of HTTP request in HTTP_req array (string)
                // leave last element in array as 0 to null terminate string (REQ_BUF_SZ - 1)
                if (req_index < (REQ_BUF_SZ - 1)) {
                    HTTP_req[req_index] = c;          // save HTTP request character
                    req_index++;
                }
                // last line of client request is blank and ends with \n
                // respond to client only after last line received
                if (c == '\n' && currentLineIsBlank) {
                                // send a standard http response header
                    
                                 
                    client.println("HTTP/1.1 200 OK");
                    // remainder of header follows below, depending on if
                    // web page or XML page is requested
                    // Ajax request - send XML file
                    if (StrContains(HTTP_req, "ajax_inputs") > -1)   // is request for current temperature and switches' state ?
                    {
                        // send rest of HTTP header
                        
                        client.println("Content-Type: text/xml");
                        client.println("Connection: keep-alive");
                        client.println();

                        // send XML file containing input states
                        XML_response(client);
                    } else if (StrContains(HTTP_req, "POST") > -1)    // is request for setpoint, IR, pwm setting ?
                           { pos=StrContains(HTTP_req,"?cmd=");
                             if (pos > -1) len=GetSubStr(pos);

                             command=num_str[0];                      //get command: u-setpoint, i-IR, w-PWM, s-set OUTPUT switches.
                          Serial.print("cmd=");      Serial.println(command);
                             pos=StrContains(HTTP_req,"?par=");
                             if (pos > -1) len=GetSubStr(pos);
                             param=char(GetNum(len));                    //get parameter of command
                         Serial.print("param=");      Serial.println(param+'0');
                                                        
                             switch (command )
                             {
                               case 'u':
                                 setpoint=param;
                                 if (EEPROM.read(EEPROM_setpoint) != setpoint) EEPROM.write(EEPROM_setpoint,setpoint);
                               break;
                               case 'i':
                                 if (param=1) {emit_on();} else {emit_off();}
                               break;
                               case 'w':
                                 pwm=param;
                                 analogWrite(PWM_pin,pwm);
                                 if (EEPROM.read(EEPROM_pwm) != setpoint) EEPROM.write(EEPROM_pwm,setpoint);
                               break;
                               case 's':
                               break;
                             }
                           }
                      
                      
                      else  
                 
                    {  //  initial web page request
                        
                        client.println("Content-Type: text/html");
                        client.println("Connection: keep-alive");
                        client.println();
                        // send web page
                        webFile = SD.open("index.htm");        // open web page file
                        if (webFile) {
                            while(webFile.available()) {
                                client.write(webFile.read()); // send web page to client
                            }
                            webFile.close();
                        }
                    }

                    // reset buffer index and all buffer elements to 0
                    req_index = 0;
                    StrClear(HTTP_req, REQ_BUF_SZ);
                    break;
                }
                // every line of text received from the client ends with \r\n
                if (c == '\n') {
                    // last character on line of received text
                    // starting new line with next character read
                    currentLineIsBlank = true;
                } 
                else if (c != '\r') {
                    // a text character was received from client
                    currentLineIsBlank = false;
                }
            } // end if (client.available())
        } // end while (client.connected())
        delay(1);      // give the web browser time to receive the data
        client.stop(); // close the connection
    } // end if (client)
}


//  sets OUTPUT switches as command prescribes
void SetOnOff(char set_on_off)
{
 
        set_on_off-='0';
        OUT_state[0] = set_on_off & 0xFE;  // save OUTPUT state for heater
        OUT_state[1] = (set_on_off & 0xFD) >>1 ;  // save OUTPUT state for heater;  

    if (EEPROM.read(EEPROM_out1) != OUT_state[0]) EEPROM.write(EEPROM_out1,OUT_state[0]);  //to save into EEPROM if state was changed

        digitalWrite(Light_pin, OUT_state[0]);
        digitalWrite(Heater_pin, OUT_state[1]);
   
    if (EEPROM.read(EEPROM_out2) != OUT_state[1]) EEPROM.write(EEPROM_out2,OUT_state[1]);
   
}

// send the XML file with current temperature, INPUT switches states and current setpoint

void XML_response(EthernetClient cl)
{   
 char p;

 p=StrContains(HTTP_req,"?seq=");
 if (p > -1) len=GetSubStr(p);
 
    cl.println("<?xml version = \"1.0\" ?>");
    cl.println("<inputs>");
    // read analog inputs
        cl.print("<sequence>");
        cl.print(num_str);
        cl.println("</sequence>");
        cl.print("<analog>");
        cl.print(String(current_temp));
        cl.println("</analog>");
        cl.print("<analog>");
        cl.print(String(setpoint));
        cl.println("</analog>");

    //heater and light outputs
        cl.print("<output>");
        if (OUT_state[0]) {cl.print("ON");} else {cl.print("OFF");}
        cl.println("</output>");
        cl.print("<output>");
        if (OUT_state[1]) {cl.print("ON");} else {cl.print("OFF");}
        cl.println("</output>");

    // read relay inputs
        cl.print("<switch>");
        if (digitalRead(A2)) {cl.print("ON");} else {cl.print("OFF");}
        cl.println("</switch>");
        cl.print("<switch>");
        if (digitalRead(A3)) {cl.print("ON");} else {cl.print("OFF");}
        cl.println("</switch>");

    cl.print("</inputs>");
}

// sets every element of str to 0 (clears array)
void StrClear(char *str, char length)
{
    for (int i = 0; i < length; i++) {
        str[i] = 0;
    }
}

//
short GetSubStr(short p)
{

  p+=5;

  short s=0;
  while (HTTP_req[p] !='@') {num_str[s]=HTTP_req[p];s++;p++;}
  num_str[s]='\0';

  return s;
  
}

//converts string value into number
int GetNum(char s)
{
  int number=0;
  for(char i=0,j=(s-1);i < s; i++,j--) {number+=int(num_str[j]-'0')*pow10(i);}
  return number;
}

//pow(10,n)
int pow10(char power)
{

 if (power==0) return 1;
 int  pw=1;
 for (char i=1;i<=power;i++) pw*=10;
 return pw;
}

//whether str-array contans sfind-array, if so - returns first position, else -1
short StrContains(char *str, char *sfind)
{
    short found = 0;
    char index = 0;
    char len;

    len = strlen(str);
    
    if (strlen(sfind) > len) {
        return 0;
    }
    while (index < len) {
        if (str[index] == sfind[found]) {
            found++;
            if (strlen(sfind) == found) {

              
                return (index-found+1);
            }
        }
        else {
            found = 0;
        }
        index++;
    }

    return -1;
}

//IR command transmission
void emit_on()
{
  
  Serial.println("ir_on");
   irsend.sendRaw(ir_on, 25, 38);
}

void emit_off()
{
    Serial.println("ir_on");
   irsend.sendRaw(ir_off, 25, 38);
}
