/*
  Door Handling Routines for Wiegand controller
  V. 1.0 Stefan Thesen, 2014 - 1st version released
  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry
                               & external alarm, if too many wrong codes entered. 
  V. 1.2 Stefan Thesen, 2015 - adaptations for 
                                 telnet 2 serial bridge, 
                                 support for usual Arduino relay boards (active = low), 
                                 support of new eQ-3 version of lock-drive
                                
  Copyright: public domain -> do what you want
*/

//////////
// Globals
//////////

//#define EQ3LOCK 1                    // enable this define for support of the new Keymatic eQ-3 lock

#ifdef EQ3LOCK
#define PINOPENCLOSEACTIVE 0         // pin low triggers lock
#define PINRFACTIVE_MS 100           // time for RF open/close commands --> use this for new eQ-3 version
#else
#define PINOPENCLOSEACTIVE 1         // pin high triggers lock
#define PINRFACTIVE_MS 3000          // time for RF open/close commands --> use this for old version
#endif

#define PINOPEN 12                   // output pin for open; do not use pin 13! - this one goes high on boot for ~ 1 sec
#define PINCLOSE 11                  // output pin for close
#define PINLED 10                    // output pin for keypad led
#define PINBUZZ 9                    // output pin for keypad buzzer
#define PINRING 8                    // output pin for door bell
#define PINRFPOWER 7                 // output pin for RF power
#define PINWRONGCODE 6               // output pin for signaling too many wrong codes to outside
#define PINRINGACTIVE_MS 300         // time for ring to be active
#define PINRINGACTIVE HIGH           // HIGH for the schematics posted in blog; LOW for typical Arduino Relay Boards

#define PUNISHWRONGCODES 5           // after this amount of wrong codes, sleep for punish time
#define PUNISHTIME_SEC 3*60UL        // sleep this time if punish, this is also the time window for wrong codes to count up

#define MINRAMBYTES 200              // min amount of bytes to keep free in RAM for stack
#define MAXEEPROMBYTES 1024          // max bytes that the EEProm can take

// struct / array for code storage
#define MAXNAMESIZE 10               // how many bytes for storing a name in codelist - keep mem in mind and term character!
struct CODELIST
{
char sName[MAXNAMESIZE];
unsigned long ulFacilityCode;
unsigned long ulCardCode;
byte bAction;
};
CODELIST *pCodeList ;                // struct to store all known codes
int iCodeListSize;                   // number of known codes
int iMaxCodeList;                    // maximum number of codes that can be stored (keep RAM & eeprom in mind)

unsigned long ulLastWrongCode_ms;    // last time that a wrong code was entered
int iWrongCodeCount;                 // wrong code entries in succession


////////////////////
// estimate free RAM
////////////////////
int freeRAM() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}


////////////////////////////
// load codelist from eeprom
////////////////////////////
void LoadFromEEProm()
{
  int bytes, addr=0;
  addr+=sizeof(byte); // skip tamper-flag
  eeprom_read_block((void*)&iCodeListSize, (void*)addr, sizeof(iCodeListSize)); addr+=sizeof(iCodeListSize);
  if (iCodeListSize>iMaxCodeList) {iCodeListSize=0;}  // reset if invalid size
  if (iCodeListSize<0)            {iCodeListSize=0;}  // reset if invalid size
  if (iCodeListSize>0)
  {
    eeprom_read_block((void*)pCodeList, (void*)addr, sizeof(CODELIST)*iCodeListSize);
  }
  Serial.print(F("Codes loaded from EEProm: "));
  Serial.println(iCodeListSize);
}


//////////////////////////
// save codelist to eeprom
//////////////////////////
void SaveToEEProm()
{
  int addr=0;
  byte bTamperFlag = 0;
  
  eeprom_write_block((const void*)&bTamperFlag  , (void*)addr, sizeof(bTamperFlag));   addr+=sizeof(bTamperFlag);
  eeprom_write_block((const void*)&iCodeListSize, (void*)addr, sizeof(iCodeListSize)); addr+=sizeof(iCodeListSize);

  if (iCodeListSize>0)
  {
    eeprom_write_block((const void*)pCodeList, (void*)addr, sizeof(CODELIST)*iCodeListSize);
  }
  Serial.print(F("Codes saved to EEProm: "));
  Serial.println(iCodeListSize);
}


////////////////////////////////////////////////////////////////////////////////
// sets the tamper-flag - avoid getting around wrong code handling / punish time
// by removing power / reboot
////////////////////////////////////////////////////////////////////////////////
void SetTamperFlag(bool bOn)
{
  if (GetTamperFlag()!=bOn)  // only, if there is a difference to EEProm
  {
    byte bTamperFlag = 0x0;
    if (bOn) 
    {
      bTamperFlag = 0xff;
    }
  
    eeprom_write_block((const void*)&bTamperFlag  , (void*)0, sizeof(bTamperFlag));
  }
}


////////////////////////////////////////////////////////////////
// gets status of tamper flag; we check for more than one bit to 
//    even be save against single bit failures of EEProm
////////////////////////////////////////////////////////////////
bool GetTamperFlag()
{
  byte bTamperFlag;
  eeprom_read_block((void*)&bTamperFlag, (void*)0, sizeof(bTamperFlag)); 
  
  // counts bits set
  byte bCount = 0;
  while(bTamperFlag)
  {
    bCount += bTamperFlag & 1;
    bTamperFlag >>= 1;
  }

  if (bCount>4) // more than 4 bits = tamper on
  {
    return(true);
  }
  
  return(false);
}


//////////////////////////////////////////////////
// pause with millis
// avoid delay to avoid issues with communications
// we still block everything!
//////////////////////////////////////////////////
void pause(unsigned long ulinterval)
{
  unsigned long ulend = millis() + ulinterval;
  while (millis()<ulend)
  {}
}


//////////////////////////////////
// setup outputs for door handling
//////////////////////////////////
void SetupCodeHandling()
{
  // define pins and set defaults
  pinMode(PINLED, OUTPUT);        // pin low is active
  digitalWrite(PINLED, HIGH);
  pinMode(PINBUZZ, OUTPUT);       // pin low is active
  digitalWrite(PINBUZZ, HIGH);
  pinMode(PINCLOSE, OUTPUT); 
  digitalWrite(PINCLOSE, !PINOPENCLOSEACTIVE);
  pinMode(PINOPEN, OUTPUT);       
  digitalWrite(PINOPEN, !PINOPENCLOSEACTIVE);
  pinMode(PINRFPOWER, OUTPUT);    // pin high is active
  digitalWrite(PINRFPOWER, LOW);
  pinMode(PINRING, OUTPUT);       // pin high is defined
  digitalWrite(PINRING, !PINRINGACTIVE);
  pinMode(PINWRONGCODE, OUTPUT);  // pin high is active
  digitalWrite(PINWRONGCODE, LOW);
  
  ulLastWrongCode_ms = 0; 
  iWrongCodeCount=0;
  
  // prepare codelist array - check RAM and EEProm conditions
  int ifr=freeRAM();
  Serial.print(F("RAM before Codelist allocation: "));
  Serial.println(ifr);
  iMaxCodeList= (ifr-MINRAMBYTES) / sizeof(CODELIST);  // allocate RAM for codelist, but leave MINRAM bytes for stack
  if ((MAXEEPROMBYTES-sizeof(iCodeListSize))<iMaxCodeList*sizeof(CODELIST))  // does this fit into EEProm? - subtract iCodeListSize as this is first in EEProm
  {
    // no, so EEProm is limiting
    iMaxCodeList= (MAXEEPROMBYTES-sizeof(iCodeListSize)) / sizeof(CODELIST);
    Serial.println(F("EEProm size limits Codelist array."));
  }
  pCodeList=(CODELIST*)calloc(iMaxCodeList,sizeof(CODELIST));
  Serial.print(F("RAM after Codelist allocation: "));
  Serial.println(freeRAM());
  Serial.print(F("Codes that can be stored: "));
  Serial.println(iMaxCodeList);
  iCodeListSize = 0;
  LoadFromEEProm();  // load codes from EEProm
  
  // check if tamper flag set in eeprom
  if(GetTamperFlag())
  {
    // re-enter punish mode
    Serial.println(F("TAMPERED?? - Restart with wrong-code counter active -> Go into wait mode."));
    iWrongCodeCount=PUNISHWRONGCODES;
    ulLastWrongCode_ms = millis(); 
    DoFail();
  }
}


///////////////////////////////////////////////////////////
// handle a code that has been received and process further
///////////////////////////////////////////////////////////
unsigned long HandleCode(unsigned long ulFacilityCode, unsigned long ulCardCode)
{
  // is it a ring? - check for sebury proprietary format; cardcode may contain different values
  if ( (ulCardCode==6 || ulCardCode==7 ) && WiegandBitCount()==18)
  {
    DoRing();
    return(0);
  }
  
  // try to find this code in our list
  int ii=0;
  bool bFound=false;
  while (ii<iCodeListSize && !bFound)
  {
    if (pCodeList[ii].ulCardCode==ulCardCode)
    {
      if (pCodeList[ii].ulFacilityCode==ulFacilityCode) { bFound=true; }
    }
    
    if (!bFound) {ii++;}
  }
  
  if (bFound) 
  { 
    return(DoDoor(ii));
  }
  
  DoFail(); 
  return(0);
}


///////////////////////////////////////////
// code authenticated - log and act on door
///////////////////////////////////////////
unsigned long DoDoor(int iCodeListEntry)
{
  Serial.print(pCodeList[iCodeListEntry].sName);
  Serial.print(F(" authenticated. Door activity "));
  Serial.print(pCodeList[iCodeListEntry].bAction);
  Serial.print(F(" at runtime "));
  Serial.println(millis());

  // V 1.2: Activity 3 = ring
  //if (pCodeList[iCodeListEntry].bAction == 3)
  //{
  //  DoRing();
  //}
  //else
  
  // actions > 1 set keypad facility
  if (pCodeList[iCodeListEntry].bAction > 1)
  {
    // short indication to request keypad input
    digitalWrite(PINLED,LOW);  // LED is active on low
    digitalWrite(PINBUZZ,LOW);  // Buzzer is active on low
    pause(500);
    digitalWrite(PINBUZZ,HIGH); 
    digitalWrite(PINLED,HIGH);
    return(pCodeList[iCodeListEntry].bAction);
  }

    digitalWrite(PINRFPOWER, HIGH);  // power to RF transmitter
    pause(50);
    if (pCodeList[iCodeListEntry].bAction == 1)
    {
      digitalWrite(PINOPEN, PINOPENCLOSEACTIVE);
    }
    else if (pCodeList[iCodeListEntry].bAction == 0)
    {
      digitalWrite(PINCLOSE, PINOPENCLOSEACTIVE);
    }
    digitalWrite(PINLED,LOW);  // LED is active on low
    digitalWrite(PINBUZZ,LOW);  // Buzzer is active on low
    #ifdef EQ3LOCK 
    { 
      // for new eQ-3 drive: short rf & beep
      pause(PINRFACTIVE_MS);
      digitalWrite(PINBUZZ,HIGH); 
    }
    #else
    { 
      // for old lock-drive: rf long on, but do not beep all time
      pause(500);
      digitalWrite(PINBUZZ,HIGH); 
      pause(PINRFACTIVE_MS-500);
    }
    #endif
    digitalWrite(PINOPEN, !PINOPENCLOSEACTIVE);
    digitalWrite(PINCLOSE, !PINOPENCLOSEACTIVE);
    digitalWrite(PINRFPOWER, LOW);
    digitalWrite(PINLED,HIGH);
    
    // only open codes reset wrong code counter
    // otherwise there would be a trick to keep the counter low: enter close command, which may be public
    if (pCodeList[iCodeListEntry].bAction != 0)
    {
      iWrongCodeCount=0;
      SetTamperFlag(false);
    }

  return(0);
}


/////////////////////
// wrong code entered
/////////////////////
void DoFail()
{
  int ii;
  
  // set tamper code - wrong code entered
  // we only remove the tampering by entry of a correct code OR after punish time
  SetTamperFlag(true);
  
  pause(100);  // small pause to separate from code entry / tag detection beeps
  
  // provide some signal for wrong code
  for (ii=0;ii<5;ii++) // sum of loop is one second
  {
    digitalWrite(PINLED,LOW);  // LED is active on low
    digitalWrite(PINBUZZ,LOW);  // Buzzer is active on low
    pause(100);
    digitalWrite(PINLED,HIGH);
    digitalWrite(PINBUZZ,HIGH);
    pause(100); 
  }
  
  // last wrong code long ago? - reset or count up
  if (millis()>ulLastWrongCode_ms+PUNISHTIME_SEC*1000UL) 
  {
    iWrongCodeCount=1;  // reset to 1
    Serial.println(F("Resetting wrong code counter due to time-out."));
  }
  else
  {
    iWrongCodeCount++;  // within time window: count up
  }
  
  // too many wrong codes in time window?
  if (iWrongCodeCount>=PUNISHWRONGCODES)
  {
    Serial.print(F("Too many wrong codes entered. Will sleep this amount of seconds - also on serial: "));
    Serial.print(PUNISHTIME_SEC);
    
    // signaling to many wrong codes also to outside
    digitalWrite(PINWRONGCODE,HIGH);
    
    // wait and keep led blinking
    for (ii=0;ii<PUNISHTIME_SEC;ii++)
    {
      int ij;
      for (ij=0;ij<5;ij++)  // sum of loop is one second
      {
        digitalWrite(PINLED,LOW);  // LED is active on low
        pause(100);
        digitalWrite(PINLED,HIGH); 
        pause(100);
      }
    }
    Serial.println(F("... Done."));
    digitalWrite(PINWRONGCODE,LOW);
    SetTamperFlag(false);
    iWrongCodeCount=0;
  }
  
  ulLastWrongCode_ms=millis();
}


////////////////
// ring the bell
////////////////
void DoRing()
{
  digitalWrite(PINRING,PINRINGACTIVE);  // bell is active on high
  Serial.println(F("Ring."));
  pause(PINRINGACTIVE_MS);
  digitalWrite(PINRING,!PINRINGACTIVE);
}