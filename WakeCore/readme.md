## V0.2.7.1
* Add detection of wake-up events for long and long button presses

## V0.2.7
* Improve and optimize the bridging module
* Added firmware version information exchange instruction
* Added USB power supply connection status acquisition command

## V0.2.6
* Added USB power activation detection and adaptively configure relevant pins when entering sleep mode

## V0.2.5
* Added PIR module (NP624M-F) and corresponding processing logic
* Added PIR configuration commands and watchdog wakeup flags

## V0.2.4
* Added independent watchdog
* Added boot OPT byte detection, automatically disable watchdog counting in Standby and Stop modes, and enable Flash read protection

## V0.2.3
* Fixed RTC ALARM unable to wake up in STOP2 mode

## V0.2.2
* Optimized and fixed several issues and risky code

## V0.2.1
* Disabled heartbeat mechanism
* Improved PIR wakeup configuration
* Added wakeup flag clearing and N6 reset commands

## V0.2.0
* Implemented protocol bridging module with N6
* Refactored APP logic based on bridging module
* Added several features

## V0.1.5
* Improved sleep and wakeup logic

## V0.1.1
* Added STOP2 mode command
* Added several features

## V0.1.0
* Completed communication module with N6 chip
* Implemented simple test functionality

## Init
* Initialized project