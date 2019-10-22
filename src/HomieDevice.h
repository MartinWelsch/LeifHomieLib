#pragma once

#include "AsyncMqttClient.h"
#include "HomieNode.h"
#include <map>


#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#else
#include "WiFi.h"
#endif

#define HOMIELIB_VERBOSE

typedef std::map<String, HomieProperty *> _map_incoming;

typedef std::function<void(const char *szText)> HomieDebugPrintCallback;

void HomieLibRegisterDebugPrintCallback(HomieDebugPrintCallback cb);

String HomieDeviceName(const char *in);

bool HomieParseRGB(const char *in, uint32_t &rgb);
bool HomieParseHSV(const char *in, uint32_t &rgb);

class HomieDevice
{
public:
	HomieDevice();

	int iInitialPublishingThrottle_ms = 200;

	bool bRapidUpdateRSSI = false;

	void Init();
	void Quit();

	void Loop();

	HomieNode *NewNode();

	bool IsConnected();

	uint16_t PublishDirect(const String &topic, uint8_t qos, bool retain, const String &payload);

	AsyncMqttClient mqtt;

	unsigned long GetUptimeSeconds_WiFi();
	unsigned long GetUptimeSeconds_MQTT();

	void setServer(IPAddress ip, uint8_t port, const char *username = NULL, const char *password = NULL);
	void setServer(const char* host, uint8_t port, const char *username = NULL, const char *password = NULL);
	void setServerCredentials(const char *username, const char *password);

private:
	uint16_t Publish(const char *topic, uint8_t qos, bool retain, const char *payload = nullptr, size_t length = 0, bool dup = false, uint16_t message_id = 0);

	friend class HomieNode;
	friend class HomieProperty;

	bool useIp = true;
	const char *mqttServerHost;
	IPAddress mqttServerIp;
	uint8_t mqttServerPort;

	const char *mqttUsername;
	const char *mqttPassword;

	String friendlyName;
	String id;

	void DoInitialPublishing();

	unsigned long mqttReconnectCount = 0;
	unsigned long homieStatsTimestamp = 0;
	unsigned long lastReconnect = 0;

	void onConnect(bool sessionPresent);
	void onDisconnect(AsyncMqttClientDisconnectReason reason);
	void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);

	bool connecting = false;

	bool doInitialPublishing = false;

	bool initialPublishingDone = false;

	int initialPublishing = 0;
	int initialPublishing_Node = 0;
	int initialPublishing_Prop = 0;

	int pubCount_Props = 0;

	unsigned long initialPublishing = 0;

	unsigned long connectTimestamp = 0;

	bool debug = false;

	bool initialized = false;

	String topic;
	char szWillTopic[128];

	std::vector<HomieNode *> node;

	_map_incoming incoming;

	unsigned long secondCounter_Uptime = 0;
	unsigned long secondCounter_WiFi = 0;
	unsigned long secondCounter_MQTT = 0;

	unsigned long lastLoopSecondCounterTimestamp = 0;
	unsigned long lastLoopDeciSecondCounterTimestamp = 0;

	bool doPublishDefaults = false; //publish default retained values that did not yet exist in the controller
	unsigned long publishDefaultsTimestamp = 0;

	void HandleInitialPublishingError();

	bool sendError = false;
	unsigned long sendErrorTimestamp;

	int GetErrorRetryFrequency();

	unsigned long GetReconnectInterval();
};
