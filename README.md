# blePOD

## Components

### Button

Provides essentially one functions dispod_m5_buttons_test() to be called in blepod_m5stack_task regularly to check for new button events. If a button event happend, an ACTIVITY_EVENTS it raised (BLEPOD_BUTTON_TAP_EVT or BLEPOD_BUTTON_2SEC_RELEASE_EVT) to be evaluated in main.cpp.

### Idle Timer

Provides functionality to set an simple (counter) timer to a duration, start/stop and touch, and check whether the idle timer is expired.

## Temp

I (1442337) BLE_ADV_RECEIVER: (0x00070002) rssi -32 | temp  21.0 | hum  34 | x  -1072 | y  -1206 | z +16282 | batt 3084
