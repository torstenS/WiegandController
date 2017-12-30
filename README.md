# Wiegand_Controller
WiegandController for Keymatic

Based on 
https://blog.thesen.eu/teil-3-die-software-rfid-codeschloss-fuer-den-keymatic-abus-funk-tuerschlossantrieb/

-  V. 1.0 Stefan Thesen, 2014 - 1st version released
-  V. 1.1 Stefan Thesen, 2015 - add 4-Bit single key entry 
                               & external alarm, if too many wrong codes entered. 
-  V. 1.2 Stefan Thesen, 2015 - adaptations for  
                                 telnet 2 serial bridge,  
                                 support for usual Arduino relay boards (active = low),  
                                 support of new eQ-3 version of lock-drive
-  V. 1.3 Torsten Schumacher 2017 - adaptions for  
                                    - user and pin management  
                                    - FS20 output  
                                    - 8bit Wiegand keycode input
-  V. 1.4 Torsten Schumacher 2017 - UI uses Software serial
                                    
  Copyright: public domain -> do what you want
  
  For details visit http://blog.thesen.eu 
  
  some code ideas / parts taken from Daniel Smith, www.pagemac.com 
