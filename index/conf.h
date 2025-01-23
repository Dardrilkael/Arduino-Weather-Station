#pragma once
/******* Configuração de ambiente *******/

struct Config {
  char station_uid[64];
  char station_name[64];
  char wifi_ssid[64];
  char wifi_password[64];
  char mqtt_server[64]; 
  char mqtt_username[64]; 
  char mqtt_password[64]; 
  char mqtt_topic[64]; 
  int mqtt_port;
  int interval;
} ;

struct Config config;

const char *configFileName = "/config.txt";

/****** root certificate *********/

