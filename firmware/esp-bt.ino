#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ============================================================
// USER CONFIGURATION
// ============================================================

const char *WIFI_SSID     = "YOUR_WIFI_NETWORK_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char *API_URL =
  "http://192.168.68.XXX:8000/api/bluetooth/scan";

const char *SCANNER_ID = "solar-bt-01";

// Wait this long AFTER one complete scan finishes
// before starting the next scan.
constexpr uint32_t SCAN_INTERVAL_MS = 60000;

// BLE scan duration.
constexpr uint32_t BLE_SCAN_SECONDS = 12;

// Wi-Fi connection timeout.
constexpr uint32_t WIFI_TIMEOUT_MS = 10000;


// ============================================================
// HC-05 — CONFIRMED WORKING CONFIGURATION
// ============================================================

constexpr int HC05_RX_PIN = 4;  // ESP RX <- HC-05 TXD
constexpr int HC05_TX_PIN = 5;  // ESP TX -> HC-05 RXD

constexpr uint32_t HC05_BAUD = 38400;

HardwareSerial HC05(1);


// ============================================================
// LIMITS
// ============================================================

constexpr int MAX_CLASSIC_DEVICES = 12;
constexpr int MAX_BLE_DEVICES = 40;


// ============================================================
// CLASSIC STORAGE
// ============================================================

struct ClassicDevice
{
  String addressRaw;
  String addressCommand;

  String classRaw;
  String rssiRaw;

  String name;
  bool nameLookupAttempted;
  bool nameLookupSucceeded;
};

ClassicDevice classicDevices[MAX_CLASSIC_DEVICES];

int classicCount = 0;


// ============================================================
// BLE STORAGE
// ============================================================

String bleObservations[MAX_BLE_DEVICES];

int bleCount = 0;


// ============================================================
// GLOBAL SCAN STATE
// ============================================================

uint32_t scanId = 0;

uint32_t scanStartedAt = 0;
uint32_t scanFinishedAt = 0;
uint32_t previousCycleFinished = 0;

bool bleInitialized = false;


// ============================================================
// JSON HELPERS
// ============================================================

String jsonEscape(const String &input)
{
  String output;
  output.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); i++)
  {
    char c = input[i];

    switch (c)
    {
      case '\\':
        output += "\\\\";
        break;

      case '"':
        output += "\\\"";
        break;

      case '\n':
        output += "\\n";
        break;

      case '\r':
        output += "\\r";
        break;

      case '\t':
        output += "\\t";
        break;

      default:

        if ((uint8_t)c < 0x20)
        {
          char buffer[7];

          snprintf(
            buffer,
            sizeof(buffer),
            "\\u%04x",
            (uint8_t)c
          );

          output += buffer;
        }
        else
        {
          output += c;
        }

        break;
    }
  }

  return output;
}


String bytesToHex(
  const uint8_t *data,
  size_t length
)
{
  static const char hexChars[] =
    "0123456789ABCDEF";

  String output;
  output.reserve(length * 2);

  for (size_t i = 0; i < length; i++)
  {
    output += hexChars[
      (data[i] >> 4) & 0x0F
    ];

    output += hexChars[
      data[i] & 0x0F
    ];
  }

  return output;
}


String binaryStringToHex(
  const String &data
)
{
  return bytesToHex(
    reinterpret_cast<const uint8_t *>(
      data.c_str()
    ),
    data.length()
  );
}


// ============================================================
// WI-FI DIAGNOSTICS
// ============================================================

void listWiFiNetworks()
{
  Serial.println();
  Serial.println(
    "# Scanning visible Wi-Fi networks..."
  );

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  delay(250);

  int count = WiFi.scanNetworks();

  if (count <= 0)
  {
    Serial.println(
      "# No Wi-Fi networks detected."
    );
  }
  else
  {
    for (int i = 0; i < count; i++)
    {
      Serial.print("# ");
      Serial.print(i + 1);
      Serial.print(": \"");

      Serial.print(
        WiFi.SSID(i)
      );

      Serial.print("\" RSSI=");
      Serial.print(
        WiFi.RSSI(i)
      );

      Serial.print(" CH=");
      Serial.print(
        WiFi.channel(i)
      );

      if (
        WiFi.encryptionType(i) ==
        WIFI_AUTH_OPEN
      )
      {
        Serial.println(" OPEN");
      }
      else
      {
        Serial.println(" SECURED");
      }
    }
  }

  WiFi.scanDelete();

  WiFi.mode(WIFI_OFF);

  delay(100);
}


// ============================================================
// WI-FI CONNECT
// ============================================================

bool connectWiFi()
{
  Serial.println();

  Serial.print(
    "# Connecting Wi-Fi: "
  );

  Serial.println(
    WIFI_SSID
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  uint32_t started =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started < WIFI_TIMEOUT_MS
  )
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    Serial.println(
      "# Wi-Fi connected"
    );

    Serial.print(
      "# IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.print(
      "# RSSI: "
    );

    Serial.println(
      WiFi.RSSI()
    );

    return true;
  }

  Serial.println(
    "# Wi-Fi connection FAILED"
  );

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  return false;
}


// ============================================================
// HC-05 HELPERS
// ============================================================

void clearHC05()
{
  while (HC05.available())
  {
    HC05.read();
  }
}


String readHC05(
  uint32_t timeoutMs
)
{
  String response;

  uint32_t started =
    millis();

  while (
    millis() - started <
    timeoutMs
  )
  {
    while (HC05.available())
    {
      response +=
        (char)HC05.read();
    }

    delay(1);
  }

  return response;
}


String sendAT(
  const String &command,
  uint32_t timeoutMs = 1000
)
{
  clearHC05();

  HC05.print(command);
  HC05.print("\r\n");

  return readHC05(
    timeoutMs
  );
}


// ============================================================
// CLASSIC INQUIRY PARSER
// ============================================================

void parseClassicInquiryLine(
  String line
)
{
  line.trim();

  if (
    !line.startsWith("+INQ:")
  )
  {
    return;
  }

  if (
    classicCount >=
    MAX_CLASSIC_DEVICES
  )
  {
    return;
  }

  line.remove(0, 5);

  int comma1 =
    line.indexOf(',');

  int comma2 =
    line.indexOf(
      ',',
      comma1 + 1
    );

  if (
    comma1 < 0 ||
    comma2 < 0
  )
  {
    return;
  }

  ClassicDevice &device =
    classicDevices[classicCount];

  device.addressRaw =
    line.substring(
      0,
      comma1
    );

  device.classRaw =
    line.substring(
      comma1 + 1,
      comma2
    );

  device.rssiRaw =
    line.substring(
      comma2 + 1
    );

  device.addressRaw.trim();
  device.classRaw.trim();
  device.rssiRaw.trim();

  device.addressCommand =
    device.addressRaw;

  device.addressCommand.replace(
    ":",
    ","
  );

  device.name = "";

  device.nameLookupAttempted =
    false;

  device.nameLookupSucceeded =
    false;

  classicCount++;
}


// ============================================================
// CLASSIC REMOTE NAME
// ============================================================

String lookupClassicName(
  const ClassicDevice &device
)
{
  clearHC05();

  String command =
    "AT+RNAME?";

  command +=
    device.addressCommand;

  HC05.print(command);
  HC05.print("\r\n");

  String response =
    readHC05(3000);

  int position =
    response.indexOf("+RNAME:");

  if (position < 0)
  {
    return "";
  }

  position += 7;

  int end =
    response.indexOf(
      '\r',
      position
    );

  if (end < 0)
  {
    end =
      response.indexOf(
        '\n',
        position
      );
  }

  if (end < 0)
  {
    end =
      response.length();
  }

  String name =
    response.substring(
      position,
      end
    );

  name.trim();

  return name;
}


// ============================================================
// CLASSIC SCAN
// ============================================================

void scanClassic()
{
  classicCount = 0;

  Serial.println();

  Serial.println(
    "# ========================================"
  );

  Serial.println(
    "# CLASSIC BLUETOOTH SCAN"
  );

  Serial.println(
    "# ========================================"
  );


  String response =
    sendAT("AT");

  Serial.print(
    "# AT: "
  );

  Serial.println(
    response
  );


  sendAT(
    "AT+ROLE=1"
  );

  sendAT(
    "AT+INQM=0,9,9"
  );


  response =
    sendAT(
      "AT+INIT",
      1500
    );

  if (
    response.indexOf(
      "ERROR:(17)"
    ) >= 0
  )
  {
    Serial.println(
      "# HC-05 already initialized."
    );
  }


  clearHC05();

  Serial.println(
    "# Running AT+INQ..."
  );

  HC05.print(
    "AT+INQ\r\n"
  );


  uint32_t started =
    millis();

  constexpr uint32_t
    inquiryTimeout = 14000;

  String line;


  while (
    millis() - started <
    inquiryTimeout
  )
  {
    while (HC05.available())
    {
      char c =
        HC05.read();

      if (c == '\n')
      {
        line.trim();

        if (
          line.startsWith("+INQ:")
        )
        {
          Serial.print(
            "# CLASSIC RAW: "
          );

          Serial.println(
            line
          );

          parseClassicInquiryLine(
            line
          );
        }

        line = "";
      }
      else if (c != '\r')
      {
        line += c;
      }
    }

    delay(1);
  }


  Serial.print(
    "# Classic devices discovered: "
  );

  Serial.println(
    classicCount
  );


  // Opportunistic remote-name lookup.

  for (
    int i = 0;
    i < classicCount;
    i++
  )
  {
    ClassicDevice &device =
      classicDevices[i];

    device.nameLookupAttempted =
      true;

    device.name =
      lookupClassicName(
        device
      );

    device.nameLookupSucceeded =
      device.name.length() > 0;

    Serial.print(
      "# RNAME "
    );

    Serial.print(
      device.addressRaw
    );

    Serial.print(": ");

    if (
      device.nameLookupSucceeded
    )
    {
      Serial.println(
        device.name
      );
    }
    else
    {
      Serial.println(
        "[no response]"
      );
    }
  }
}


// ============================================================
// CLASSIC CLASS-OF-DEVICE DECODING
// ============================================================

uint8_t classicFormatType(
  uint32_t cod
)
{
  return cod & 0x03;
}


uint8_t classicMajorCode(
  uint32_t cod
)
{
  return (
    cod >> 8
  ) & 0x1F;
}


uint8_t classicMinorCode(
  uint32_t cod
)
{
  return (
    cod >> 2
  ) & 0x3F;
}


uint16_t classicServiceBits(
  uint32_t cod
)
{
  return (
    cod >> 13
  ) & 0x07FF;
}


String classicMajorName(
  uint8_t major
)
{
  switch (major)
  {
    case 0x00:
      return "miscellaneous";

    case 0x01:
      return "computer";

    case 0x02:
      return "phone";

    case 0x03:
      return "lan_network";

    case 0x04:
      return "audio_video";

    case 0x05:
      return "peripheral";

    case 0x06:
      return "imaging";

    case 0x07:
      return "wearable";

    case 0x08:
      return "toy";

    case 0x09:
      return "health";

    case 0x1F:
      return "uncategorized";

    default:
      return "reserved";
  }
}


String classicMinorName(
  uint8_t major,
  uint8_t minor
)
{
  switch (major)
  {
    // COMPUTER
    case 0x01:

      switch (minor & 0x3F)
      {
        case 0x00:
          return "uncategorized";

        case 0x01:
          return "desktop";

        case 0x02:
          return "server";

        case 0x03:
          return "laptop";

        case 0x04:
          return "handheld";

        case 0x05:
          return "palm";

        case 0x06:
          return "wearable_computer";

        case 0x07:
          return "tablet";

        default:
          return "other";
      }


    // PHONE
    case 0x02:

      switch (minor & 0x3F)
      {
        case 0x00:
          return "uncategorized";

        case 0x01:
          return "cellular";

        case 0x02:
          return "cordless";

        case 0x03:
          return "smartphone";

        case 0x04:
          return "wired_modem_or_voice_gateway";

        case 0x05:
          return "common_isdn_access";

        default:
          return "other";
      }


    // AUDIO / VIDEO
    case 0x04:

      switch (minor & 0x3F)
      {
        case 0x00:
          return "uncategorized";

        case 0x01:
          return "headset";

        case 0x02:
          return "handsfree";

        case 0x04:
          return "microphone";

        case 0x05:
          return "loudspeaker";

        case 0x06:
          return "headphones";

        case 0x07:
          return "portable_audio";

        case 0x08:
          return "car_audio";

        case 0x09:
          return "set_top_box";

        case 0x0A:
          return "hifi_audio";

        case 0x0B:
          return "vcr";

        case 0x0C:
          return "video_camera";

        case 0x0D:
          return "camcorder";

        case 0x0E:
          return "video_monitor";

        case 0x0F:
          return "video_display_and_loudspeaker";

        case 0x10:
          return "video_conferencing";

        case 0x12:
          return "gaming_toy";

        default:
          return "other";
      }


    // WEARABLE
    case 0x07:

      switch (minor & 0x3F)
      {
        case 0x01:
          return "wristwatch";

        case 0x02:
          return "pager";

        case 0x03:
          return "jacket";

        case 0x04:
          return "helmet";

        case 0x05:
          return "glasses";

        default:
          return "other";
      }


    default:
      return "unspecified";
  }
}


String classicServiceJSON(
  uint16_t serviceBits
)
{
  String json = "[";

  bool first = true;

  auto addService =
    [&](const char *name)
  {
    if (!first)
    {
      json += ",";
    }

    json += "\"";
    json += name;
    json += "\"";

    first = false;
  };


  if (
    serviceBits &
    (1 << 0)
  )
  {
    addService(
      "limited_discoverable"
    );
  }

  if (
    serviceBits &
    (1 << 3)
  )
  {
    addService(
      "positioning"
    );
  }

  if (
    serviceBits &
    (1 << 4)
  )
  {
    addService(
      "networking"
    );
  }

  if (
    serviceBits &
    (1 << 5)
  )
  {
    addService(
      "rendering"
    );
  }

  if (
    serviceBits &
    (1 << 6)
  )
  {
    addService(
      "capturing"
    );
  }

  if (
    serviceBits &
    (1 << 7)
  )
  {
    addService(
      "object_transfer"
    );
  }

  if (
    serviceBits &
    (1 << 8)
  )
  {
    addService(
      "audio"
    );
  }

  if (
    serviceBits &
    (1 << 9)
  )
  {
    addService(
      "telephony"
    );
  }

  if (
    serviceBits &
    (1 << 10)
  )
  {
    addService(
      "information"
    );
  }


  json += "]";

  return json;
}


// ============================================================
// CLASSIC JSON OBJECT
// ============================================================

String buildClassicObject(
  const ClassicDevice &device
)
{
  uint32_t classValue =
    strtoul(
      device.classRaw.c_str(),
      nullptr,
      16
    );

  uint8_t formatType =
    classicFormatType(
      classValue
    );

  uint8_t majorCode =
    classicMajorCode(
      classValue
    );

  uint8_t minorCode =
    classicMinorCode(
      classValue
    );

  uint16_t serviceBits =
    classicServiceBits(
      classValue
    );


  bool rssiValid =
    !device.rssiRaw.equalsIgnoreCase(
      "7FFF"
    );

  int16_t rssiDbm = 0;

  if (rssiValid)
  {
    uint16_t raw =
      strtoul(
        device.rssiRaw.c_str(),
        nullptr,
        16
      );

    rssiDbm =
      (int16_t)raw;
  }


  String json;

  json.reserve(600);

  json += "{";


  json +=
    "\"address\":\"";

  json +=
    jsonEscape(
      device.addressRaw
    );

  json += "\"";


  json +=
    ",\"name\":";

  if (
    device.name.length()
  )
  {
    json += "\"";

    json +=
      jsonEscape(
        device.name
      );

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"name_lookup_attempted\":";

  json +=
    device.nameLookupAttempted
      ? "true"
      : "false";


  json +=
    ",\"name_lookup_succeeded\":";

  json +=
    device.nameLookupSucceeded
      ? "true"
      : "false";


  json +=
    ",\"class_raw\":\"";

  json +=
    jsonEscape(
      device.classRaw
    );

  json += "\"";


  json +=
    ",\"class_value\":";

  json +=
    String(classValue);


  json +=
    ",\"class_format_type\":";

  json +=
    String(formatType);


  json +=
    ",\"class_major_code\":";

  json +=
    String(majorCode);


  json +=
    ",\"class_major\":\"";

  json +=
    classicMajorName(
      majorCode
    );

  json += "\"";


  json +=
    ",\"class_minor_code\":";

  json +=
    String(minorCode);


  json +=
    ",\"class_minor\":\"";

  json +=
    classicMinorName(
      majorCode,
      minorCode
    );

  json += "\"";


  json +=
    ",\"service_class_bits\":";

  json +=
    String(serviceBits);


  json +=
    ",\"services\":";

  json +=
    classicServiceJSON(
      serviceBits
    );


  json +=
    ",\"rssi_raw\":\"";

  json +=
    jsonEscape(
      device.rssiRaw
    );

  json += "\"";


  json +=
    ",\"rssi_dbm\":";

  if (rssiValid)
  {
    json +=
      String(rssiDbm);
  }
  else
  {
    json += "null";
  }


  json += "}";

  return json;
}


// ============================================================
// BLE JSON OBJECT
// ============================================================

String buildBLEObject(
  BLEAdvertisedDevice &device
)
{
  String json;

  json.reserve(700);

  json += "{";


  json +=
    "\"address\":\"";

  json +=
    device
      .getAddress()
      .toString()
      .c_str();

  json += "\"";


  json +=
    ",\"address_type_raw\":";

  json +=
    String(
      device.getAddressType()
    );


  json +=
    ",\"rssi\":";

  json +=
    String(
      device.getRSSI()
    );


  json +=
    ",\"name\":";

  if (
    device.haveName()
  )
  {
    json += "\"";

    json +=
      jsonEscape(
        String(
          device
            .getName()
            .c_str()
        )
      );

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"tx_power\":";

  if (
    device.haveTXPower()
  )
  {
    json +=
      String(
        device.getTXPower()
      );
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"appearance\":";

  if (
    device.haveAppearance()
  )
  {
    json +=
      String(
        device.getAppearance()
      );
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"manufacturer_data_hex\":";

  if (
    device.haveManufacturerData()
  )
  {
    String data =
      device.getManufacturerData();

    json += "\"";

    json +=
      binaryStringToHex(
        data
      );

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"service_uuid\":";

  if (
    device.haveServiceUUID()
  )
  {
    json += "\"";

    json +=
      device
        .getServiceUUID()
        .toString()
        .c_str();

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"service_data_hex\":";

  if (
    device.haveServiceData()
  )
  {
    String data =
      device.getServiceData();

    json += "\"";

    json +=
      binaryStringToHex(
        data
      );

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"service_data_uuid\":";

  if (
    device.haveServiceData()
  )
  {
    json += "\"";

    json +=
      device
        .getServiceDataUUID()
        .toString()
        .c_str();

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json +=
    ",\"raw_payload_hex\":";


  uint8_t *payload =
    device.getPayload();

  size_t payloadLength =
    device.getPayloadLength();


  if (
    payload &&
    payloadLength > 0
  )
  {
    json += "\"";

    json +=
      bytesToHex(
        payload,
        payloadLength
      );

    json += "\"";
  }
  else
  {
    json += "null";
  }


  json += "}";

  return json;
}


// ============================================================
// BLE SCAN
// ============================================================

void scanBLE()
{
  bleCount = 0;

  Serial.println();

  Serial.println(
    "# ========================================"
  );

  Serial.println(
    "# BLE SCAN"
  );

  Serial.println(
    "# ========================================"
  );


  if (!bleInitialized)
  {
    BLEDevice::init("");

    bleInitialized = true;
  }


  BLEScan *scan =
    BLEDevice::getScan();

  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);


  Serial.print(
    "# Scanning BLE for "
  );

  Serial.print(
    BLE_SCAN_SECONDS
  );

  Serial.println(
    " seconds..."
  );


  BLEScanResults *results =
    scan->start(
      BLE_SCAN_SECONDS,
      false
    );


  int discovered =
    results->getCount();


  Serial.print(
    "# BLE devices discovered: "
  );

  Serial.println(
    discovered
  );


  int numberToStore =
    min(
      discovered,
      MAX_BLE_DEVICES
    );


  for (
    int i = 0;
    i < numberToStore;
    i++
  )
  {
    BLEAdvertisedDevice device =
      results->getDevice(i);

    bleObservations[bleCount] =
      buildBLEObject(
        device
      );

    bleCount++;
  }


  scan->clearResults();


  Serial.print(
    "# BLE devices stored: "
  );

  Serial.println(
    bleCount
  );
}


// ============================================================
// BUILD ONE UNIFIED SCAN PAYLOAD
// ============================================================

String buildBatchJSON()
{
  String json;

  json.reserve(16000);

  json += "{";


  json +=
    "\"scanner_id\":\"";

  json +=
    jsonEscape(
      SCANNER_ID
    );

  json += "\"";


  json +=
    ",\"scan_id\":";

  json +=
    String(scanId);


  json +=
    ",\"scan_started_uptime_ms\":";

  json +=
    String(scanStartedAt);


  json +=
    ",\"scan_finished_uptime_ms\":";

  json +=
    String(scanFinishedAt);


  json +=
    ",\"classic_count\":";

  json +=
    String(classicCount);


  json +=
    ",\"ble_count\":";

  json +=
    String(bleCount);


  json +=
    ",\"classic\":[";


  for (
    int i = 0;
    i < classicCount;
    i++
  )
  {
    if (i > 0)
    {
      json += ",";
    }

    json +=
      buildClassicObject(
        classicDevices[i]
      );
  }


  json += "]";


  json +=
    ",\"ble\":[";


  for (
    int i = 0;
    i < bleCount;
    i++
  )
  {
    if (i > 0)
    {
      json += ",";
    }

    json +=
      bleObservations[i];
  }


  json += "]";

  json += "}";

  return json;
}


// ============================================================
// POST ONE COMPLETE SCAN
// ============================================================

bool postScan(
  const String &payload
)
{
  if (!connectWiFi())
  {
    Serial.println(
      "# Upload skipped."
    );

    return false;
  }


  Serial.println();

  Serial.println(
    "# Posting complete scan..."
  );


  HTTPClient http;

  http.setTimeout(10000);


  if (
    !http.begin(API_URL)
  )
  {
    Serial.println(
      "# HTTP begin failed."
    );

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    return false;
  }


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  int responseCode =
    http.POST(
      payload
    );


  Serial.print(
    "# HTTP response: "
  );

  Serial.println(
    responseCode
  );


  if (
    responseCode > 0
  )
  {
    String response =
      http.getString();

    if (
      response.length()
    )
    {
      Serial.print(
        "# Server response: "
      );

      Serial.println(
        response
      );
    }
  }
  else
  {
    Serial.print(
      "# HTTP error: "
    );

    Serial.println(
      http.errorToString(
        responseCode
      )
    );
  }


  http.end();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println(
    "# Wi-Fi OFF"
  );


  return (
    responseCode >= 200 &&
    responseCode < 300
  );
}


// ============================================================
// COMPLETE SCAN CYCLE
// ============================================================

void runScanCycle()
{
  scanId++;

  scanStartedAt =
    millis();


  Serial.println();
  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.print(
    " SCAN CYCLE "
  );

  Serial.println(
    scanId
  );

  Serial.println(
    "========================================"
  );


  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(100);


  scanClassic();

  scanBLE();


  scanFinishedAt =
    millis();


  String payload =
    buildBatchJSON();


  Serial.println();

  Serial.println(
    "# ========================================"
  );

  Serial.println(
    "# COMPLETE JSON PAYLOAD"
  );

  Serial.println(
    "# ========================================"
  );

  Serial.println(
    payload
  );


  Serial.print(
    "# Payload bytes: "
  );

  Serial.println(
    payload.length()
  );


  postScan(
    payload
  );


  Serial.println();

  Serial.println(
    "# Scan cycle finished."
  );


  previousCycleFinished =
    millis();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(2000);


  Serial.println();

  Serial.println(
    "ESP32-C3 SOLAR BLUETOOTH OBSERVER"
  );


  HC05.begin(
    HC05_BAUD,
    SERIAL_8N1,
    HC05_RX_PIN,
    HC05_TX_PIN
  );


  listWiFiNetworks();


  runScanCycle();
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  if (
    millis() -
    previousCycleFinished >=
    SCAN_INTERVAL_MS
  )
  {
    runScanCycle();
  }

  delay(100);
}
