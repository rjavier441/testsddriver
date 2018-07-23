#include <SPI.h>
#include <string.h> // strncmp()

#define CMDMAX 50

// TODO: Ensure you know which voltage level the SPI card reader's chip operates on BEFORE connecting it!

// Arduino UNO has these pins hard-locked to these functions:
//cosnt int pinSCK = 13;
//const int pinMISO = 12;
//const int pinMOSI = 11;
const int pinCS = 10;

typedef enum {    // NOTE: the OR'ing of b0100_0000 is meant to keep in line with the SD protocol (i.e. first bits must be b01)
    RESET       = 0x04 | 0x00,     // 0: reset the sd card
    GETOP       = 0x04 | 0x08,     // 8: request the sd card's current operational conditions
    ACHEADER    = 0x04 | 0x37,     // 55: the leading command for application-specific commands
    ACINIT      = 0x04 | 0x29,     // 41: starts an initiation of the card
    GETOCR      = 0x04 | 0x3A,     // 58: request data from the operational conditions register
    CHGBL       = 0x04 | 0x10,     // 16: change block length
    READ        = 0x04 | 0x11,     // 17: read a block of data
    WRITE       = 0x04 | 0x18,     // 24: write a block of data
    DELFROM     = 0x04 | 0x20,     // 32: signal the start block for deletion
    DELTO       = 0x04 | 0x21,     // 33: signal the end block for deletion
    DEL         = 0x04 | 0x26      // 38: begin deletion from the block range specified by the [DELFROM : DELTO] commands
} sdCommand;
typedef enum {
  READY,
  PROCESSING,
  IDLE
} State;
char cmd[CMDMAX]= {};
State cs = IDLE;
State ns = IDLE;

void gotoNextState() {
  cs = ns;
}

void printHelp () {
  Serial.println("SJdev2_sdTest Commands:");
  Serial.println("  End all commands by pressing enter");
  Serial.println("\t'echo': Echoes 'echo' back to you");
  Serial.println("\t'help': Prints this help prompt");
  Serial.println("\t'test': Runs the test script");
}

uint64_t sdWrite(sdCommand sdc, uint32_t arg) {
  int crc = 0x00;
  uint64_t response = 0x00000000;

  // TODO: Ensure I'm using the correct CRC7 calculation algorithm
  // Calculate the 7-bit CRC (i.e. CRC7) using the SD card standard's algorithm
  int generator = (sdc >> 7) + ((sdc & 0x03) >> 3) + 1;
  int multiplicand = 0x00;
  uint64_t tempFrame = ((uint64_t) sdc << 32) | arg;
  for (int i = 39; i >= 0; i--) {
    int xn = (tempFrame >> i) * 0x01;         // get the i'th bit of the tempFrame
    int yn = (tempFrame >> (39 - i)) & 0x01;  // get the (39-i)'th bit of the tempFrame
    multiplicand += yn * xn;
  }
  crc = (multiplicand * (sdc >> 7)) % generator;

  // Send a command frame to the SD card board
  digitalWrite(pinCS, LOW);                               // select the SPI comm board
  response |= SPI.transfer(sdc);                          // begin by transfering the command byte
  response << 8;
  response |= SPI.transfer((int) (arg >> 24) & 0xFF);     // send argument byte [31:24]
  response << 8;
  response |= SPI.transfer((int) (arg >> 16) & 0xFF);     // send argument byte [23:16]
  response << 8;
  response |= SPI.transfer((int) (arg >> 8) & 0xFF);      // send argument byte [15:8]
  response << 8;
  response |= SPI.transfer((int) (arg >> 0) & 0xFF);      // send argument byte [7:0]
  response << 8;
  response |= SPI.transfer((int) (crc << 1) | 0x01);      // send 7-bit CRC and LSB stop addr (as b1)
  digitalWrite(pinCS, HIGH);                              // deselect the SPI comm board
  return response;
}

void processCmd(char* cmd) {
  if (strncmp(cmd, "help", CMDMAX) == 0) {
    printHelp();
  } else if (strncmp(cmd, "echo", CMDMAX) == 0) {
    Serial.println("echo");
  } else if (strncmp(cmd, "test", CMDMAX) == 0) {
    Serial.println("Executing Test...");  // Read OCR register
    uint64_t response = sdWrite(GETOP,0x00);
    Serial.print("Response:");
    Serial.print((int) (response >> 32) & 0xFF, HEX);
    Serial.print((int) (response >> 24) & 0xFF, HEX);
    Serial.print((int) (response >> 16) & 0xFF, HEX);
    Serial.print((int) (response >> 8) & 0xFF, HEX);
    Serial.println((int) response & 0xFF, HEX);
  } else {
    Serial.println("Unrecognized command!");
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  // Activate CS pins (all other pins are activated by SPI
  pinMode(pinCS, OUTPUT);
  digitalWrite(pinCS, HIGH);  // deactivate CS at start
  SPI.begin();
  printHelp();
}

void loop() {
  switch (cs) {
    case IDLE: {
      Serial.print("CMD>");
      ns = READY;
      break;
    }
    case READY: {
      // Acquire command input
      if (Serial.available() > 0) {
        int end = Serial.readBytesUntil('\n', cmd, CMDMAX);
        if (end < CMDMAX) cmd[end] = '\0';

        // After acquiring command, proceed to process that command
        ns = (PROCESSING);
      }
      break;
    }
    case PROCESSING: {
      Serial.println(cmd);

      processCmd(cmd);

      // END state
      ns = (IDLE);
      break;
    }
  }

  gotoNextState();
}
