#pragma once
#define DEBUG_MODE

#ifdef DEBUG_MODE
#define OnDebug(x) x
#else
#define OnDebug(x)
#endif

#define DEBUG_LOG_ENABLED 1

#if DEBUG_LOG_ENABLED
#define logDebug(msg) Serial.print(msg)
#define logDebugln(msg) Serial.println(msg)
#define logDebugf(...) Serial.printf(__VA_ARGS__)

#else
#define logDebug(msg)
#define logDebugln(msg)
#define logDebugf(...)
#endif