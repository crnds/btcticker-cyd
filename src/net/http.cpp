#include "http.h"
#include <WiFiClientSecure.h>
#include "config.h"

static WiFiClientSecure tls;

bool httpGet(HTTPClient& http, const char* url) {
  tls.setInsecure();               // v1: no cert pinning (see README roadmap)
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);            // no chunked encoding -> safe to stream-parse
  if (!http.begin(tls, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  return true;
}
