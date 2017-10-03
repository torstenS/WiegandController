/*
  Wiegand Routines
  V. 1.0 Stefan Thesen, 2014 - 1st version released
  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry
                               & external alarm, if too many wrong codes entered. 
  V. 1.2 Stefan Thesen, 2015 - adaptations for 
                                 telnet 2 serial bridge, 
                                 support for usual Arduino relay boards (active = low), 
                                 support of new eQ-3 version of lock-drive
                                 
  Copyright: public domain -> do what you want
*/

// Number of facility bits for Wiegand26.
// Remaining bits are used for card code.
// Standard would be 8 facility bits.
// Maximum 24 bits -> 3 bytes for card code simplifies
// code entry in UI. See CARDCODE_MASK in UI.ino
#define WEG26FACILITYBITS 0          

//////////
// Globals
//////////
#define MAX_BITS 50                  // max number of wiegand bits 
#define WEIGAND_WAIT_TIME_MS 50      // milliseconds to wait for another weigand pulse.  

unsigned char pDataBits[MAX_BITS];   // stores all of the data bits
int iBitCount;                       // number of bits currently captured
bool bflagDone;                      // goes low when data is currently being captured
unsigned long ullast_weigand_event_ms; // countdown until we assume there are no more bits
bool bWiegandAvail;                  // true, if we are receiving

unsigned long ulFacilityCode=0;      // decoded facility code
unsigned long ulCardCode=0;          // decoded card code


///////////////////////////////////////////////////
// handle falling edge on Wiegand D0 line (bit = 0)
///////////////////////////////////////////////////
void WiegandD0()
{
  if(bWiegandAvail)
  {
    if (iBitCount<MAX_BITS) 
    {
      pDataBits[iBitCount] = 0;
      iBitCount++;
    }
    bflagDone=false;
    ullast_weigand_event_ms = millis();   
  }
}


///////////////////////////////////////////////////
// handle falling edge on Wiegand D1 line (bit = 1)
///////////////////////////////////////////////////
void WiegandD1()
{
  if(bWiegandAvail)
  {
    if (iBitCount<MAX_BITS) 
    {
      pDataBits[iBitCount] = 1;
      iBitCount++;
    }
    bflagDone=false;
    ullast_weigand_event_ms = millis();   
  }
}


////////////////////////////////////
// check if Wiegand signal available
////////////////////////////////////
bool WiegandAvailable()
{
  // there is no end to a command in the Wiegand protocol. So we wait till some time after the last pulse has passed.
  if (!bflagDone) 
  {
    if ( (millis() - ullast_weigand_event_ms) > WEIGAND_WAIT_TIME_MS ) 
    { 
      bflagDone = true; 
      bWiegandAvail = false;
      if (iBitCount>0)
      { 
        if(WiegandDecode())
        {
          return(true);
        }
        else
        {
          WiegandReset();
          return(false);
        }
      }
    }
  }
  
  return(false);
}


/////////////////////////////////////
// reset all Wiegand reader variables
/////////////////////////////////////
void WiegandReset()
{
  // clean up and get ready for the next card
  iBitCount = 0;
  bflagDone = true;
  bWiegandAvail = true;
  ulFacilityCode = 0;
  ulCardCode = 0;
}


////////////////////////////////
// setup Wiegand Reader on D2/D3
////////////////////////////////
void WiegandSetup()
{
  pinMode(2, INPUT);     // Wiegand D0
  pinMode(3, INPUT);     // Wiegand D1
  
  // Wiegand Read functions triggered by falling edge of D2 and D3
  attachInterrupt(0, WiegandD0, FALLING);  
  attachInterrupt(1, WiegandD1, FALLING);

  // init variables
  ullast_weigand_event_ms = 0;
  WiegandReset();
}


//////////////////////////////////////////////////////
// Decode facility and card-code from Wiegand raw data
//////////////////////////////////////////////////////
bool WiegandDecode()
{
  // different formats allocate bits differently to facility and card-code
  // define decoding based on BitCount
  // this is not perfect there are sometimes multiple standards for one BitCount
  int i;
  int ilowfac=0,ihighfac=0;
  int ilowcard=0,ihighcard=0;
  
  Serial.print(F("Received "));
  Serial.print(iBitCount);
  Serial.print(F(" bits: "));
  for (i=0; i<iBitCount;  i++)
  {
    Serial.print((pDataBits[i]&1)?"1":"0");
  }
  Serial.println(".");
  
  switch (iBitCount)
  {
    case 4:   // 4 Bit HID --> separate keys
      ilowfac  = 0;  ihighfac  = 0;
      ilowcard = 0;  ihighcard = 4;    
      break;
    case 8:   // 8 bit HID --> seperate keys
      ilowfac  = 0;  ihighfac  = 0;
      ilowcard = 0;  ihighcard = 8;
      break;
    case 18:  // sebury proprietary ring indication
      ilowfac  = 0;  ihighfac  = 0;
      ilowcard = 1;  ihighcard = 17;
      break;
    case 26:  // standard 26 bit format
      ilowfac  = 1;  ihighfac  = 1 + WEG26FACILITYBITS;
      ilowcard = 1+WEG26FACILITYBITS;  ihighcard = 25;
      break;
    case 35:  // 35 bit HID Corporate 1000 format
      ilowfac  = 2;  ihighfac  = 14;
      ilowcard = 14; ihighcard = 34;
      break;
    case 37:  
      // HID 37 bit standard / H10304
      //ilowfac  = 1;  ihighfac  = 17;
      //ilowcard = 17;  ihighcard = 36;
      // my way: get as much as possible into cardcode --> 4 bytes
      ilowfac  = 1;  ihighfac  = 12;
      ilowcard = 12; ihighcard = 36;
      break;
    default:
      Serial.print("Unknown format. Cannot decode ");
      Serial.print(iBitCount);
      Serial.println(" bit format.");
      return (false);
      break;
  }

  // facility code
  for (i=ilowfac; i<ihighfac; i++)
  {
     ulFacilityCode <<=1;
     ulFacilityCode |= pDataBits[i];
  }
  
  // card code
  for (i=ilowcard; i<ihighcard; i++)
  {
     ulCardCode <<=1;
     ulCardCode |= pDataBits[i];
  }
  
  // check 8 bit key codes
  if (iBitCount == 8)
  {
    byte high = ulCardCode >> 4;
    byte low  = ulCardCode & 0x0f;
    if ( (~high & 0x0f) != low )
    {
      return(false);
    } else {
      ulCardCode = low;
      iBitCount = 4;
    }
  }
  
  return(true);
}


////////////////////////////
// get decoded Facility Code
////////////////////////////
unsigned long WiegandFacilityCode()
{
  return ulFacilityCode;
}


////////////////////////////
// get decoded Card Code
////////////////////////////
unsigned long WiegandCardCode()
{
  return ulCardCode;
}


////////////////////////////
// get decoded Card Code
////////////////////////////
int WiegandBitCount()
{
  return iBitCount;
}
