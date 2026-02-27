// --- audiodata.h (PROGMEM Hex Data) ---
#ifndef AUDIODATA_H
#define AUDIODATA_H
#include <Arduino.h>

// 1. HAPPY SOUND: "Thank you" (Played when lid opens)
// Replace the placeholder below with your actual happy hex data
const unsigned char rawData_Happy[] PROGMEM = {
  0x80, 0x80, 0x81, 0x81, 0x82 // ... (Put your thousands of happy hex bytes here)
};
const unsigned int rawData_Happy_len = sizeof(rawData_Happy);

// 2. SAD SOUND: "Wrong bin" (Played for non-recyclable items)
// Replace the placeholder below with your actual sad hex data
const unsigned char rawData_Sad[] PROGMEM = {
  0x7F, 0x7E, 0x7D, 0x7C, 0x7B // ... (Put your thousands of sad hex bytes here)
};
const unsigned int rawData_Sad_len = sizeof(rawData_Sad);

#endif // AUDIODATA_H
