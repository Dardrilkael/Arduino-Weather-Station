#pragma once
#include "FS.h"
#include "SD.h"

// Sends a CSV file via HTTP POST to the given URL.
// Returns true on HTTP 2xx response.
bool sendCSVFile(File &file, const char *url, const char *id = "0");

// Iterates files in dirPath, uploading up to `amount` files to the recovery
// server. Skips today's file (todayDateString) and files prefixed with '@'.
// Returns true if at least one file was successfully sent.
bool processFiles(const char *dirPath, const char *todayDateString = nullptr, int amount = 1);