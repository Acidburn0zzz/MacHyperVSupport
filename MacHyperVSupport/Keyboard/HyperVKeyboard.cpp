//
//  HyperVKeyboard.cpp
//  Hyper-V keyboard driver
//
//  Copyright © 2021 Goldfish64. All rights reserved.
//

#include "HyperVKeyboard.hpp"
#include "HyperVADBMap.hpp"

OSDefineMetaClassAndStructors(HyperVKeyboard, super);

bool HyperVKeyboard::start(IOService *provider) {
  if (!super::start(provider)) {
    return false;
  }
  
  DBGLOG("Initializing Hyper-V Synthetic Keyboard");
  
  //
  // Get parent VMBus device object.
  //
  hvDevice = OSDynamicCast(HyperVVMBusDevice, provider);
  if (hvDevice == NULL) {
    super::stop(provider);
    return false;
  }
  hvDevice->retain();
  
  //
  // Configure interrupt.
  //
  interruptSource =
    IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &HyperVKeyboard::handleInterrupt), provider, 0);
  getWorkLoop()->addEventSource(interruptSource);
  interruptSource->enable();
  
  //
  // Configure the channel.
  //
  if (!hvDevice->openChannel(kHyperVKeyboardRingBufferSize, kHyperVKeyboardRingBufferSize)) {
    interruptSource->disable();
    getWorkLoop()->removeEventSource(interruptSource);
    OSSafeReleaseNULL(interruptSource);
    super::stop(provider);
    return false;
  }
  
  connectKeyboard();
  
  SYSLOG("Initialized Hyper-V Synthetic Keyboard");
  return true;
}

bool HyperVKeyboard::connectKeyboard() {
  DBGLOG("Connecting to keyboard interface");
  
  HyperVKeyboardMessageProtocolRequest requestMsg;
  requestMsg.header.type = kHyperVKeyboardMessageTypeProtocolRequest;
  requestMsg.versionRequested = kHyperVKeyboardVersion;
  
  hvDevice->writeInbandPacket(&requestMsg, sizeof (requestMsg), true);
  
  return true;
}

UInt32 HyperVKeyboard::deviceType() {
  return 3;
}

UInt32 HyperVKeyboard::interfaceID() {
  return NX_EVS_DEVICE_INTERFACE_ADB;
}

inline UInt32 getKeyCode(HyperVKeyboardMessageKeystroke *keyEvent) {
  UInt8 keyCode = PS2ToADBMapStock[keyEvent->makeCode + (keyEvent->isE0 ? kADBConverterExStart : 0)];
  
  return keyCode;
}

void HyperVKeyboard::handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count) {
  UInt8 data128[128];

  do {
    //
    // Check for available inband packets.
    // Large packets will be allocated as needed.
    //
    HyperVKeyboardMessage *message;
    UInt32 pktDataLength;
    if (!hvDevice->nextInbandPacketAvailable(&pktDataLength)) {
      break;
    }

    if (pktDataLength <= sizeof (data128)) {
      message = (HyperVKeyboardMessage*)data128;
    } else {
      DBGLOG("Allocating large packet of %u bytes", pktDataLength);
      message = (HyperVKeyboardMessage*)IOMalloc(pktDataLength);
    }

    //
    // Read next packet.
    //
    if (hvDevice->readInbandCompletionPacket((void *)message, pktDataLength, NULL) == kIOReturnSuccess) {
      switch (message->header.type) {
        case kHyperVKeyboardMessageTypeProtocolResponse:
          DBGLOG("Keyboard protocol status %u %u", message->protocolResponse.header.type, message->protocolResponse.status);
          break;

        case kHyperVKeyboardMessageTypeEvent:
          UInt64 time;
          clock_get_uptime(&time);
          
          dispatchKeyboardEvent(getKeyCode(&message->keystroke), !message->keystroke.isBreak, *(AbsoluteTime*)&time);
          break;

        default:
          DBGLOG("Unknown message type %u, size %u", message->header.type, pktDataLength);
          break;
      }
    }

    //
    // Free allocated packet if needed.
    //
    if (pktDataLength > sizeof (data128)) {
      IOFree(message, pktDataLength);
    }
  } while (true);
}

const unsigned char * HyperVKeyboard::defaultKeymapOfLength(UInt32 * length)
{
    //
    // Keymap data borrowed and modified from IOHIDFamily/IOHIDKeyboard.cpp
    // references  http://www.xfree.org/current/dumpkeymap.1.html
    //             http://www.tamasoft.co.jp/en/general-info/unicode.html
    //
    static const unsigned char appleUSAKeyMap[] = {
        0x00,0x00, // use byte unit.
       
        
        // modifier definition
        0x0b,   //Number of modifier keys.
        // ( modifier   , num of keys, ADB keycodes... )
        // ( 0x00       , 0x01       , 0x39            )
        //0x00,0x01,0x39, //NX_MODIFIERKEY_ALPHALOCK, uses one byte, ADB keycode is 0x39
        NX_MODIFIERKEY_SHIFT,       0x01,0x38,
        NX_MODIFIERKEY_CONTROL,     0x01,0x3b,
        NX_MODIFIERKEY_ALTERNATE,   0x01,0x3a,
        NX_MODIFIERKEY_COMMAND,     0x01,0x37,
        NX_MODIFIERKEY_NUMERICPAD,  0x15,0x52,0x41,0x4c,0x53,0x54,0x55,0x45,0x58,0x57,0x56,0x5b,0x5c,0x43,0x4b,0x51,0x7b,0x7d,0x7e,0x7c,0x4e,0x59,
        NX_MODIFIERKEY_HELP,        0x01,0x72,
        NX_MODIFIERKEY_SECONDARYFN, 0x01,0x3f, // Apple's Fn key
        NX_MODIFIERKEY_RSHIFT,      0x01,0x3c,
        NX_MODIFIERKEY_RCONTROL,    0x01,0x3e,
        NX_MODIFIERKEY_RALTERNATE,  0x01,0x3d,
        NX_MODIFIERKEY_RCOMMAND,    0x01,0x36,
        
        
        // ADB virtual key definitions
        0xa2, // number of key definitions
        // ( modifier mask           , generated character{char_set,char_code}...         )
        // ( 0x0d[has 3bit modifiers], {0x00,0x3c}, {0x00,0x3e}, ... total 2^3 characters )
        0x0d,0x00,0x61,0x00,0x41,0x00,0x01,0x00,0x01,0x00,0xca,0x00,0xc7,0x00,0x01,0x00,0x01, //00 A
        0x0d,0x00,0x73,0x00,0x53,0x00,0x13,0x00,0x13,0x00,0xfb,0x00,0xa7,0x00,0x13,0x00,0x13, //01 S
        0x0d,0x00,0x64,0x00,0x44,0x00,0x04,0x00,0x04,0x01,0x44,0x01,0xb6,0x00,0x04,0x00,0x04, //02 D
        0x0d,0x00,0x66,0x00,0x46,0x00,0x06,0x00,0x06,0x00,0xa6,0x01,0xac,0x00,0x06,0x00,0x06, //03 F
        0x0d,0x00,0x68,0x00,0x48,0x00,0x08,0x00,0x08,0x00,0xe3,0x00,0xeb,0x00,0x00,0x18,0x00, //04 H
        0x0d,0x00,0x67,0x00,0x47,0x00,0x07,0x00,0x07,0x00,0xf1,0x00,0xe1,0x00,0x07,0x00,0x07, //05 G
        0x0d,0x00,0x7a,0x00,0x5a,0x00,0x1a,0x00,0x1a,0x00,0xcf,0x01,0x57,0x00,0x1a,0x00,0x1a, //06 Z
        0x0d,0x00,0x78,0x00,0x58,0x00,0x18,0x00,0x18,0x01,0xb4,0x01,0xce,0x00,0x18,0x00,0x18, //07 X
        0x0d,0x00,0x63,0x00,0x43,0x00,0x03,0x00,0x03,0x01,0xe3,0x01,0xd3,0x00,0x03,0x00,0x03, //08 C
        0x0d,0x00,0x76,0x00,0x56,0x00,0x16,0x00,0x16,0x01,0xd6,0x01,0xe0,0x00,0x16,0x00,0x16, //09 V
        0x02,0x00,0x3c,0x00,0x3e, //0a NON-US-BACKSLASH on ANSI and JIS keyboards GRAVE on ISO
        0x0d,0x00,0x62,0x00,0x42,0x00,0x02,0x00,0x02,0x01,0xe5,0x01,0xf2,0x00,0x02,0x00,0x02, //0b B
        0x0d,0x00,0x71,0x00,0x51,0x00,0x11,0x00,0x11,0x00,0xfa,0x00,0xea,0x00,0x11,0x00,0x11, //0c Q
        0x0d,0x00,0x77,0x00,0x57,0x00,0x17,0x00,0x17,0x01,0xc8,0x01,0xc7,0x00,0x17,0x00,0x17, //0d W
        0x0d,0x00,0x65,0x00,0x45,0x00,0x05,0x00,0x05,0x00,0xc2,0x00,0xc5,0x00,0x05,0x00,0x05, //0e E
        0x0d,0x00,0x72,0x00,0x52,0x00,0x12,0x00,0x12,0x01,0xe2,0x01,0xd2,0x00,0x12,0x00,0x12, //0f R
        0x0d,0x00,0x79,0x00,0x59,0x00,0x19,0x00,0x19,0x00,0xa5,0x01,0xdb,0x00,0x19,0x00,0x19, //10 Y
        0x0d,0x00,0x74,0x00,0x54,0x00,0x14,0x00,0x14,0x01,0xe4,0x01,0xd4,0x00,0x14,0x00,0x14, //11 T
        0x0a,0x00,0x31,0x00,0x21,0x01,0xad,0x00,0xa1, //12 1
        0x0e,0x00,0x32,0x00,0x40,0x00,0x32,0x00,0x00,0x00,0xb2,0x00,0xb3,0x00,0x00,0x00,0x00, //13 2
        0x0a,0x00,0x33,0x00,0x23,0x00,0xa3,0x01,0xba, //14 3
        0x0a,0x00,0x34,0x00,0x24,0x00,0xa2,0x00,0xa8, //15 4
        0x0e,0x00,0x36,0x00,0x5e,0x00,0x36,0x00,0x1e,0x00,0xb6,0x00,0xc3,0x00,0x1e,0x00,0x1e, //16 6
        0x0a,0x00,0x35,0x00,0x25,0x01,0xa5,0x00,0xbd, //17 5
        0x0a,0x00,0x3d,0x00,0x2b,0x01,0xb9,0x01,0xb1, //18 EQUALS
        0x0a,0x00,0x39,0x00,0x28,0x00,0xac,0x00,0xab, //19 9
        0x0a,0x00,0x37,0x00,0x26,0x01,0xb0,0x01,0xab, //1a 7
        0x0e,0x00,0x2d,0x00,0x5f,0x00,0x1f,0x00,0x1f,0x00,0xb1,0x00,0xd0,0x00,0x1f,0x00,0x1f, //1b MINUS
        0x0a,0x00,0x38,0x00,0x2a,0x00,0xb7,0x00,0xb4, //1c 8
        0x0a,0x00,0x30,0x00,0x29,0x00,0xad,0x00,0xbb, //1d 0
        0x0e,0x00,0x5d,0x00,0x7d,0x00,0x1d,0x00,0x1d,0x00,0x27,0x00,0xba,0x00,0x1d,0x00,0x1d, //1e RIGHTBRACKET
        0x0d,0x00,0x6f,0x00,0x4f,0x00,0x0f,0x00,0x0f,0x00,0xf9,0x00,0xe9,0x00,0x0f,0x00,0x0f, //1f O
        0x0d,0x00,0x75,0x00,0x55,0x00,0x15,0x00,0x15,0x00,0xc8,0x00,0xcd,0x00,0x15,0x00,0x15, //20 U
        0x0e,0x00,0x5b,0x00,0x7b,0x00,0x1b,0x00,0x1b,0x00,0x60,0x00,0xaa,0x00,0x1b,0x00,0x1b, //21 LEFTBRACKET
        0x0d,0x00,0x69,0x00,0x49,0x00,0x09,0x00,0x09,0x00,0xc1,0x00,0xf5,0x00,0x09,0x00,0x09, //22 I
        0x0d,0x00,0x70,0x00,0x50,0x00,0x10,0x00,0x10,0x01,0x70,0x01,0x50,0x00,0x10,0x00,0x10, //23 P
        0x10,0x00,0x0d,0x00,0x03, //24 RETURN
        0x0d,0x00,0x6c,0x00,0x4c,0x00,0x0c,0x00,0x0c,0x00,0xf8,0x00,0xe8,0x00,0x0c,0x00,0x0c, //25 L
        0x0d,0x00,0x6a,0x00,0x4a,0x00,0x0a,0x00,0x0a,0x00,0xc6,0x00,0xae,0x00,0x0a,0x00,0x0a, //26 J
        0x0a,0x00,0x27,0x00,0x22,0x00,0xa9,0x01,0xae, //27 APOSTROPHE
        0x0d,0x00,0x6b,0x00,0x4b,0x00,0x0b,0x00,0x0b,0x00,0xce,0x00,0xaf,0x00,0x0b,0x00,0x0b, //28 K
        0x0a,0x00,0x3b,0x00,0x3a,0x01,0xb2,0x01,0xa2, //29 SEMICOLON
        0x0e,0x00,0x5c,0x00,0x7c,0x00,0x1c,0x00,0x1c,0x00,0xe3,0x00,0xeb,0x00,0x1c,0x00,0x1c, //2a BACKSLASH
        0x0a,0x00,0x2c,0x00,0x3c,0x00,0xcb,0x01,0xa3, //2b COMMA
        0x0a,0x00,0x2f,0x00,0x3f,0x01,0xb8,0x00,0xbf, //2c SLASH
        0x0d,0x00,0x6e,0x00,0x4e,0x00,0x0e,0x00,0x0e,0x00,0xc4,0x01,0xaf,0x00,0x0e,0x00,0x0e, //2d N
        0x0d,0x00,0x6d,0x00,0x4d,0x00,0x0d,0x00,0x0d,0x01,0x6d,0x01,0xd8,0x00,0x0d,0x00,0x0d, //2e M
        0x0a,0x00,0x2e,0x00,0x3e,0x00,0xbc,0x01,0xb3, //2f PERIOD
        0x02,0x00,0x09,0x00,0x19, //30 TAB
        0x0c,0x00,0x20,0x00,0x00,0x00,0x80,0x00,0x00, //31 SPACE
        0x0a,0x00,0x60,0x00,0x7e,0x00,0x60,0x01,0xbb, //32 GRAVE on ANSI and JIS keyboards, NON-US-BACKSLASH on ISO
        0x02,0x00,0x7f,0x00,0x08, //33 BACKSPACE
        0xff, //34 PLAY/PAUSE
        0x02,0x00,0x1b,0x00,0x7e, //35 ESCAPE
        0xff, //36 RGUI
        0xff, //37 LGUI
        0xff, //38 LSHIFT
        0xff, //39 CAPSLOCK
        0xff, //3a LALT
        0xff, //3b LCTRL
        0xff, //3c RSHIFT
        0xff, //3d RALT
        0xff, //3e RCTRL
        0xff, //3f Apple Fn key
        0x00,0xfe,0x36, //40 F17
        0x00,0x00,0x2e, //41 KEYPAD_PERIOD
        0xff, //42 NEXT TRACK or FAST
        0x00,0x00,0x2a, //43 KEYPAD_MULTIPLY
        0xff, //44
        0x00,0x00,0x2b, //45 KEYPAD_PLUS
        0xff, //46
        0x00,0x00,0x1b, //47 CLEAR
        0xff, //48 VOLUME UP
        0xff, //49 VOLUME DOWN
        0xff, //4a MUTE
        0x0e,0x00,0x2f,0x00,0x5c,0x00,0x2f,0x00,0x1c,0x00,0x2f,0x00,0x5c,0x00,0x00,0x0a,0x00, //4b KEYPAD_DIVIDE
        0x00,0x00,0x0d,  //4c Apple Fn + Return = ENTER //XX03
        0xff, //4d PREVIOUS TRACK or REWIND
        0x00,0x00,0x2d, //4e KEYPAD_MINUS
        0x00,0xfe,0x37, //4f F18
        0x00,0xfe,0x38, //50 F19
        0x0e,0x00,0x3d,0x00,0x7c,0x00,0x3d,0x00,0x1c,0x00,0x3d,0x00,0x7c,0x00,0x00,0x18,0x46, //51 KEYPAD_EQUALS
        0x00,0x00,0x30, //52 KEYPAD_0
        0x00,0x00,0x31, //53 KEYPAD_1
        0x00,0x00,0x32, //54 KEYPAD_2
        0x00,0x00,0x33, //55 KEYPAD_3
        0x00,0x00,0x34, //56 KEYPAD_4
        0x00,0x00,0x35, //57 KEYPAD_5
        0x00,0x00,0x36, //58 KEYPAD_6
        0x00,0x00,0x37, //59 KEYPAD_7
        0x00,0xfe,0x39, //5a F20
        0x00,0x00,0x38, //5b KEYPAD_8
        0x00,0x00,0x39, //5c KEYPAD_9
        0xff,           //0x02,0x00,0xa5,0x00,0x7c, //5d JIS JAPANESE YEN
        0xff,           //0x00,0x00,0x5f, //5e JIS JAPANESE RO
        0xff,           //0x00,0x00,0x2c, //5f KEYPAD_COMMA, JIS only
        0x00,0xfe,0x24, //60 F5
        0x00,0xfe,0x25, //61 F6
        0x00,0xfe,0x26, //62 F7
        0x00,0xfe,0x22, //63 F3
        0x00,0xfe,0x27, //64 F8
        0x00,0xfe,0x28, //65 F9
        0xff, //66 JIS JAPANESE EISU, KOREAN HANJA
        0x00,0xfe,0x2a, //67 F11
        0xff, //68 JIS JAPANESE KANA, KOREAN HANGUL
        0x00,0xfe,0x32, //69 F13
        0x00,0xfe,0x35, //6a F16
        0x00,0xfe,0x33, //6b F14
        0xff, //6c
        0x00,0xfe,0x29, //6d F10
        0xff, //6e
        0x00,0xfe,0x2b, //6f F12
        0xff, //70
        0x00,0xfe,0x34, //71 F15
        0xff, //72 HELP
        0x00,0xfe,0x2e, //73 HOME
        0x00,0xfe,0x30, //74 PAGEUP
        0x00,0xfe,0x2d, //75 DELETE
        0x00,0xfe,0x23, //76 F4
        0x00,0xfe,0x2f, //77 END
        0x00,0xfe,0x21, //78 F2
        0x00,0xfe,0x31, //79 PAGEDOWN
        0x00,0xfe,0x20, //7a F1
        0x00,0x01,0xac, //7b LEFT ARROW
        0x00,0x01,0xae, //7c RIGHT ARROW
        0x00,0x01,0xaf, //7d DOWN ARROW
        0x00,0x01,0xad, //7e UP ARROW
        0x00,0x00,0x00, //7f POWER
        0x00,0x00,0x00,
        0x00,0x00,0x00, //81 Spotlight
        0x00,0x00,0x00, //82 Dashboard
        0x00,0x00,0x00, //83 Launchpad
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00, //90 Main Brightness Up
        0x00,0x00,0x00, //91 Main Brightness Down
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00, // a0 Exposes All
        0x00,0x00,0x00, // a1 Expose Desktop
        
        
        // key sequence definition
        // I tested some key sequence "Command + Shift + '['", but it doesn't work well.
        // No one sequence was used on key deff table now.
        0x11, // number of of sequence definitions
        // ( num of keys, generated sequence characters(char_set,char_code)... )
        // ( 0x02       , {0xff,0x04}, {0x00,0x31},                            )
        0x02,0xff,0x04,0x00,0x31, // Command + '1'
        0x02,0xff,0x04,0x00,0x32, // Command + '2'
        0x02,0xff,0x04,0x00,0x33, // Command + '3'
        0x02,0xff,0x04,0x00,0x34, // Command + '4'
        0x02,0xff,0x04,0x00,0x35, // Command + '5'
        0x02,0xff,0x04,0x00,0x36, // Command + '6'
        0x02,0xff,0x04,0x00,0x37, // Command + '7'
        0x02,0xff,0x04,0x00,0x38, // Command + '8'
        0x02,0xff,0x04,0x00,0x39, // Command + '9'
        0x02,0xff,0x04,0x00,0x30, // Command + '0'
        0x02,0xff,0x04,0x00,0x2d, // Command + '-'
        0x02,0xff,0x04,0x00,0x3d, // Command + '='
        0x02,0xff,0x04,0x00,0x70, // Command + 'p'
        0x02,0xff,0x04,0x00,0x5d, // Command + ']'
        0x02,0xff,0x04,0x00,0x5b, // Command + '['
        0x03,0xff,0x04,0xff,0x01,0x00,0x5b, // Command + Shift + '['
        0x03,0xff,0x04,0xff,0x01,0x00,0x5d, // Command + shift + ']'

       
        // special key definition
        0x0e, // number of special keys
        // ( NX_KEYTYPE,        Virtual ADB code )
        NX_KEYTYPE_CAPS_LOCK,   0x39,
        NX_KEYTYPE_HELP,        0x72,
        NX_POWER_KEY,           0x7f,
        NX_KEYTYPE_MUTE,        0x4a,
        NX_KEYTYPE_SOUND_UP,    0x48,
        NX_KEYTYPE_SOUND_DOWN,  0x49,
        // remove arrow keys as special keys. They are generating double up/down scroll events
        // in both carbon and coco apps.
        //NX_UP_ARROW_KEY,        0x7e,       // ADB is 3e raw, 7e virtual (KMAP)
        //NX_DOWN_ARROW_KEY,      0x7d,       // ADB is 0x3d raw, 7d virtual
        NX_KEYTYPE_NUM_LOCK,    0x47,       // ADB combines with CLEAR key for numlock
        NX_KEYTYPE_VIDMIRROR,   0x70,
        NX_KEYTYPE_PLAY,        0x34,
        NX_KEYTYPE_NEXT,        0x42,       // if this event repeated, act as NX_KEYTYPE_FAST
        NX_KEYTYPE_PREVIOUS,    0x4d,        // if this event repeated, act as NX_KEYTYPE_REWIND
    NX_KEYTYPE_BRIGHTNESS_UP, 0x90,
    NX_KEYTYPE_BRIGHTNESS_DOWN,  0x91,
        NX_KEYTYPE_EJECT,       0x92,
    };
 
    *length = sizeof(appleUSAKeyMap);
    return appleUSAKeyMap;
}

UInt32 HyperVKeyboard::maxKeyCodes() { return NX_NUMKEYCODES; }
