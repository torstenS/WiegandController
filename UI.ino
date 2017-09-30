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

//////////
// Globals
//////////

#define CARDCODE_MASK 0x00ffffff     // 3 Byte CardCode only

// note: Arduino String Objects eat (lots) of RAM dynamically and are not easy to predict
// so we do standard c 'strings', so that we can control memory
#define MAXSERBUFF 40                // max size for serial line in input
char sInBuffer[MAXSERBUFF];          // buffer for serial reading
int iMenuMode;                       // Mode flag for menu


void SetupUI()
{
  // init variables
  iMenuMode = 0;
  sInBuffer[0]=0;
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
       ProcessBuffer();
    }
    else if (cc!=10)
    {
      Serial.print(cc);
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
      Serial.println(F("To add code/card enter: <Name,FacilityCode,CardCode,Action> as list or just <enter> to cancel"));
      Serial.print(F(">"));
      iMenuMode=1;
    }
    else if (strcmp(sInBuffer,"2") == 0)
    {
      Serial.println(F("To delete a code/card enter: Name"));
      Serial.print(F(">"));
      iMenuMode=2;
    }
    else if (strcmp(sInBuffer,"3") == 0)
    {
      // display all codes
      int ii;
      Serial.println(F("Codelist - Format: Name,FacilityCode,CardCode,Action"));
      for (ii=0;ii<iCodeListSize;ii++)
      {
        Serial.print(pCodeList[ii].sName);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].ulFacilityCode);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].ulCardCode);
        Serial.print(F(","));
        Serial.print(pCodeList[ii].bAction);
        Serial.println(F(""));
      }
      Serial.println(F("Codelist End."));
      
      iMenuMode=0;
    }
    else if (strcmp(sInBuffer,"4") == 0)
    {
      // read a card
      Serial.println(F("Hold card to reader. Return to exit."));
      setDryRun(true);
      iMenuMode=4;
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
      Serial.println(F("Saving to EEprom..."));
      SaveToEEProm();
      iMenuMode=0;
    }
    else if (strcmp(sInBuffer,"999") == 0)
    {
      // delete all codes
      iCodeListSize=0;
      Serial.println(F("Erasing EEprom..."));
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
      char *cFirstComma=NULL,*cSecondComma=NULL,*cThirdComma=NULL;  // set all to return code fail 
      cFirstComma=strchr(sInBuffer,',');
      if (cFirstComma) cSecondComma=strchr(cFirstComma+1,',');
      if (cSecondComma) cThirdComma=strchr(cSecondComma+1,',');
      if (cFirstComma!=NULL && cSecondComma!=NULL && cThirdComma!=NULL)
      {
        pCodeList[iCodeListSize].ulFacilityCode = atol(cFirstComma+1);
        pCodeList[iCodeListSize].ulCardCode     = atol(cSecondComma+1) & CARDCODE_MASK;
        pCodeList[iCodeListSize].bAction        = atoi(cThirdComma+1);
        
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
        Serial.println(F("Invalid entry! - Retry or hit Enter to exit."));
        Serial.print(F(">"));
      }
    }
    else
    {
      Serial.println(F("Maximum number of code entries reached! - Exiting."));
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


///////////////////////
// prints the main menu
///////////////////////
void PrintMainMenu()
{
  Serial.println(F("***** MENU ****"));
  Serial.println(F("<enter> -> print this menu"));
  Serial.println(F("  1     -> learn a code / paste csv-list of codes"));
  Serial.println(F("  2     -> delete a code"));
  Serial.println(F("  3     -> display all codes"));
  Serial.println(F("  4     -> read a card"));
  Serial.println(F("  7     -> reset tamper flag & reboot"));
  Serial.println(F("  8     -> save all codes to eeprom"));
  Serial.println(F("  999   -> delete ALL codes in eeprom"));
  Serial.print  (F("Notes: Name entries max "));
  Serial.print  (MAXNAMESIZE-1);
  Serial.println(F(" characters."));
  Serial.println(F("       Action entries 1 for open, 0 for close, >1 request pin"));
  Serial.println(F(""));
  Serial.print  (F(">"));
}

