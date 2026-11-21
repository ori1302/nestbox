// Console replies that the Mac app's Wi-Fi setup reads (companion/mac/app/ConsoleReply.swift holds the same strings).
#pragma once

namespace console_reply {

constexpr const char* WIFI = "wifi: ";            // starts each Wi-Fi report: the answer to `wifi`, and when it joins
constexpr const char* JOINING = "joining \"";     // credentials taken: joining "<ssid>"
// net::status() on a network: on "<ssid>" as <ip> (http://<host>.local), signal <rssi> dBm
constexpr const char* ONLINE = "on \"";
constexpr const char* ADDRESS = "\" as ";
constexpr const char* HOST_URL = " (http://";

}  // namespace console_reply
