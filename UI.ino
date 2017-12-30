/*
  User Interface Routines for Wiegand controller
  V. 1.0 Stefan Thesen, 2014 - 1st version released
  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry
                               & external alarm, if too many wrong codes entered. 
  V. 1.2 Stefan Thesen, 2015 - adaptations for 
                                 telnet 2 serial bridge, 
                                 support for usual Arduino relay boards (active = low), 
                                 support of new eQ-3 version of lock-drive
                                 
  Copyright: public domain -> do what you want
*/

#include <avr/wdt.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(11, 12); // RX, TX

//////////
// Globals
//////////

#define CARDCODE_MASK 0x00ffffff     // 3 Byte CardCode only

// note: Arduino String Objects eat (lots) of RAM dynamically and are not easy to predict
// so we do standard c 'strings', so that we can control memory
#define MAXSERBUFF 64                // max size for serial line in input
char sInBuffer[MAXSERBUFF];          // buffer for soft serial reading
char sUsbInBuffer[MAXSERBUFF];       // buffer for USB reading
int iMenuMode;                       // Mode flag for menu
int iUsbMenuMode;                    // Mode flag for USB reading

void SetupUI()
{
  // init variables
  iMenuMode = 0;
  sInBuffer[0]=0;
  iUsbMenuMode = 0;
  sUsbInBuffer[0]=0;

  // init softSerial
  mySerial.begin(57600);
}


/////////////////////////////////////////////////
// read serial input data and create input buffer
/////////////////////////////////////////////////
void serialEvent()
{
  char cc=0;
  if(Serial.available())
  {
    cc = Serial.read();
    // V1.2: we do not listen to cc==10 any more; by this way we are compatible to Windows, Linux and MacOS
    if(cc==13) 
    {
       Serial.println(F(""));
       ProcessUsbBuffer();
    }  else if (cc==0x7F)  // backspace
    {
       int ipos = strlen(sUsbInBuffer);
       if (ipos > 0) {
         ipos--;
         sUsbInBuffer[ipos]=0;
         Serial.print(F("\b \b"));
       }
    } else if (cc!=10)
    {
      Serial.print(cc);
      int ipos = strlen(sUsbInBuffer);
      if (ipos<MAXSERBUFF-1)
      { 
        sUsbInBuffer[ipos]=cc;
        sUsbInBuffer[ipos+1]=0;
      }
    }
  }
}

void uiEvent()
{
  char cc=0;
  if(mySerial.available())
  {
    cc = mySerial.read();
    // V1.2: we do not listen to cc==10 any more; by this way we are compatible to Windows, Linux and MacOS
    if(cc==13) 
    {
       mySerial.println(F(""));
       ProcessBuffer();
    }  else if (cc==0x7F)  // backspace
    {
       int ipos = strlen(sInBuffer);
       if (ipos > 0) {
         ipos--;
         sInBuffer[ipos]=0;
         mySerial.print(F("\b \b"));
       }
    } else if (cc!=10)
    {
      mySerial.print(cc);
      int ipos = strlen(sInBuffer);
      if (ipos<MAXSERBUFF-1)
      { 
        sInBuffer[ipos]=cc;
        sInBuffer[ipos+1]=0;
      }
    }
  }
}


//////////////////////////////////
// trigger actions by serial input
//////////////////////////////////
void ProcessBuffer()
{
  if (iMenuMode==0)
  {
    if (strcmp(sInBuffer,"1") == 0)
    {
      mySerial.println(F("To add code/card enter data as list or just <enter> to cancel"));
      mySerial.println(F("<Name,FunctionCode,CardCode,UserId,Action>"));
      mySerial.print(F(">"));
      iMenuMode=1;
    }
    else if (strcmp(sInBuffer,"2") == 0)
    {
      mySerial.println(F("To delete a code/card enter: Name"));
      mySerial.print(F(">"));
      iMenuMode=2;
    }
    else if (strcmp(sInBuffer,"3") == 0)
    {
      // display all codes
      int ii;
      mySerial.println(F("Codelist - Format: Name,FunctionCode,CardCode,UserId,Action"));
      for (ii=0;ii<iCodeListSize;ii++)
      {
        mySerial.print(pCodeList[ii].sName);
        mySerial.print(F(","));
        mySerial.print(pCodeList[ii].bFunctionCode);
        mySerial.print(F(","));
        mySerial.print(pCodeList[ii].ulCardCode);
        mySerial.print(F(","));
        mySerial.print(pCodeList[ii].bAction >> 2);
        mySerial.print(F(","));
        mySerial.print(pCodeList[ii].bAction & 0x03);
        mySerial.println(F(""));
      }
      mySerial.println(F("Codelist End."));
      
      iMenuMode=0;
    }
    else if (strcmp(sInBuffer,"4") == 0)
    {
      mySerial.println(F("To change a code's action enter: Name,Action"));
      mySerial.print(F(">"));
      iMenuMode=4;
    }
    else if (strcmp(sInBuffer,"5") == 0)
    {
      // read a card
      mySerial.println(F("Hold card to reader. Return to exit."));
      setDryRun(true);
      iMenuMode=5;
    }
    else if (strcmp(sInBuffer,"7") == 0)
    {
      // reboot
      SetTamperFlag(false);
      asm volatile ("  jmp 0");
    }
    else if (strcmp(sInBuffer,"8") == 0)
    {
      // save to eeprom
      mySerial.println(F("Saving to EEprom..."));
      SaveToEEProm();
      iMenuMode=0;
    }
    else if (strcmp(sInBuffer,"999") == 0)
    {
      // delete all codes
      iCodeListSize=0;
      mySerial.println(F("Erasing EEprom..."));
      SaveToEEProm();
      iMenuMode=0;
    }
  }
  // ADD CODES AS CSV LIST 
  else if (iMenuMode==1)
  {
    if (strlen(sInBuffer)==0)
    {
      iMenuMode=0;
    }
    else if (iCodeListSize<iMaxCodeList)
    {
      // find comma delimiters
      char *cFirstComma=NULL,*cSecondComma=NULL,*cThirdComma=NULL,*cFourthComma=NULL;  // set all to return code fail 
      cFirstComma=strchr(sInBuffer,',');
      if (cFirstComma) cSecondComma=strchr(cFirstComma+1,',');
      if (cSecondComma) cThirdComma=strchr(cSecondComma+1,',');
      if (cThirdComma) cFourthComma=strchr(cThirdComma+1,',');      
      if (cFirstComma!=NULL && cSecondComma!=NULL && cThirdComma!=NULL && cFourthComma!=NULL)
      {
        pCodeList[iCodeListSize].bFunctionCode  = atol(cFirstComma+1);
        pCodeList[iCodeListSize].ulCardCode     = atol(cSecondComma+1) & CARDCODE_MASK;
        pCodeList[iCodeListSize].bAction        = (atoi(cThirdComma+1) << 2) + 
                                                  (atoi(cFourthComma+1) & 0x03);
        
        // name is max MAXNAMESIZE bytes incl termination char 
        if ((cFirstComma-sInBuffer)>=MAXNAMESIZE) {cFirstComma=sInBuffer+MAXNAMESIZE-1;}
        memcpy((void*)pCodeList[iCodeListSize].sName,(void*)sInBuffer,cFirstComma-sInBuffer);
        pCodeList[iCodeListSize].sName[cFirstComma-sInBuffer]=0; // to be sure: term char
        
        // do not display status - otherwise arduino is too slow, when pasting a list
        //Serial.print(F("Entry added "));
        //Serial.print(pCodeList[iCodeListSize].sName);
        //Serial.println(F(". Next entry or hit Enter to exit."));
        //Serial.print(F(">"));
        iCodeListSize++;
      }
      else
      {
        mySerial.println(F("Invalid entry! - Retry or hit Enter to exit."));
        mySerial.print(F(">"));
      }
    }
    else
    {
      mySerial.println(F("Maximum number of code entries reached! - Exiting."));
      iMenuMode=0;
    }    
  }  
  else if (iMenuMode==2)
  {
    // delete a code
    int ii=0;
    bool bFound=false;
    while (ii<iCodeListSize && !bFound)
    {
      if (strncmp(sInBuffer,pCodeList[ii].sName,MAXNAMESIZE)==0) {bFound=true;}
      else { ii++; }
    }
    if (bFound)
    {
      int ikill=ii;
      iCodeListSize--;
      for(ii=ikill;ii<iCodeListSize;ii++)
      {
        memcpy((void*)&pCodeList[ii],(void*)&pCodeList[ii+1],sizeof(CODELIST));
      }
      mySerial.println(F("Entry removed."));
    }
    else
    {
      mySerial.println(F("Entry not found!"));
    }
    iMenuMode=0;
  }
  else if (iMenuMode==4)
  {
    // edit a code

    // find comma delimiters
    char *cFirstComma=NULL,*cSecondComma=NULL;  // set all to return code fail 
    cFirstComma=strchr(sInBuffer,',');
    if (cFirstComma!=NULL)
    {
      // name is max MAXNAMESIZE bytes incl termination char
      char sName[MAXNAMESIZE];
      if ((cFirstComma-sInBuffer)>=MAXNAMESIZE) {cFirstComma=sInBuffer+MAXNAMESIZE-1;}
      memcpy((void*)sName,(void*)sInBuffer,cFirstComma-sInBuffer);
      sName[cFirstComma-sInBuffer]=0; // to be sure: term char
      
      // search name
      int ii=0;
      bool bFound=false;
      while (ii<iCodeListSize && !bFound)
      {
        if (strncmp(sName,pCodeList[ii].sName,MAXNAMESIZE)==0) {bFound=true;}
        else { ii++; }
      }
      if (bFound)
      {
        pCodeList[ii].bAction        = (pCodeList[ii].bAction & 0xfc) + 
                                       (atoi(cFirstComma+1) & 0x03);
        mySerial.println(F("Entry updated."));
      }
      else
      {
        mySerial.println(F("Entry not found!"));
      }
    }
    else
    {
      mySerial.println(F("Invalid entry!"));
    }
    iMenuMode=0;
  }
  else if (iMenuMode==5)
  {
    if (strlen(sInBuffer)==0)
    {
      iMenuMode=0;
      setDryRun(false);
    }
  }  
  // prepare for next inputs
  sInBuffer[0]=0;
  if (iMenuMode==0) {PrintMainMenu();}
}

void ProcessUsbBuffer()
{
  if (iUsbMenuMode==0)
  {
    if (strcmp(sUsbInBuffer,"1") == 0)
    {
      Serial.println(F("To add code/card enter data as list or just <enter> to cancel"));
      Serial.println(F("<Name,FunctionCode,CardCode,UserId,Action>"));
      Serial.print(F(">"));
      iUsbMenuMode=1;
    }
    else if (strcmp(sUsbInBuffer,"2") == 0)
    {
      Serial.println(F("To delete a code/card enter: Name"));
      Serial.print(F(">"));
      iUsbMenuMode=2;
    }
    else if (strcmp(sUsbInBuffer,"3") == 0)
    {
      // display all codes
      int ii;
      Serial.println(F("Codelist - Format: Name,FunctionCode,CardCode,UserId,Action"));
      for (ii=0;ii<iCodeListSize;ii++)
      {
        Serial.print(pCodeList[ii].sName);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].bFunctionCode);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].ulCardCode);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].bAction >> 2);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].bAction & 0x03);
        Serial.println(F(""));
      }
      Serial.println(F("Codelist End."));
      
      iUsbMenuMode=0;
    }
    else if (strcmp(sUsbInBuffer,"4") == 0)
    {
      Serial.println(F("To change a code's action enter: Name,Action"));
      Serial.print(F(">"));
      iUsbMenuMode=4;
    }
    else if (strcmp(sUsbInBuffer,"5") == 0)
    {
      // read a card
      Serial.println(F("Hold card to reader. Return to exit."));
      setDryRun(true);
      iUsbMenuMode=5;
    }
    else if (strcmp(sUsbInBuffer,"7") == 0)
    {
      // reboot
      SetTamperFlag(false);
      asm volatile ("  jmp 0");
    }
    else if (strcmp(sUsbInBuffer,"8") == 0)
    {
      // save to eeprom
      Serial.println(F("Saving to EEprom..."));
      SaveToEEProm();
      iUsbMenuMode=0;
    }
    else if (strcmp(sUsbInBuffer,"999") == 0)
    {
      // delete all codes
      iCodeListSize=0;
      Serial.println(F("Erasing EEprom..."));
      SaveToEEProm();
      iMenuMode=0;
    }
  }
  // ADD CODES AS CSV LIST 
  else if (iUsbMenuMode==1)
  {
    if (strlen(sUsbInBuffer)==0)
    {
      iUsbMenuMode=0;
    }
    else if (iCodeListSize<iMaxCodeList)
    {
      // find comma delimiters
      char *cFirstComma=NULL,*cSecondComma=NULL,*cThirdComma=NULL,*cFourthComma=NULL;  // set all to return code fail 
      cFirstComma=strchr(sUsbInBuffer,',');
      if (cFirstComma) cSecondComma=strchr(cFirstComma+1,',');
      if (cSecondComma) cThirdComma=strchr(cSecondComma+1,',');
      if (cThirdComma) cFourthComma=strchr(cThirdComma+1,',');      
      if (cFirstComma!=NULL && cSecondComma!=NULL && cThirdComma!=NULL && cFourthComma!=NULL)
      {
        pCodeList[iCodeListSize].bFunctionCode  = atol(cFirstComma+1);
        pCodeList[iCodeListSize].ulCardCode     = atol(cSecondComma+1) & CARDCODE_MASK;
        pCodeList[iCodeListSize].bAction        = (atoi(cThirdComma+1) << 2) + 
                                                  (atoi(cFourthComma+1) & 0x03);
        
        // name is max MAXNAMESIZE bytes incl termination char 
        if ((cFirstComma-sUsbInBuffer)>=MAXNAMESIZE) {cFirstComma=sUsbInBuffer+MAXNAMESIZE-1;}
        memcpy((void*)pCodeList[iCodeListSize].sName,(void*)sUsbInBuffer,cFirstComma-sUsbInBuffer);
        pCodeList[iCodeListSize].sName[cFirstComma-sUsbInBuffer]=0; // to be sure: term char
        
        // do not display status - otherwise arduino is too slow, when pasting a list
        //Serial.print(F("Entry added "));
        //Serial.print(pCodeList[iCodeListSize].sName);
        //Serial.println(F(". Next entry or hit Enter to exit."));
        //Serial.print(F(">"));
        iCodeListSize++;
      }
      else
      {
        Serial.println(F("Invalid entry! - Retry or hit Enter to exit."));
        Serial.print(F(">"));
      }
    }
    else
    {
      Serial.println(F("Maximum number of code entries reached! - Exiting."));
      iUsbMenuMode=0;
    }    
  }  
  else if (iUsbMenuMode==2)
  {
    // delete a code
    int ii=0;
    bool bFound=false;
    while (ii<iCodeListSize && !bFound)
    {
      if (strncmp(sUsbInBuffer,pCodeList[ii].sName,MAXNAMESIZE)==0) {bFound=true;}
      else { ii++; }
    }
    if (bFound)
    {
      int ikill=ii;
      iCodeListSize--;
      for(ii=ikill;ii<iCodeListSize;ii++)
      {
        memcpy((void*)&pCodeList[ii],(void*)&pCodeList[ii+1],sizeof(CODELIST));
      }
      Serial.println(F("Entry removed."));
    }
    else
    {
      Serial.println(F("Entry not found!"));
    }
    iMenuMode=0;
  }
  else if (iMenuMode==4)
  {
    // edit a code

    // find comma delimiters
    char *cFirstComma=NULL,*cSecondComma=NULL;  // set all to return code fail 
    cFirstComma=strchr(sUsbInBuffer,',');
    if (cFirstComma!=NULL)
    {
      // name is max MAXNAMESIZE bytes incl termination char
      char sName[MAXNAMESIZE];
      if ((cFirstComma-sUsbInBuffer)>=MAXNAMESIZE) {cFirstComma=sUsbInBuffer+MAXNAMESIZE-1;}
      memcpy((void*)sName,(void*)sUsbInBuffer,cFirstComma-sUsbInBuffer);
      sName[cFirstComma-sUsbInBuffer]=0; // to be sure: term char
      
      // search name
      int ii=0;
      bool bFound=false;
      while (ii<iCodeListSize && !bFound)
      {
        if (strncmp(sName,pCodeList[ii].sName,MAXNAMESIZE)==0) {bFound=true;}
        else { ii++; }
      }
      if (bFound)
      {
        pCodeList[ii].bAction        = (pCodeList[ii].bAction & 0xfc) + 
                                       (atoi(cFirstComma+1) & 0x03);
        Serial.println(F("Entry updated."));
      }
      else
      {
        Serial.println(F("Entry not found!"));
      }
    }
    else
    {
      Serial.println(F("Invalid entry!"));
    }
    iUsbMenuMode=0;
  }
  else if (iUsbMenuMode==5)
  {
    if (strlen(sUsbInBuffer)==0)
    {
      iUsbMenuMode=0;
      setDryRun(false);
    }
  }  
  // prepare for next inputs
  sUsbInBuffer[0]=0;
  if (iUsbMenuMode==0) {PrintUsbMainMenu();}
}

///////////////////////
// prints the main menu
///////////////////////
void PrintMainMenu()
{
  mySerial.println(F("***** MENU ****"));
  mySerial.println(F("<enter> -> print this menu"));
  mySerial.println(F("  1     -> learn a code / paste csv-list of codes"));
  mySerial.println(F("  2     -> delete a code"));
  mySerial.println(F("  3     -> display all codes"));
  mySerial.println(F("  4     -> edit a code's action"));
  mySerial.println(F("  5     -> read a card (dry run mode)"));
  mySerial.println(F("  7     -> reset tamper flag & reboot"));
  mySerial.println(F("  8     -> save all codes to eeprom"));
  mySerial.println(F("  999   -> delete ALL codes in eeprom"));
  mySerial.print  (F("Notes: Name entries max "));
  mySerial.print  (MAXNAMESIZE-1);
  mySerial.println(F(" characters."));
  mySerial.println(F("       Function codes: 0 keypad, 1 token, 2 bell, 255 any user, others user id"));
  mySerial.println(F("       Action entries: 0 close, 1 open, 2 request pin, 3 disabled"));
  mySerial.println(F(""));
  mySerial.print  (F(">"));
}

void PrintUsbMainMenu()
{
  Serial.println(F("***** MENU ****"));
  Serial.println(F("<enter> -> print this menu"));
  Serial.println(F("  1     -> learn a code / paste csv-list of codes"));
  Serial.println(F("  2     -> delete a code"));
  Serial.println(F("  3     -> display all codes"));
  Serial.println(F("  4     -> edit a code's action"));
  Serial.println(F("  5     -> read a card (dry run mode)"));
  Serial.println(F("  7     -> reset tamper flag & reboot"));
  Serial.println(F("  8     -> save all codes to eeprom"));
  Serial.println(F("  999   -> delete ALL codes in eeprom"));
  Serial.print  (F("Notes: Name entries max "));
  Serial.print  (MAXNAMESIZE-1);
  Serial.println(F(" characters."));
  Serial.println(F("       Function codes: 0 keypad, 1 token, 2 bell, 255 any user, others user id"));
  Serial.println(F("       Action entries: 0 close, 1 open, 2 request pin, 3 disabled"));
  Serial.println(F(""));
  Serial.print  (F(">"));
}

void PrintCode(unsigned long code)
{
  mySerial.print(F("Card code:"));
  mySerial.println(code);
}

