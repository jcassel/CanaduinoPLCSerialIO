//simple D1 mini Serial IO Driver
//Takes serial commands to control configured IO.
//Can create automations for interactions between inputs, outputs and actions. (Future)

//Eventually should support a closed loop temerature control. Need to have a sensor and a output to control a heating element. Could also do a fan / exaust system to cool enclosure down. 

//Target outputs 
//Digital/relay: PSU,Lighting,Enclosure heater, alarm, filter relay,exaust fan
//Digital (PWM):cooling fan(PWM*) (Future)

//Target inputs
//Digital(5v/3.3v): General switches(NO or NC Selectable)
//temperature inputs(Future + TBA on type)
//#define USEDEBOUNCE

#define VERSIONINFO "CanaduinoPLCSerialIO 1.0.3"
#define COMPATIBILITY "SIOPlugin 0.1.1"
#include "TimeRelease.h"
#include <Bounce2.h>
#include <EEPROM.h>


#define IO0 7     //(Input D1)
#define IO1 8     //(Input D2)
#define IO2 12   //(Input D3)
#define IO3 13   //(Input D4) BuiltIn LED (you should have been removed it from the nano board)

#define IO4 2     //(Output Relay1)
#define IO5 3     //(Output Relay2)
#define IO6 4     //(Output Relay3)
#define IO7 5     //(Output Relay4)
#define IO8 A2   //(Output Relay5)
#define IO9 A3   //(Output Relay6)


//Unconfigured/unused in this example (Not included in the report array.)
#define IO10 2     // pin2/D0  //cant be used for this... used as TX on the comm port
#define IO11 1     // pin1/D1  //cant be used for this... used as RX on the comm port
#define IO12 6     //(Output A1 [analog])
#define IO13 9     //(Output A2 [analog])
#define IO14 10   //(Output A3 [analog])
#define IO15 11   //(Output A4 [analog])
#define IO16 A0   //(Input A1 [analog])
#define IO17 A1   //(Input A2 [analog])
#define IO18 A6   //(Input A3 [analog])
#define IO19 A7   //(Input A4 [analog])
#define IO20 A4   //(i2c SDA)
#define IO21 A5   //(i2c SCL) 
//Arduino Documentation also warns that Any input that it floating is suseptable to random readings. Best way to solve this is to use a pull up(avalible as an internal) or down resister(external).


#define IOSize  10 //(0-9)

bool _debug = false;
int IOType[IOSize]{INPUT,INPUT,INPUT,INPUT,OUTPUT,OUTPUT,OUTPUT,OUTPUT,OUTPUT,OUTPUT}; //0-9
int IOMap[IOSize] {IO0,IO1,IO2,IO3,IO4,IO5,IO6,IO7,IO8,IO9};
int IO[IOSize];
Bounce Bnc[IOSize];
bool EventTriggeringEnabled = 1;

/*
Serial Commands supported

If a command is recognized it will return OK. 
Then execute the command. Any results of that command will come after.

Command: IO [a]:[b] is used to set an IO point on or off by index.
where [a] is the IO index and [b] is 1(HIGH) or 0(LOW)

Command: SI [n] is used to adjust the auto reporting timing in milliseconds.(500 min)
[n] is a range from 500 to 30000.

Command: EIO will pause Autoreporting if it is enabled.

Command: BIO will resume Autoreporting if it is enabled. Does not set Autoreporting true.

Command: debug [n] turns on or off serial debug messaging.
[n] is 1 for on and 0 for off.

Command: IOT will return the current IO Configuration.

Command: SE [n] will enable or disable event triggering.
If enabled when an input is triggered, an IO report will be imediatly triggered.
This is good for EStops and getting faster responses to input changes
[n] is 1 for on and 0 for off.

Command: GS will trigger an IO Report.

*/



void StoreIOConfig(){
  if(_debug){Serial.println("Storing IO Config.");}
  int cs = 0;
  for (int i=0;i<IOSize;i++){
    EEPROM.update(i+1,IOType[i]); //store IO type map in eeprom but only if they are different... Will make it last a little longer but unlikely to really matter.:) 
    cs += IOType[i];
    if(_debug){Serial.print("Writing to EE pos:");Serial.print(i);Serial.print(" type:");Serial.println(IOType[i]);Serial.print("Current CS:");Serial.println(cs);}
  }
  EEPROM.update(0,cs); 
  if(_debug){Serial.print("Final Simple Check Sum:");Serial.println(cs);}
}


void FetchIOConfig(){
  int cs = EEPROM.read(0); //specifics of number is completely random. 
  int tempCS = 0;
  int IOTTmp[IOSize];
  

  for (int i=0;i<IOSize;i++){ //starting at 2 to avoide reading in first 2 IO points.
    IOTTmp[i] = EEPROM.read(i+1);//retreve IO type map from eeprom. 
    tempCS += IOTTmp[i];
  }
  
  //if(_debug){
    Serial.print("tempCS: ");Serial.println(tempCS);
  //}
      
  if(cs == tempCS){
    //if(_debug){
      Serial.println("Using stored IO set");
      //}
    for (int i=0;i<IOSize;i++){
      IOType[i] = IOTTmp[i];
    }
    return;  
  }

  //if(_debug){
    Serial.print("ALERT: Can't trust stored IO config. Using defaults. ");Serial.println(tempCS);
   // }
}



bool isOutPut(int IOP){
  return IOType[IOP] == OUTPUT; 
}

void ConfigIO(){
  
  Serial.println("Setting IO");
  for (int i=0;i<IOSize;i++){
    if(IOType[i] == 0 ||IOType[i] == 2 || IOType[i] == 3){ //if it is an input
      pinMode(IOMap[i],IOType[i]);
      Bnc[i].attach(IOMap[i],IOType[i]);
      Bnc[i].interval(5);
    }else{
      pinMode(IOMap[i],IOType[i]);
      digitalWrite(IOMap[i],LOW);
    }
  }

}



TimeRelease IOReport;
TimeRelease IOTimer[9];
unsigned long reportInterval = 3000;



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(300);
  Serial.println(VERSIONINFO);
  Serial.println("Start Types");
  FetchIOConfig();
  ConfigIO();
  reportIOTypes();
  Serial.println("End Types");

  //need to get a baseline state for the inputs and outputs.
  for (int i=0;i<IOSize;i++){
    IO[i] = digitalRead(IOMap[i]);  
  }
  
  IOReport.set(100ul);
  Serial.println("RR"); //send ready for commands    
}


bool _pauseReporting = false;
bool ioChanged = false;

//*********************Start loop***************************//
void loop() {
  // put your main code here, to run repeatedly:
  checkSerial();
  if(!_pauseReporting){
    ioChanged = checkIO();
    reportIO(ioChanged);
    
  }
  
}
//*********************End loop***************************//

void reportIO(bool forceReport){
  if (IOReport.check()||forceReport){
    Serial.print("IO:");
    for (int i=0;i<IOSize;i++){
      if(IOType[i] == 1 ){ //if it is an output
        IO[i] = digitalRead(IOMap[i]);  
      }
      Serial.print(IO[i]);
    }
    Serial.println();
    IOReport.set(reportInterval);
    
  }
}

bool checkIO(){
  bool changed = false;
  
  for (int i=0;i<IOSize;i++){
    if(!isOutPut(i)){
      Bnc[i].update();
      if(Bnc[i].changed()){
       changed = true;
       IO[i]=Bnc[i].read();
       if(_debug){Serial.print("Output Changed: ");Serial.println(i);}
      }
    }else{
      //is the current state of this output not the same as it was on last report.
      //this really should not happen if the only way an output can be changed is through Serial commands.
      //the serial commands force a report after it takes action.
      if(IO[i] != digitalRead(IOMap[i])){
        if(_debug){Serial.print("Output Changed: ");Serial.println(i);}
        changed = true;
      }
    }
    
  }
    
  return changed;
}


void reportIOTypes(){
  for (int i=0;i<IOSize;i++){
    Serial.print(IOType[i]);
    //if(i<IOSize-1){Serial.print(";");}
  }
  Serial.println();
}

void checkSerial(){
  if (Serial.available()){
    
    String buf = Serial.readStringUntil('\n');
    buf.trim();
    if(_debug){Serial.print("buf:");Serial.println(buf);}
    int sepPos = buf.indexOf(" ");
    String command ="";
    String value = "";
    
    if(sepPos){
      command = buf.substring(0,sepPos);
      value = buf.substring(sepPos+1);;
      if(_debug){
        Serial.print("command:");Serial.print("[");Serial.print(command);Serial.println("]");
        Serial.print("value:");Serial.print("[");Serial.print(value);Serial.println("]");
      }
    }else{
      command = buf;
      if(_debug){
        Serial.print("command:");Serial.print("[");Serial.print(command);Serial.println("]");
      }
    }
    
    if(command == "BIO"){ 
      ack();
      _pauseReporting = false; //restarts IO reporting 
      return;
    }    
    else if(command == "EIO"){ //this is the command meant to test for good connection.
      ack();
      _pauseReporting = true; //stops all IO reporting so it does not get in the way of a good confimable response.
      //Serial.print("Version ");
      //Serial.println(VERSIONINFO);
      //Serial.print("COMPATIBILITY ");
      //Serial.println(COMPATIBILITY);
      return;
    }
    else if (command == "IC") { //io count.
      ack();
      Serial.print("IC:");
      Serial.println(IOSize);
      return;
    }
    
    else if(command =="debug"){
      ack();
      if(value == "1"){
        _debug = true;
        Serial.println("Serial debug On");
      }else{
        _debug=false;
        Serial.println("Serial debug Off");
      }
      return;
    }
    else if(command=="CIO"){ //set IO Configuration
      ack();
      if (validateNewIOConfig(value)){
        updateIOConfig(value);
      }
      return;
    }
    
    else if(command=="SIO"){
      ack();
      StoreIOConfig();
      return;
    }
    else if(command=="IOT"){
      ack();
      reportIOTypes();
      return;
    }
    
    //Set IO point high or low (only applies to IO set to output)
    else if(command =="IO" && value.length() > 0){
      ack();
      int IOPoint = value.substring(0,value.indexOf(" ")).toInt();
      int IOSet = value.substring(value.indexOf(" ")+1).toInt();
      if(_debug){
        Serial.print("IO #:");
        Serial.println(IOMap[IOPoint]);
        Serial.print("Set:");Serial.println(IOSet);
      }
      
      if(isOutPut(IOPoint)){
        if(IOSet == 1){
          digitalWrite(IOMap[IOPoint],HIGH);
        }else{
          digitalWrite(IOMap[IOPoint],LOW);
        }
      }else{
        Serial.println("ERROR: Attempt to set IO which is not an output");   
      }
      delay(200); // give it a moment to finish changing the 
      reportIO(true);
      return;
    }
    
    //Set AutoReporting Interval  
    else if(command =="SI" && value.length() > 0){
      ack();
      unsigned long newTime = 0;
      if(strToUnsignedLong(value,newTime)){ //will convert to a full long.
        if(newTime >=500){
          reportInterval = newTime;
          IOReport.clear();
          IOReport.set(reportInterval);
          if(_debug){
            Serial.print("Auto report timing changed to:");Serial.println(reportInterval);
          }
        }else{
          Serial.println("ERROR: minimum value 500");
        }
        return;
      }
      Serial.println("ERROR: bad format number out of range");
      return; 
    }
    //Enable event trigger reporting Mostly used for E-Stop
    else if(command == "SE" && value.length() > 0){
      ack();
      EventTriggeringEnabled = value.toInt();
      return;
    }

    //Get States 
    else if(command == "GS"){
      ack();
      reportIO(true);
      return; 
    }
    else{
      Serial.print("ERROR: Unrecognized command[");
      Serial.print(command);
      Serial.println("]");
    }
  }
}

void ack(){
  Serial.println("OK");
}

bool validateNewIOConfig(String ioConfig){
  
  if(ioConfig.length() != IOSize){
    if(_debug){Serial.println("IOConfig validation failed(Wrong len)");}
    return false;  
  }

  for (int i=0;i<IOSize;i++){
    int pointType = ioConfig.substring(i,i+1).toInt();
    if(pointType > 4){//cant be negative. we would have a bad parse on the number so no need to check negs
      if(_debug){
        Serial.println("IOConfig validation failed");Serial.print("Bad IO Point type: index[");Serial.print(i);Serial.print("] type[");Serial.print(pointType);Serial.println("]");
      }
      return false;
    }
  }
  if(_debug){Serial.println("IOConfig validation good");}
  return true; //seems its a good set of point Types.
}

void updateIOConfig(String newIOConfig){
  for (int i=2;i<IOSize;i++){//start at 2 to avoid D0 and D1
    int nIOC = newIOConfig.substring(i,i+1).toInt();
    if(IOType[i] != nIOC){
      IOType[i] = nIOC;
      if(nIOC == OUTPUT){
        digitalWrite(IOMap[i],LOW); //set outputs to low since they will be high coming from INPUT_PULLUP
      }
    }
  }
}

int getIOType(String typeName){
  if(typeName == "INPUT"){return 0;}
  if(typeName == "OUTPUT"){return 1;}
  if(typeName == "INPUT_PULLUP"){return 2;}
  if(typeName == "INPUT_PULLDOWN"){return 3;}
  if(typeName == "OUTPUT_OPEN_DRAIN"){return 4;} //not sure on this value have to double check
}

bool strToUnsignedLong(String& data, unsigned long& result) {
  data.trim();
  long tempResult = data.toInt();
  if (String(tempResult) != data) { // check toInt conversion
    // for very long numbers, will return garbage, non numbers returns 0
   // Serial.print(F("not a long: ")); Serial.println(data);
    return false;
  } else if (tempResult < 0) { //  OK check sign
   // Serial.print(F("not an unsigned long: ")); Serial.println(data);
    return false;
  } //else
  result = tempResult;
  return true;
}
