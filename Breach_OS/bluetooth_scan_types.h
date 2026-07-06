#pragma once

struct BluetoothScanDeviceInfo {
    String address;
    uint8_t addressType = 0;
    String name;
    bool haveRssi = false;
    int rssi = -999;
    bool haveTxPower = false;
    int8_t txPower = 0;
    bool haveAppearance = false;
    uint16_t appearance = 0;
    uint8_t advType = 0;
    bool legacy = false;
    bool scannable = false;
    bool connectable = false;
    int frameType = 0;
    String manufacturerHex;
    std::vector<String> serviceUuids;
    std::vector<String> serviceDataUuids;
    std::vector<String> serviceDataHex;
    std::vector<String> serviceDataAscii;
    std::vector<uint8_t> payload;
};
