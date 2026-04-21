#pragma once

#include <Arduino.h>

struct DisplayApiConfig {
    String serialNumber;
    String factoryUrl;
    String factoryKey;
    String customerUrl;
    String customerKey;
};

void displayApiService();
void displayApiPrintStatus();
bool displayApiIsBusy();

DisplayApiConfig displayApiLoadConfig();
void displayApiSetSerialNumber(const String& serialNumber);
void displayApiSetFactoryUrl(const String& url);
void displayApiSetFactoryKey(const String& key);
void displayApiSetCustomerUrl(const String& url);
void displayApiSetCustomerKey(const String& key);

