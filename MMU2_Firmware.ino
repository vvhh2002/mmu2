// CSK MMU2 Controller Version
//
//  Code developed by Chuck Kozlowski
//  September 19, 2018
//
//  Code was developed because I am impatiently waiting for my MMU2 to arrive so I thought
//  I would develop some code to operate the PRUSA MMU2 hardware
//
// This code uses 3 stepper motor controllers and 1 Pinda filament sensor
//
//  Work to be done:  Interface Control with the Einsy Board (MK3)
//                    Refine speed settings for each stepper motor
//                    Failure Recovery Modes - basically non-existent
//                    Uses the serial interface with a host computer at the moment - probably could do some smarter things
//                                                                                   like selection switches.
//

#include <SoftwareSerial.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#define SERIAL1ENABLED    1

#define MMU2_VERSION "2.23  9/23/18"

#define FW_VERSION 90             // config.h  (MM-control-01 firmware)
#define FW_BUILDNR 85             // config.h  (MM-control-01 firmware)

#define ORIGINALCODE 0            // code that is no longer needed for operational use
int command = 0;

#define MAXROLLERTRAVEL 125         // number of steps that the roller bearing stepper motor can travel

#define FULL_STEP  1
#define HALF_STEP  2
#define QUARTER_STEP 4
#define EIGTH_STEP 8
#define SIXTEENTH_STEP 16

#define STEPSIZE SIXTEENTH_STEP

#define STEPSPERREVOLUTION 200     // 200 steps per revolution  - 1.8 degree motors are being used

#define MMU2TOEXTRUDERSTEPS STEPSIZE*STEPSPERREVOLUTION*19   // for the 'T' command 

#define CW 0
#define CCW 1
#define INACTIVE 0
#define ACTIVE 1

#define CSDISTANCE 368

//#define POSITION1   0              // position for the color selector stepper motor 
//#define POSITION2   CSDISTANCE
//#define POSITION3   CSDISTANCE*2
//#define POSITION4   CSDISTANCE*3
//#define POSITION5   CSDISTANCE*4

//#define BEARINGSTEPSIZE 24         // steps to each roller bearing
float bearingAbsPos[5] = {1, 24, 48, 72, 96}; // absolute position of roller bearing stepper motor
                                                    // changed position #2 to 372  (still tuning this little sucker
                                                    //  
int selectorAbsPos[5] = {0,372,368*2,368*3,368*4};  // absolute position of selector stepper motor

int currentCSPosition = 0;         // color selector position
int currentPosition = 0;


int oldBearingPosition = 0;      // this tracks the roller bearing position (top motor on the MMU)
int filamentSelection = 0;       // keep track of filament selection (0,1,2,3,4))
int dummy[100];
char currentExtruder = "0";

int firstTimeFlag = 0;
int earlyCommands = 0;           // forcing communications with the mk3 at startup

char receivedChar;
boolean newData = false;
int rollerStatus = INACTIVE;
int colorSelectorStatus = INACTIVE;

//String kbString;

#define BEARINGMOTORDELAY 1000      // 2,000 useconds  (idler motor)
#define EXTRUDERMOTORDELAY 150     // 400 useconds    (controls filament feed speed to the printer)
#define COLORSELECTORMOTORDELAY 50 // 200 useconds


byte bearingDirPin = 9;
byte bearingStepPin = 8;
byte bearingSleepPin = 2;
//byte bearingRstPin = 3;

//byte M0Pin = 12;
//byte M1Pin = 11;
//byte M2Pin = 10;


byte extruderDirPin = 5;     //  pin 5 for extruder motor direction pin
byte extruderStepPin = 4;   //  pin 4 for extruder motor stepper motor pin
byte extruderSleepPin = 6;    //  pin 6 for extruder motor rst/sleep motor pin

byte colorSelectorDirPin = 3;    //color selector stepper motor (driven by trapezoidal screw)
byte colorSelectorStepPin = 7;
byte colorSelectorSleepPin = 12;



byte findaPin = 13;


//SoftwareSerial Serial1(10,11); // RX, TX (communicates with the MK3 controller board




void setup() {
  static int findaStatus;

  
  
  Serial.begin(115200);  // startup the local serial interface
  while (!Serial) {
    ; // wait for serial port to connect. needed for native USB port only 
    Serial.println("waiting for serial port"); 
  }
  
  Serial.println(MMU2_VERSION);

  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // THIS DELAY IS CRITICAL DURING POWER UP/RESET TO PROPERLY SYNC WITH THE MK3 CONTROLLER BOARD
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  delay(4000);                    // this is key to syncing to the MK3 controller - currently 4 seconds


  
  
  Serial1.begin(115200);         // startup the mk3 serial 
  // Serial1.begin(115200;              // ATMEGA hardware serial interface
  
  //Serial.println("started the mk3 serial interface");
  delay(100);

  
  Serial.println("Sending START command to mk3 controller board");
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // THIS NEXT COMMAND IS CRITICAL ... IT TELLS THE MK3 controller that an MMU is present
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  Serial1.print("start\n");                 // attempt to tell the mk3 that the mmu is present
  
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//  check the serial interface to see if it is active
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  while (!Serial1.available()) {

      delay(100); 
  }
  Serial.println("inbound message from mk3");
 

 

  pinMode(bearingDirPin, OUTPUT);
  pinMode(bearingStepPin, OUTPUT);

  pinMode(findaPin, INPUT);                        // pinda Filament sensor 
  pinMode(bearingSleepPin, OUTPUT);
  // pinMode(bearingRstPin, OUTPUT);

  pinMode(extruderSleepPin, OUTPUT);
  pinMode(extruderDirPin, OUTPUT);
  pinMode(extruderStepPin, OUTPUT);

  pinMode(colorSelectorSleepPin, OUTPUT);
  pinMode(colorSelectorDirPin, OUTPUT);
  pinMode(colorSelectorStepPin, OUTPUT);

  Serial.println("finished setting up input and output pins");

   
 
  
  digitalWrite(bearingSleepPin, HIGH);           // enable the roller bearing motor (motor #1)
  // digitalWrite(bearingRstPin, HIGH);

  digitalWrite(extruderSleepPin, HIGH);        //  enable the extruder motor  (motor #2)
  digitalWrite(colorSelectorSleepPin, HIGH);  // enable the color selector motor  (motor #3)





// moved these inits to the loop() section since the mk3 serial interface needs to be handled
//

  findaStatus = digitalRead(findaPin);    // check the pinda status ( DO NOT MOVE THE COLOR SELECTOR if filament is present)
  
  if (findaStatus == 0) {
     initColorSelectorPosition();   // reset the color selector if there is NO filament present
    
  } else {
    Serial.println("Filament is in the selector, will not move the selector");  
    Serial.println("Serious Problem, filament is present between the MMU2 and Extruder:  UNLOAD FILAMENT !!!");   
  }

   initBearingPosition();    // reset the roller bearing position

  

}



void loop() {
  int i;
  char rcvChar;
  int pindaStatus;
  char c1, c2, c3;
  String kbString;
  
  // Serial.println("looping");
  checkSerialInterface();           // check the serial interface for input commands from the mk3
  delay(100);                        // wait for 100 milliseconds


  // Serial.println("Enter Filament Selection (1-5),Disengage Roller (D), Load Filament (L), Unload Filament (U), Test Color Extruder(T)");


  // check for keyboard input

  if (Serial.available()) {
      Serial.print("Key was hit");
      //c1 = Serial.read();
      //c2 = Serial.read();
      //c3 = Serial.read();  
      
      kbString = Serial.readString();
      // Serial.print(c1); Serial.print(" "); Serial.println("c2");
      Serial.print(kbString);
      
     if (kbString[0] == 'C') {
        //if (c1 == 'C') {
        Serial.println("Processing 'C' Command");
        filamentLoadToExtruder();
      }
     if (kbString[0] == 'T') {
        //if (c1 == 'T') {
         Serial.println("Processing 'T' Command");
         toolChange(kbString[1]);                 // invoke the tool change command
         //toolChange(c2); 
      // processKeyboardInput();
      }
  }
  
  

}   // end of infinite loop



// need to check the PINDA status

void checkSerialInterface() {
  unsigned char c1, c2, c3;
  int i;
  int cnt;
  char c;
  String inputLine;
  int counter = 0;
  int findaStatus;
  int index;
  long steps;
  
  
   // Serial.println("Waiting for communication with mk3");

  // while (earlyCommands == 0) {
         // Serial.println("waiting for response from mk3");
   index = 0;
    if ((cnt = Serial1.available()) > 0) {
    
    //Serial.print("chars received: ");
    //Serial.println(cnt);
    
    inputLine = Serial1.readString();      // fetch the command from the mmu2 serial input interface

    if (inputLine[0] != 'P') {
         Serial.print("MMU Command: ");
         Serial.println(inputLine);
    }
process_more_commands:  // parse the inbound command 
    c1 = inputLine[index++];                      // fetch single characer from the input line
    c2 = inputLine[index++];                      // fetch 2nd character from the input line
    c3 = inputLine[index++];                      // carriage return


// process commands coming from the mk3 controller
//***********************************************************************************
// Commands still to be implemented:  X0 (MMU Reset), F0 (Filament type select), 
// E0->E4 (Eject Filament), R0 (recover from eject)
//***********************************************************************************
    switch (c1) {
        case 'T': 
                                               // request for idler and selector based on filament number
 
                  if ((c2 >= '0')  && (c2 <= '4')) {
                        toolChange(c2);

                   } else {
                       Serial.println("T: Invalid filament Selection");
                    }
                          
                   delay(200);
                   Serial1.print("ok\n");
                   break;
      case 'C':
                                        // move filament from selector ALL the way to printhead
#ifdef NOTDEF
                         Serial.println("C: Moving filament to Bondtech gears");
#endif
                         // filamentLoadToExtruder();
                         filamentLoadWithBondTechGear(); 
                         // delay(200);             
                         Serial1.print("ok\n");
                         break;

      case 'U':
                                            // request for filament unload
                 Serial.println("U: Filament Unload Selected");
                 if (rollerStatus == INACTIVE)
                       reActivateRollers();
                 if ((c2 >= '0') && (c2 <= '4')) {
                     unloadFilamentToFinda();
                     deActivateRollers();
                     Serial.println("U: Sending Filament Unload Acknowledge to MK3");
                     delay(200);
                     Serial1.print("ok\n");
                     
                 } else {
                    Serial.println("U: Invalid filament Unload Requested");
                    delay(200);
                    Serial1.print("ok\n");
                 }
                 break;
    case 'L':
                                                     // request for filament load
                 Serial.println("L: Filament Load Selected");
                 if (rollerStatus == INACTIVE)
                        reActivateRollers();

                  if (colorSelectorStatus == INACTIVE)
                       activateColorSelector();         // turn on the color selector motor

                  if ((c2 >= '0') && (c2 <= '4')) {
                          
                          Serial.println("L: Moving the bearing idler");
                          bearingSelector(c2);   // move the filament selector stepper motor to the right spot
                          Serial.println("L: Moving the color selector");
                          colorSelector(c2);     // move the color Selector stepper Motor to the right spot
                          Serial.println("L: Loading the Filament");
                          // loadFilament(CCW);
                          loadFilamentToFinda();
                          deActivateRollers();             // turn off the idler roller
                          
                          Serial.println("L: Sending Filament Load Acknowledge to MK3");

                          delay(200);

                          Serial1.print("ok\n");


                          
                  } else {
                        Serial.println("Error: Invalid Filament Number Selected");
                  }
                  break;

    case 'S':
                                                   // request for firmware version
        // Serial.println("S Command received from MK3");
                  // this is a serious hack since the serial interface is flaky at this point in time
#ifdef NOTDEF
                  if (command == 1) {
                     Serial.println("S: Processing S2");
                     Serial1.print(FW_BUILDNR);
                     Serial1.print("ok\n");

                     command++;

                  }
                  if (command == 0) {
                      Serial.println("S: Processing S1");
                      Serial1.print(FW_VERSION);
                      Serial1.print("ok\n");

                      command++;
                  }
#endif
               
        switch(c2) {
              case '0':
                    Serial.println("S: Sending back OK to MK3");
                    Serial1.print("ok\n");
                    break;
              case '1':
                    Serial.println("S: FW Version Request");
                    Serial1.print(FW_VERSION);
                    Serial1.print("ok\n");
                    break;
              case '2':
                    Serial.println("S: Build Number Request");
                    Serial.println("Initial Communication with MK3 Controller: Successful");
                    Serial1.print(FW_BUILDNR);
                    Serial1.print("ok\n");
                    break;
              default:
                    Serial.println("S: Unable to process S Command");
                    break;
        }  // switch(c2) check
        break;
     case 'P':
     
                                                            // check FINDA status
         // Serial.println("Check FINDA Status Request");
         findaStatus = digitalRead(findaPin);
         if (findaStatus == 0) {
              // Serial.println("P: FINDA INACTIVE");
              Serial1.print("0");
         }
         else {
              // Serial.println("P: FINDA ACTIVE");
              Serial1.print("1");
         }
         Serial1.print("ok\n");
     
         break;
     default:
         Serial.print("ERROR: unrecognized command from the MK3 controller");
         Serial1.print("ok\n");

         
     }  // end of switch statement 
       if (cnt != 3) {
            Serial.print("Index: ");
            Serial.print(index);
            Serial.print(" cnt: ");
            Serial.println(cnt);   
       }  
  }  // end of cnt > 0 check

    if (index < cnt) {
        Serial.println("More commands in the buffer");

        goto process_more_commands;
    }
  // }  // check for early commands
}


void colorSelector(char selection) {

  int findaStatus;


  // Serial.println("Entering colorSelector() routine");

loop:
  findaStatus = digitalRead(findaPin);    // check the pinda status ( DO NOT MOVE THE COLOR SELECTOR if filament is present)
  if (findaStatus == 1) {
    Serial.println("colorSelector(): Serious Problem, filament is present between the MMU2 and Extruder:  UNLOAD FILAMENT !!!");
    Serial.println("Clear the problem and then hit any key");
    
    while (!Serial.available()) {
      //  wait until key is entered to proceed
    }
    goto loop;
  }

  digitalWrite(colorSelectorSleepPin, HIGH );    // turn on the color selector motor


  switch (selection) {
    case '0':
      csTurnAmount(currentPosition, CCW);
      currentPosition = selectorAbsPos[0];
      break;
    case '1':
      if (currentPosition <= selectorAbsPos[1]) {
        csTurnAmount((selectorAbsPos[1] - currentPosition), CW);
      } else {
        csTurnAmount((currentPosition - selectorAbsPos[1]), CCW);
      }
      currentPosition = selectorAbsPos[1];
      break;
    case '2':
      if (currentPosition <= selectorAbsPos[2]) {
        csTurnAmount((selectorAbsPos[2] - currentPosition), CW);
      } else {
        csTurnAmount((currentPosition - selectorAbsPos[2]), CCW);

      }
      currentPosition = selectorAbsPos[2];
      break;
    case '3':
      if (currentPosition <= selectorAbsPos[3]) {
        csTurnAmount((selectorAbsPos[3] - currentPosition), CW);
      } else {
        csTurnAmount((currentPosition - selectorAbsPos[3]), CCW);

      }
      currentPosition = selectorAbsPos[3];
      break;
    case '4':
      if (currentPosition <= selectorAbsPos[4]) {
        csTurnAmount((selectorAbsPos[4] - currentPosition), CW);
      } else {
        csTurnAmount((currentPosition - selectorAbsPos[4]), CCW);

      }
      currentPosition = selectorAbsPos[4];
      break;

  }
  digitalWrite(colorSelectorSleepPin, LOW);    // turn off the color selector motor


}  // end of colorSelector routine()


// this is the motor with the lead screw (final stage of the MMU2 unit)

void csTurnAmount(int steps, int direction) {
  int i;

  if (direction == CW)
    digitalWrite(colorSelectorDirPin, LOW);      // set the direction for the Color Extruder Stepper Motor
  else
    digitalWrite(colorSelectorDirPin, HIGH);

  for (i = 0; i < steps * STEPSIZE; ++i) {
    digitalWrite(colorSelectorStepPin, HIGH);
    delayMicroseconds(10);               // delay for 10 useconds
    digitalWrite(colorSelectorStepPin, LOW);
    delayMicroseconds(10);               // delay for 10 useconds
    delayMicroseconds(COLORSELECTORMOTORDELAY);         // wait for 400 useconds

  }
}





// test code snippet for moving a stepper motor
//  (not used operationally)
void completeRevolution() {
  int i, delayValue;

  for (i = 0; i < STEPSPERREVOLUTION * STEPSIZE; i++) {
    digitalWrite(bearingStepPin, HIGH);
    delayMicroseconds(10);               // delay for 10 useconds
    digitalWrite(bearingStepPin, LOW);
    delayMicroseconds(10);               // delay for 10 useconds

    delayMicroseconds(BEARINGMOTORDELAY);
    //delayValue = 64/stepSize;
    //delay(delayValue);           // wait for 30 milliseconds

  }
}

//
// turn any of the three sepper motors
//
void bearingTurnAmount(int steps, int dir) {
  int i;
  int delayValue;


  digitalWrite(bearingSleepPin, HIGH);   // turn on motor 
  
  // digitalWrite(ledPin, HIGH);

  digitalWrite(bearingDirPin, dir);

  for (i = 0; i < steps * STEPSIZE; i++) {
    digitalWrite(bearingStepPin, HIGH);
    delayMicroseconds(10);               // delay for 10 useconds
    digitalWrite(bearingStepPin, LOW);
    delayMicroseconds(10);               // delay for 10 useconds

    delayMicroseconds(BEARINGMOTORDELAY);

  }
 // digitalWrite(bearingSleepPin, LOW);  // turn off motor 
  
  // digitalWrite(ledPin, LOW);
}

// turns on the extruder motor
void loadFilamentToFinda() {
  int i;
  int findaStatus;
  unsigned int steps;
  unsigned long startTime, currentTime;


       digitalWrite(extruderDirPin, CCW);  // set the direction of the MMU2 extruder motor

     startTime = millis();

loop:
      currentTime = millis();
      if ((currentTime-startTime) > 15000) {           // 15 seconds worth of trying to unload the filament
          Serial.println("loadFilamentToFinda() ERROR: Timeout occurred, Filament is not loading");
          return;         
      }
      feedFilament(1);        // 1 step and then check the pinda status

      findaStatus = digitalRead(findaPin);
      if (findaStatus == 0)              // keep feeding the filament until the pinda sensor triggers
          goto loop;
          
#ifdef NOTDEF
      Serial.println("Pinda Sensor Triggered during Filament Load");
#endif
      //
      // for a filament load ... need to get the filament out of the selector head !!!
      // 
      digitalWrite(extruderDirPin, CW);   // back the filament away from the selector
      steps = 200 * STEPSIZE + 50;       
      feedFilament(steps);  
#ifdef NOTDEF
      Serial.println("Loading Filament Complete ...");
#endif

  // digitalWrite(ledPin, LOW);     // turn off LED
}

// unload Filament using the FINDA sensor
// turns on the extruder motor
void unloadFilamentToFinda() {
  int i;
  int findaStatus;
  unsigned int steps;
  unsigned long startTime,currentTime;
  


     digitalWrite(extruderDirPin, CW);  // set the direction of the MMU2 extruder motor

     startTime = millis();        
     
loop:
                         
      currentTime = millis();
      if ((currentTime-startTime) > 15000) {           // 15 seconds worth of trying to unload the filament
          Serial.println("unloadFilamenttoFinda() ERROR: Timeout occurred, Filament is not unloading");
          Serial.println("unloadFilamenttoFinda(): Unload the filament manually, hit any key when done");
          while (!Serial.available()) {
              // wait until key is hit

          }
          startTime = millis();   // reset the start time      
      }
      
      feedFilament(1);        // 1 step and then check the pinda status

      findaStatus = digitalRead(findaPin);
      
      if (findaStatus == 1)              // keep feeding the filament until the pinda sensor triggers
          
          goto loop;
          
#ifdef NOTDEF
      Serial.println("unloadFilamenttoFinda(): Pinda Sensor Triggered during Filament unload");
#endif
      //
      // for a filament load ... need to get the filament out of the selector head !!!
      // 
      digitalWrite(extruderDirPin, CW);   // back the filament away from the selector
      steps = 200 * STEPSIZE + 40;       
      feedFilament(steps);    // 
#ifdef NOTDEF
      Serial.println("unloadFilamentToFinda(): Unloading Filament Complete ...");
#endif

  // digitalWrite(ledPin, LOW);     // turn off LED
}


void loadFilament(int direction) {
  int i;
  int findaStatus;
  unsigned int steps;


  // digitalWrite(ledPin, HIGH);          // turn on LED to indicate extruder motor is running
  digitalWrite(extruderDirPin, direction);  // set the direction of the MMU2 extruder motor


  switch (direction) {
    case CCW:                     // load filament
loop:
      feedFilament(1);        // 1 step and then check the pinda status

      findaStatus = digitalRead(findaPin);
      if (findaStatus == 0)              // keep feeding the filament until the pinda sensor triggers
        goto loop;
      Serial.println("Pinda Sensor Triggered");
      // now feed the filament ALL the way to the printer extruder assembly
      steps = 17 * 200 * STEPSIZE;

      Serial.print("steps: ");
      Serial.println(steps);
      feedFilament(steps);    // 17 complete revolutions
      Serial.println("Loading Filament Complete ...");
      break;

    case CW:                      // unload filament
loop1:
      feedFilament(1);            // 1 step and then check the pinda status
      findaStatus = digitalRead(findaPin);
      if (findaStatus == 1)        // wait for the filament to unload past the pinda sensor
        goto loop1;
      Serial.println("Pinda Sensor Triggered, unloading filament complete");
      // the +40 is a fudge factor to get the filament out of the way of the cutter
      feedFilament(200 * STEPSIZE + 40);  // turn for 1 more complete turn to get past the cutter head (21.2mm per revolution)

      break;
    default:
      Serial.println("loadFilament:  I shouldn't be here !!!!");


  }

  // digitalWrite(ledPin, LOW);     // turn off LED
}

//
// this routine feeds filament for 1 complete revolution - this represents 21.2mm of filament
//
void feedFilament(unsigned int steps) {

  int i;

#ifdef NOTDEF
  if (steps > 1) {
    Serial.print("Steps: ");
    Serial.println(steps);
  }
#endif  
  // for (i = 0; i < 200*STEPSIZE; i++) {
  for (i = 0; i <= steps; i++) {
    digitalWrite(extruderStepPin, HIGH);
    delayMicroseconds(10);               // delay for 400 useconds
    digitalWrite(extruderStepPin, LOW);
    delayMicroseconds(10);               // delay for 400 useconds
    // delayValue = 32/stepSize;
    //delayValue = 1;
    delayMicroseconds(EXTRUDERMOTORDELAY);         // wait for 400 useconds
    //delay(delayValue);           // wait for 30 milliseconds

  }
}


void recoverfilamentSelector() {

}

// this routine drives the 5 position bearings (aka idler) on the top of the MMU2 carriage
//
void bearingSelector(char filament) {
  int steps;
  int newBearingPosition;
  int newSetting;

#ifdef NOTDEF
  Serial.print("bearingSelector(): Filament Selected: ");
  Serial.println(filament);
#endif  

  if ((filament < '0') || (filament > '4')) {
       Serial.println("bearingSelector() ERROR, invalid filament selection");
       Serial.print("bearingSelector() filament: ");
       Serial.println(filament);
       return;
  }
  // move the selector back to it's origin state

#ifdef DEBUG
  Serial.print("Old Idler Roller Bearing Position:");
  Serial.println(oldBearingPosition);  
  Serial.println("Moving filament selector");
#endif

  switch (filament) {
    case '0':
      newBearingPosition = bearingAbsPos[0];                         // idler set to 1st position
      filamentSelection = 0; 
      currentExtruder = '0';   
      break;
    case '1':
      newBearingPosition = bearingAbsPos[1];
      filamentSelection = 1;
      currentExtruder = '1';
      break;
    case '2':
      newBearingPosition = bearingAbsPos[2];
      filamentSelection = 2;
      currentExtruder = '2';
      break;
    case '3':
      newBearingPosition = bearingAbsPos[3];
      filamentSelection = 3;
      currentExtruder = '3';
      break;
    case '4':
      newBearingPosition = bearingAbsPos[4];
      filamentSelection = 4;
      currentExtruder = '4';
      break;
    default:
      Serial.println("bearingSelector(): ERROR, Invalid Idler Bearing Position");
      break;
  }

  // turnAmount(newFilamentPosition,CCW);                        // new method


  newSetting = newBearingPosition - oldBearingPosition;
#ifdef NOTDEF
  Serial.print("Old Bearing Position: ");
  Serial.println(oldBearingPosition);
  Serial.print("New Bearing Position: ");
  Serial.println(newBearingPosition);

  Serial.print("New Setting: ");
  Serial.println(newSetting);
#endif

  if (newSetting < 0) {
    bearingTurnAmount(-newSetting, CW);                     // turn idler to appropriate position
  } else {
    bearingTurnAmount(newSetting, CCW);                     // turn idler to appropriate position
  }
  oldBearingPosition = newBearingPosition;

}


// perform this function only at power up/reset
//
void initBearingPosition() {
  Serial.println("initBearingPosition(): resetting the Idler Roller Bearing position");
  digitalWrite(bearingSleepPin, HIGH);   // turn on the roller bearing motor
  oldBearingPosition = 125;                // points to position #1
  bearingTurnAmount(MAXROLLERTRAVEL, CW);                 
  bearingTurnAmount(MAXROLLERTRAVEL, CCW);                // move the bearings out of the way
  digitalWrite(bearingSleepPin, LOW);   // turn off the idler roller bearing motor

   filamentSelection = 0;       // keep track of filament selection (0,1,2,3,4))
   currentExtruder = "0";


}

// perform this function only at power up/reset
//
void initColorSelectorPosition() {
  Serial.println("resettng the Color Selector position");
  digitalWrite(colorSelectorSleepPin, HIGH);   // turn on the stepper motor
  csTurnAmount(1900, CW);             // move to the right
  csTurnAmount(1920, CCW);        // move all the way to the left
  
  digitalWrite(colorSelectorSleepPin, LOW);   // turn off the stepper motor

}

// this just energizes the roller bearing extruder motor
//
void activateRollers() {

  digitalWrite(bearingSleepPin, HIGH);   // turn on the roller bearing stepper motor

  // turnAmount(120, CW);   // move the rollers to filament position #1
  // oldBearingPosition = 45;  // filament position #1

  // oldBearingPosition = MAXROLLERTRAVEL;   // not sure about this CSK

  rollerStatus = ACTIVE;
}

// move the filament Roller pulleys away from the filament

void deActivateRollers() {
  int newSetting;

  oldBearingPosition = bearingAbsPos[filamentSelection];          // fetch the bearing position based on the filament state
#ifdef NOTDEF
  Serial.print("oldBearingPosition: ");
  Serial.print(oldBearingPosition);
  Serial.print("   filamentSelection: ");
  Serial.println(filamentSelection);
#endif
  newSetting = MAXROLLERTRAVEL - oldBearingPosition;

#ifdef NOTDEF
  Serial.print("DeactiveRoller newSetting: ");
  Serial.println(newSetting);
#endif
  bearingTurnAmount(newSetting, CCW);     // move the bearing roller out of the way
  oldBearingPosition = MAXROLLERTRAVEL;   // record the current roller status  (CSK)
  rollerStatus = INACTIVE;
  digitalWrite(bearingSleepPin, LOW);    // turn off the roller bearing stepper motor  (nice to do, cuts down on CURRENT utilization)

}


// turn on the bearing rollers

void reActivateRollers() {
  int rollerSetting;

  digitalWrite(bearingSleepPin, HIGH);   // turn on the roller bearing motor

  Serial.println("Activating the Bearing Rollers");

  rollerSetting = MAXROLLERTRAVEL - bearingAbsPos[filamentSelection];

  Serial.print("reActivateRollers() Roller Setting: ");
  Serial.println(rollerSetting);

  bearingTurnAmount(rollerSetting, CW);    // restore the old position

}

void deActivateColorSelector() {
  int newSetting;

  digitalWrite(colorSelectorSleepPin, LOW);    // turn off the color selector stepper motor  (nice to do, cuts down on CURRENT utilization)
  colorSelectorStatus = INACTIVE;
}

void activateColorSelector() {
  digitalWrite(colorSelectorSleepPin, HIGH);
  colorSelectorStatus = ACTIVE;
}




void recvOneChar() {
  if (Serial.available() > 0) {
    receivedChar = Serial.read();
    newData = true;
  }
}

void showNewData() {
  if (newData == true) {
    Serial.print("This just in ... ");
    Serial.println(receivedChar);
    newData = false;
  }
}

#ifdef ORIGINALCODE

void processKeyboardInput() {

  
  while (newData == false) {
    recvOneChar();
  }

  showNewData();      // character received

  Serial.print("Filament Selected: ");
  Serial.println(receivedChar);

  switch (receivedChar) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
      if (rollerStatus == INACTIVE)
        activateRollers();

      if (colorSelectorStatus == INACTIVE)
        activateColorSelector();         // turn on the color selector motor


      bearingSelector(receivedChar);   // move the filament selector stepper motor to the right spot
      colorSelector(receivedChar);     // move the color Selector stepper Motor to the right spot

      break;
    case 'd':                             // de-active the bearing roller stepper motor and color selector stepper motor
    case 'D':
      deActivateRollers();
      deActivateColorSelector();
      break;
    case 'l':                            // start the load process for the filament
    case 'L':
      // reActivateRollers();
      if (rollerStatus == INACTIVE)
        reActivateRollers();
      loadFilament(CCW);
      deActivateRollers();          // move the bearing rollers out of the way after a load is complete
      break;
    case 'u':                           // unload the filament from the MMU2 device
    case 'U':
      reActivateRollers();           // working on this command
      loadFilament(CW);
      deActivateRollers();         // after the unload of the filament, move the bearing rollers out of the way
      break;
    case 't':
    case 'T':
      csTurnAmount(200, CW);
      delay(1000);
      csTurnAmount(200, CCW);
      break;
    default:
      Serial.println("Invalid Serial Output Selection");
  } // end of switch statement
}
#endif

void filamentLoadToExtruder() {
     float fsteps;
     unsigned int steps;
     int findaStatus;
                        
                        if ((currentExtruder < '0')  || (currentExtruder > '4')) {
                            Serial.println("filamentLoadToExtruder(): fixing current extruder variable");
                            currentExtruder = '0';
                        }
#ifdef DEBUG
                        Serial.println("Attempting to move Filament to Print Head Extruder Bondtech Gears");
                        //reActivateRollers();
                        Serial.print("filamentLoadToExtruder():  currentExtruder: ");
                        Serial.println(currentExtruder);
#endif
                        bearingSelector(currentExtruder);        // this was not done properly before
                        
                        deActivateColorSelector();
                        //if (rollerStatus == INACTIVE) {
                           // activateRollers();
                        //}
loop:
                        digitalWrite(extruderDirPin, CCW);  
                        feedFilament(1);        // 1 step and then check the pinda status

                        findaStatus = digitalRead(findaPin);
                        if (findaStatus == 0)              // keep feeding the filament until the pinda sensor triggers
                        goto loop;
#ifdef DEBUG
                        Serial.println("filamentLoadToExtruder(): Pinda Sensor Triggered during Filament Load");
#endif
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//  STEPS FROM MMU2 to EXTRUDER HEADER (bondtech gear)
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                        fsteps = 200 * STEPSIZE * 18.7
                        ;    // modifed from 19.5 to 20.0 after additional testing 
#ifdef DEBUG  
                        Serial.println("filamentLoadToExtruder(): steps: ");
                        Serial.println(steps);   
#endif
                        steps = fsteps;
                        feedFilament(steps);    // 
                        deActivateRollers();    // turn OFF the idler rollers when filament is loaded
#ifdef DEBUG                        
                        Serial.println("filamentLoadToExtruder(): Loading Filament to Print Head Complete");
#endif
                        delay(200);
                        Serial1.print("ok\n");    // send back acknowledge to the mk3 controller
                        
}

int isFilamentLoaded() {
    int findaStatus;
    
          findaStatus = digitalRead(findaPin);
          return(findaStatus);
}

// (T) Tool Change Command
// 
void toolChange(char selection) {
     int newExtruder;
                            
                          newExtruder = selection - 0x30;                // convert ASCII to a number (0-4)
                        
                          if (newExtruder == filamentSelection) {  // already at the correct filament selection

                               if (!isFilamentLoaded) {            // no filament loaded

                                     bearingSelector(selection);   // move the filament selector stepper motor to the right spot
                                     colorSelector(selection);     // move the color Selector stepper Motor to the right spot
                                     filamentLoadToExtruder();
                                     //loadFilamentToFinda();
                               }
//                               else {                           // added on 9.24.18 to
//                                     Serial.println("Filament already loaded, unloading the filament");
//                                     bearingSelector(selection);
//                                     unloadFilamentToFinda();
//                               }
                                
                          }  else {                                 // different filament position    

                                if (isFilamentLoaded) {

                                      bearingSelector(currentExtruder);  
                                      unloadFilamentToFinda();          // have to unload the filament first
                                }  

                               // a little tricky since I need to select the proper idler bearing
                               bearingSelector(selection);                                                    
                               colorSelector(selection);
                               filamentLoadToExtruder();
                               //loadFilamentToFinda();

                               filamentSelection = newExtruder;
                               currentExtruder = selection;
                               
                            }
                            // filamentLoadToExtruder();              // load the filament ALL the way to the bondtech gear
                            
}  // end of ToolChange processing

// part of the 'C' command,  does the last little bit to load into the past the extruder gear
 void filamentLoadWithBondTechGear() {
     int findaStatus;
     long steps;
     int i;
     
                          findaStatus = digitalRead(findaPin);
                          
                          if (findaStatus == 0) {
                               Serial.println("C:  Error, filament sensor thinks there is no filament");
                               return;
                          }
  
                          if ((currentExtruder < '0')  || (currentExtruder > '4')) {
                            Serial.println("filamentLoadWithBondTechGear(): fixing current extruder variable");
                            currentExtruder = '0';
                        }

                        bearingSelector(currentExtruder);        // this was not done properly before                        
                        deActivateColorSelector();

                        // probably should slow down the extruder motor in the MMU2 to feed the bondtech better
                        //
#ifdef NOTDEF                       
                        //Serial.print("ok\n");       // tell the mk3 to proceed (placment of this is critical)
                        // need to push more filament down past the bondtech gears 
                        //
                        
                        steps = 250 * STEPSIZE;    //single revolution the extruder motor for now
                        
                        Serial.println("C:  steps: ");
                        Serial.println(steps);   

                        //feedFilament(steps); 
                          // feed the filament ... slowly while the bondtech gear grab it 
                          // 
                          for (i = 0; i <= steps; i++) {
                                        digitalWrite(extruderStepPin, HIGH);
                                        delayMicroseconds(10);               // delay for 10 useconds
                                        digitalWrite(extruderStepPin, LOW);
                                        delayMicroseconds(10);               // delay for 10 useconds

                                        delayMicroseconds(EXTRUDERMOTORDELAY*5);         // slow down the extruder 
                          }
#endif
                         //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                         // copied from the MM-control-01/blob/master/motion.cpp routine
                         //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! 
                        Serial.println("C:  320+450 steps");   
                        for (i = 0; i <= 320; i++) {
                                        digitalWrite(extruderStepPin, HIGH);
                                        delayMicroseconds(10);               // delay for 10 useconds
                                        digitalWrite(extruderStepPin, LOW);
                                        delayMicroseconds(2600);
                                        
                        }
                        for (i = 0; i <= 450; i++) {
                                        digitalWrite(extruderStepPin, HIGH);
                                        delayMicroseconds(10);               // delay for 10 useconds
                                        digitalWrite(extruderStepPin, LOW);
                                        delayMicroseconds(2200);                             
                        }

 
                        deActivateRollers();    // turn OFF the idler rollers when filament is loaded
#ifdef DEBUG                        
                        Serial.println("filamentLoadToExtruder(): Loading Filament to Print Head Complete");
#endif
                       // delay(200);
                        Serial1.print("ok\n");    // send back acknowledge to the mk3 controller
                        
  
 } // end of filamentLoad with BondTechGear() routine