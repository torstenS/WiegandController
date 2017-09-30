/*
  Wiegand Door Controller
  V. 1.0 Stefan Thesen, 2014 - 1st version released
  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry 
                               & external alarm, if too many wrong codes entered. 
  V. 1.2 Stefan Thesen, 2015 - adaptations for 
                                 telnet 2 serial bridge, 
                                 support for usual Arduino relay boards (active = low), 
                                 support of new eQ-3 version of lock-drive
                               
  Copyright: public domain -> do what you want
  
  For details visit http://blog.thesen.eu 
  
  some code ideas / parts taken from Daniel Smith, www.pagemac.com 
*/

#include <avr/eeprom.h>

// globals for code entry with separate keystrokes
unsigned long ulKeypadCode;
unsigned long ulKeypadFacility;
unsigned long ulCodeCount;
unsigned long ulLastCodeEntry;
#define KEYTIMEOUT_MS 10000    // after this amount of milliseconds, the code entry forgets any prior input
#define INVERT4BIT 0           // invert 4 Bit entry (some Wiegand devices need this)

////////////////////
// the setup routine
////////////////////
void setup()
{  
  Serial.begin(9600);
  Serial.println(F("Wiegand Controller V.1.2 - S.T. 2015"));

  // setup helpers
  SetupCodeHandling();
  WiegandSetup();
  SetupUI(); 
  
  // setup for separate key-stroke entries
  ulKeypadCode=0;
  ulKeypadFacility=0;
  ulCodeCount=0;
  ulLastCodeEntry=0;
  
  Serial.println(F(""));
  PrintMainMenu();
}


///////////////////////////////////////////
// main loop - handles reception & decoding
///////////////////////////////////////////
void loop()
{
  unsigned long ulFacilityCode, ulCardCode;
  
  // timeout on Keypad input
  if (ulKeypadFacility && ulLastCodeEntry+KEYTIMEOUT_MS<millis())
  {
    Serial.println(F("Reseting Keypad facility code"));
    ulKeypadFacility = 0;
  }
  // we have data?
  if (WiegandAvailable())
  {
    unsigned char i;
        
    if (WiegandBitCount()!=0) 
    {
      ulFacilityCode = WiegandFacilityCode();
      ulCardCode = WiegandCardCode();

      // 4 Bit Wiegand 
      //    -> separate key-strokes -> construct code
      if(WiegandBitCount()==4)
      {
        // invert! - and keep lower 4 bit
        if (INVERT4BIT!=0)
        {
          ulCardCode = ~ulCardCode;
          ulCardCode &= 15;
        }
        
        // * entered OR timeout -> reset manual code entry
        if ( (ulCardCode==10) || (ulLastCodeEntry+KEYTIMEOUT_MS<millis()) )  
        {
          Serial.println(F("4-Bit Wiegand - Manual code entry starting."));
          ulKeypadCode = 0;
          ulCodeCount = 0;
        }
        
        // # and at least one digit entered -> manual code entry complete
        if (ulCardCode==11) 
        {
          Serial.print(F("4-Bit Wiegand - Manual code entry finalized:"));
          Serial.println(ulKeypadCode);
          ulKeypadFacility = HandleCode(ulKeypadFacility, ulKeypadCode);
          ulKeypadCode=0;
          ulCodeCount = 0;
        }
        
        // number 0..9 entered -> build code
        if (ulCardCode<10)  
        {
          Serial.print(F("Key pressed: "));
          Serial.println(ulCardCode);
          ulKeypadCode *= 10;
          ulKeypadCode += ulCardCode;
          ulCodeCount++;
        }
        
        // unclear input
        if (ulCardCode>11)  
        {
          Serial.println(F("Cannot interpret key-stroke. Ignoring."));
        }
        
        ulLastCodeEntry = millis();
      }
      // non 4-bit Wiegand 
      //    -> full code is in results
      else  
      {
        Serial.print(F("Read "));
        Serial.print(WiegandBitCount());
        Serial.print(F(" bits. "));
        Serial.print(F("FacilityCode,CardCode = "));
        Serial.print(ulFacilityCode);
        Serial.print(F(","));
        Serial.println(ulCardCode); 

        ulKeypadFacility = HandleCode(ulFacilityCode, ulCardCode);
      } 
    }
    else 
    {
      Serial.println(F("Cannot decode. Unknown format.")); 
    }
    
    // clean up and get ready for the next card
    WiegandReset();
  }
}

