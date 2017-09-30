#include  <util/parity.h>

#define PINFS20 5              // output pin for FS20 send

#define FS20HC        0x35a4   // housecode
#define FS20ADDR_BELL 0x11     // address byte for bell
#define FS20ADDR_DOOR 0x12     // address byte for door

bool bDoorIsLocked;
unsigned long lastSendMillis;

void SetupFS20()
{
  pinMode(PINFS20, OUTPUT);        // pin high is active
  digitalWrite(PINFS20, LOW);
  bDoorIsLocked = GetDoorStatus();
  lastSendMillis = millis();
}

void sendBit(uint8_t sbit)
{
  word width = sbit ? 600 : 400;
  digitalWrite(PINFS20, 1);
  delayMicroseconds(width);
  digitalWrite(PINFS20, 0);
  delayMicroseconds(width);
}

void sendBits(uint16_t data, uint8_t bits) {
  for (int x = bits; x > 0; x--)
  {
    uint8_t b = bitRead(data, x - 1);
    sendBit(b);
  }
  if (bits == 8) { //Paritätsbit
    uint8_t p = parity_even_bit(data);
    sendBit(p);
  }
}

void fs20cmd(uint16_t house, uint8_t adr, uint8_t cmd, uint8_t cmd2 = 0) {
  uint8_t hc1 = house >> 8;
  uint8_t hc2 = house;
  uint8_t sum = 6 + hc1 + hc2 + adr + cmd + cmd2; // 6 für FS20 12 für fht
  // Für die Quersumme werden nur 8 Bit genommen, der rest wird "abgeschnitten"
  // bei FHT ist das erweiterungsbyte immer gesetzt.
  // Bei FS20 steht das Erweiterungsbyte für den Timerwert.
  // Zeit = 2^(High-Nibble) * Low-Nibble * 0,25s
  // fhz4linux.info/tiki-index.php?page=FS20%20Protocol
  if (cmd2 > 0) bitWrite(cmd, 5, 1); //Erweiterungsbit setzen für zusätzliches byte
  uint8_t loops = 3;
  uint8_t sreg = SREG;
  cli(); //Interrupts abschalten um störungen beim Senden zu vermeiden
  for (uint8_t i = 0; i < loops; ++i)
  {
    sendBits(1, 13);  // 12*0, 1*1
    sendBits(hc1, 8); //Hauscode 1 FHT: Code Teil 1
    sendBits(hc2, 8); //Hauscode 2 FHT: Code Teilo 2
    sendBits(adr, 8); //Adresse bei FHT: Funktionsnummer
    sendBits(cmd, 8); //Befehl FHT: Statusbits, Bit 5 immer gesetzt für erweiterungs byte
    if (cmd2 > 0)
      sendBits(cmd2, 8); //Erweiterungs Byte
    sendBits(sum, 8); //Checksumme
    sendBits(0, 1); //Übertragungsende
    pause(10);
  }
  SREG = sreg;
  pause(75);
}

void doSendRing()
{
  fs20cmd(FS20HC, FS20ADDR_BELL, 16);
}

////////////////////////////////////////////////////////////////////////////////
// sets the tamper-flag - avoid getting around wrong code handling / punish time
// by removing power / reboot
////////////////////////////////////////////////////////////////////////////////
void SetDoorStatus(bool isLocked)
{
  bDoorIsLocked = isLocked;
  
  if (GetDoorStatus() != isLocked) // only, if there is a difference to EEProm
  {
    byte bDoorStatus = 0x0;
    if (isLocked)
    {
      bDoorStatus = 0xff;
    }

    eeprom_write_block((const void*)&bDoorStatus  , (void*)1, sizeof(bDoorStatus));    
  }
  
  // report state change immediately
  lastSendMillis = 0;
  doSendStatus();
}


////////////////////////////////////////////////////////////////
// gets status of door; we check for more than one bit to
//    even be save against single bit failures of EEProm
////////////////////////////////////////////////////////////////
bool GetDoorStatus()
{
  byte bDoorStatus;
  eeprom_read_block((void*)&bDoorStatus, (void*)1, sizeof(bDoorStatus));

  // counts bits set
  byte bCount = 0;
  while (bDoorStatus)
  {
    bCount += bDoorStatus & 1;
    bDoorStatus >>= 1;
  }

  if (bCount > 4) // more than 4 bits = door is locked
  {
    return (true);
  }

  return (false);
}


void doSendStatus()
{
  if (lastSendMillis + 3 * 60 * 1000 < millis())   // report status every 3 minutes
  {
    fs20cmd(FS20HC, FS20ADDR_DOOR, bDoorIsLocked ? 16 : 0);
    lastSendMillis = millis();
  }
}
