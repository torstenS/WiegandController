/*
  Wiegand Door Controller
  V. 1.0 Stefan Thesen, 2014 - 1st version released
  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry 
                               & external alarm, if too many wrong codes entered. 
  V. 1.2 Stefan Thesen, 2015 - adaptations for 
                                 telnet 2 serial bridge, 
                                 support for usual Arduino relay boards (active = low), 
                                 support of new eQ-3 version of lock-drive
  V. 1.3 Torsten Schumacher 2017 - adaptions for 
                                    - user and pin management
                                    - FS20 output
                                    - 8bit Wiegand keycode input
  V. 1.4 Torsten Schumacher 2017 - UI uses Software serial
                                    
  Copyright: public domain -> do what you want
  
  For details visit http://blog.thesen.eu 
  
  some code ideas / parts taken from Daniel Smith, www.pagemac.com 
*/

#include <avr/eeprom.h>
#include <avr/wdt.h>

// globals for code entry with separate keystrokes
unsigned long ulKeypadCode;
byte          bKeypadUser;
unsigned long ulCodeCount;
unsigned long ulLastCodeEntry;
bool bDryRun;

#define KEYTIMEOUT_MS 10000    // after this amount of milliseconds, the code entry forgets any prior input
#define INVERT4BIT 0           // invert 4 Bit entry (some Wiegand devices need this)

////////////////////
// the setup routine
////////////////////
void setup()
{
   // immediately disable watchdog timer so setup will not get interrupted
   wdt_disable();

  Serial.begin(9600);
  Serial.println(F("Wiegand Controller V.1.4 - T.S. 2017"));

  // setup helpers
  SetupCodeHandling();
  SetupFS20();
  WiegandSetup();
  SetupUI(); 
  
  // setup for separate key-stroke entries
  ulKeypadCode=0;
  bKeypadUser=0;
  ulCodeCount=0;
  ulLastCodeEntry=0;
  
  bDryRun = false;
  
  Serial.println(F(""));

  // the following forces a pause before enabling WDT. This gives the IDE a chance to
  // call the bootloader in case something dumb happens during development and the WDT
  // resets the MCU too quickly. Once the code is solid, remove this.
  delay(2L * 1000L);

  // enable the watchdog timer. There are a finite number of timeouts allowed (see wdt.h).
  // Notes I have seen say it is unwise to go below 250ms as you may get the WDT stuck in a
  // loop rebooting.
  // The timeouts I'm most likely to use are:
  // WDTO_1S
  // WDTO_2S
  // WDTO_4S
  // WDTO_8S
  wdt_enable(WDTO_4S);  

  PrintMainMenu();
}

void setDryRun(bool in)
{
  bDryRun=in;
}

///////////////////////////////////////////
// main loop - handles reception & decoding
///////////////////////////////////////////
void loop()
{
  unsigned long ulFacilityCode, ulCardCode;
  
  // timeout on Keypad input
  if (bKeypadUser && (ulLastCodeEntry+KEYTIMEOUT_MS<millis()))
  {
    Serial.println(F("Reseting keypad user"));
    bKeypadUser = 0;
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
          if (!bDryRun)
          {
            bKeypadUser = HandleCode(bKeypadUser, ulKeypadCode);
          } else {
            // report ulKeypadCode to UI
            PrintCode(ulKeypadCode);
          }
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

        if (!bDryRun)
        {
          // 18 bit long is proprietary format for door bell
          // function Code 1 is card
          // function code 2 is door bell
          // other function codes apply for keypad entry
          byte bFunctionCode = (WiegandBitCount()==18 ? 2 : 1);
          bKeypadUser = HandleCode(bFunctionCode, ulCardCode);
          // allow for KEYTIMEOUT_MS to enter PIN
          ulKeypadCode = 0;
          ulCodeCount = 0;          
          ulLastCodeEntry = millis();
        } else {
            // report ulCardCode to UI          
            PrintCode(ulCardCode);
        }
      } 
    }
    else 
    {
      Serial.println(F("Cannot decode. Unknown format.")); 
    }
    
    // clean up and get ready for the next card
    WiegandReset();
  }
  doSendStatus();
  uiEvent();
  // reset the watchdog timer
  wdt_reset();
}

