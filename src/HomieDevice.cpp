#include "HomieDevice.h"
#include "HomieNode.h"
void HomieLibDebugPrint(const char *szText);

#define csprintf(...)                 \
	{                                 \
		char szTemp[256];             \
		sprintf(szTemp, __VA_ARGS__); \
		HomieLibDebugPrint(szTemp);   \
	}

static std::vector<HomieDebugPrintCallback> vecDebugPrint;

const int ipub_qos = 1;
const int sub_qos = 2;

void HomieLibRegisterDebugPrintCallback(HomieDebugPrintCallback cb)
{
	vecDebugPrint.push_back(cb);
}

void HomieLibDebugPrint(const char *szText)
{
	for (size_t i = 0; i < vecDebugPrint.size(); i++)
	{
		vecDebugPrint[i](szText);
	}
}

HomieDevice *pToken = NULL;

bool AllowInitialPublishing(HomieDevice *pSource)
{
	if (pToken == pSource)
		return true;
	if (!pToken)
	{
		pToken = pSource;
		return true;
	}
	return false;
}

void FinishInitialPublishing(HomieDevice *pSource)
{
	if (pToken == pSource)
	{
		pToken = NULL;
	}
}

//#define HOMIELIB_VERBOSE

HomieDevice::HomieDevice()
{
}

void HomieDevice::Init()
{

	topic = String("homie/") + id;
	strcpy(szWillTopic, String(topic + "/$state").c_str());

	if (!node.size())
	{
		HomieNode *pNode = NewNode();

		pNode->id = "dummy";
		pNode->friendlyName = "No Nodes";

		/*
 	 	lots of mqtt:homie300:srvrm-lightsense:dummy#dummy triggered in the log

  		HomieProperty * pProp=pNode->NewProperty();
		pProp->strID="dummy";
		pProp->strFriendlyName="No Properties";
		pProp->bRetained=false;
		pProp->datatype=homieString;*/
	}

	for (size_t a = 0; a < node.size(); a++)
	{
		node[a]->Init();
	}

	if (this->useIp)
	{
		mqtt.setServer(this->mqttServerIp, this->mqttServerPort);
	}
	else
	{
		mqtt.setServer(this->mqttServerHost, this->mqttServerPort);
	}

	if (this->mqttUsername != NULL && this->mqttPassword != NULL)
	{
		mqtt.setCredentials(this->mqttUsername, this->mqttPassword);
	}

	mqtt.setWill(szWillTopic, 2, true, "lost");

	mqtt.onConnect(std::bind(&HomieDevice::onConnect, this, std::placeholders::_1));
	mqtt.onDisconnect(std::bind(&HomieDevice::onDisconnect, this, std::placeholders::_1));
	mqtt.onMessage(std::bind(&HomieDevice::onMqttMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));

	sendError = false;

	initialized = true;
}

void HomieDevice::Quit()
{
	Publish(String(topic + "/$state").c_str(), 1, true, "disconnected");
	mqtt.disconnect(false);
	initialized = false;
}

bool HomieDevice::IsConnected()
{
	return mqtt.connected();
}

int iWiFiRSSI = 0;

void HomieDevice::Loop()
{
	if (!initialized)
		return;

	bool bEvenSecond = false;

	if ((int)(millis() - lastLoopSecondCounterTimestamp) >= 1000)
	{
		lastLoopSecondCounterTimestamp += 1000;
		secondCounter_Uptime++;
		secondCounter_WiFi++;
		secondCounter_MQTT++;

		if (bRapidUpdateRSSI && (secondCounter_Uptime & 1))
		{
			int iWiFiRSSI_Current = WiFi.RSSI();
			if (iWiFiRSSI != iWiFiRSSI_Current)
			{
				iWiFiRSSI = iWiFiRSSI_Current;

				Publish(String(topic + "/$stats/signal").c_str(), 2, true, String(iWiFiRSSI).c_str());
			}
		}

		bEvenSecond = true;
	}

	bool bEvenDeciSecond = false;

	if ((int)(millis() - lastLoopDeciSecondCounterTimestamp) >= 100)
	{
		lastLoopDeciSecondCounterTimestamp += 100;
		bEvenDeciSecond = true;
	}

	if (!bEvenDeciSecond)
		return;

	if (bEvenSecond)
	{
	}

	if (WiFi.status() != WL_CONNECTED)
	{
		secondCounter_WiFi = 0;
		secondCounter_MQTT = 0;
		return;
	}

	if (mqtt.connected())
	{

		DoInitialPublishing();

		//		pubsubClient.loop();
		mqttReconnectCount = 0;

		if ((int)(millis() - homieStatsTimestamp) >= 30000)
		{
			bool bError = false;

			if (initialPublishingDone)
			{
				bError |= 0 == Publish(String(topic + "/$state").c_str(), ipub_qos, true, "ready"); //re-publish ready every time we update stats
			}

			bError |= 0 == Publish(String(topic + "/$stats/uptime").c_str(), 2, true, String(secondCounter_Uptime).c_str());
			bError |= 0 == Publish(String(topic + "/$stats/uptime-wifi").c_str(), 2, true, String(secondCounter_WiFi).c_str());
			bError |= 0 == Publish(String(topic + "/$stats/uptime-mqtt").c_str(), 2, true, String(secondCounter_MQTT).c_str());
			bError |= 0 == Publish(String(topic + "/$stats/signal").c_str(), 2, true, String(WiFi.RSSI()).c_str());

			if (bError)
			{
				homieStatsTimestamp = millis() - (30000 - GetErrorRetryFrequency()); //retry in a while
			}
			else
			{
				homieStatsTimestamp = millis();
			}

			//			csprintf("Periodic publishing: %i, %i, %i\n",pub_return[0],pub_return[1],pub_return[2]);
		}

		if (doPublishDefaults && (int)(millis() - publishDefaultsTimestamp) > 0)
		{
			doPublishDefaults = 0;

			for (size_t a = 0; a < node.size(); a++)
			{
				node[a]->PublishDefaults();
			}
		}
	}
	else
	{

		//csprintf("not connected. bConnecting=%i\n",bConnecting);

		secondCounter_MQTT = 0;

		homieStatsTimestamp = millis() - 1000000;

		if (!connecting)
		{

			//csprintf("millis()-ulLastReconnect=%i  interval=%i\n",millis()-ulLastReconnect,interval);

			if (!lastReconnect || (millis() - lastReconnect) > GetReconnectInterval())
			{

				csprintf("Connecting to MQTT server %s...\n", strMqttServerIP.c_str());
				connecting = true;
				sendError = false;
				initialPublishingDone = false;

				connectTimestamp = millis();
				mqtt.connect();
			}
		}
		else
		{
			//if we're still not connected after a minute, try again
			if (!connectTimestamp || (millis() - connectTimestamp) > 60000)
			{
				csprintf("Reconnect needed, dangling flag\n");
				mqtt.disconnect(true);
				connecting = false;
			}
		}
	}
}

void HomieDevice::onConnect(bool sessionPresent)
{
	if (sessionPresent) //squelch unused parameter warning
	{
	}
#ifdef HOMIELIB_VERBOSE
	csprintf("onConnect... %p\n", this);
#endif
	connecting = false;

	doInitialPublishing = true;
	initialPublishing = 0;
	initialPublishing_Node = 0;
	initialPublishing_Prop = 0;
	pubCount_Props = 0;

	secondCounter_MQTT = 0;
}

void HomieDevice::onDisconnect(AsyncMqttClientDisconnectReason reason)
{
	if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED)
	{
	}
	//csprintf("onDisconnect...");
	if (connecting)
	{
		lastReconnect = millis();
		mqttReconnectCount++;
		connecting = false;
		//csprintf("onDisconnect...   reason %i.. lr=%lu\n",reason,ulLastReconnect);
		csprintf("MQTT server connection failed. Retrying in %lums\n", GetReconnectInterval());
	}
	else
	{
		csprintf("MQTT server connection lost\n");
	}
}

void HomieDevice::onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
	String strTopic = topic;
	_map_incoming::const_iterator citer = incoming.find(strTopic);

	if (citer != incoming.end())
	{
		HomieProperty *pProp = citer->second;
		if (pProp)
		{

			pProp->OnMqttMessage(topic, payload, properties, len, index, total);
		}
	}

	//csprintf("RECEIVED %s %s\n",topic,payload);
}

HomieNode *HomieDevice::NewNode()
{
	HomieNode *ret = new HomieNode;
	node.push_back(ret);
	ret->parent = this;

	return ret;
}

void HomieDevice::HandleInitialPublishingError()
{
	csprintf("Initial publishing error at stage %i, retrying in %i\n", initialPublishing, GetErrorRetryFrequency());

	initialPublishing = millis() + GetErrorRetryFrequency();
}

void HomieDevice::DoInitialPublishing()
{
	if (!doInitialPublishing)
	{
		initialPublishing = 0;
		initialPublishing = 0;
		return;
	}

	if (initialPublishing != 0 && (int)(millis() - initialPublishing) < iInitialPublishingThrottle_ms)
	{
		return;
	}

	if (!initialPublishing)
	{
		csprintf("%s MQTT Initial Publishing...\n", topic.c_str());
		pubCount_Props = 0;
	}

	initialPublishing = millis();

	if (!AllowInitialPublishing(this))
		return;

#ifdef HOMIELIB_VERBOSE
	if (debug)
		csprintf("IPUB: %i        Node=%i  Prop=%i\n", initialPublishing, initialPublishing_Node, initialPublishing_Prop);
#endif

	if (initialPublishing == 0)
	{
		bool bError = false;
		bError |= 0 == Publish(String(topic + "/$state").c_str(), ipub_qos, true, "init");
		bError |= 0 == Publish(String(topic + "/$homie").c_str(), ipub_qos, true, "3.0.1");
		bError |= 0 == Publish(String(topic + "/$name").c_str(), ipub_qos, true, friendlyName.c_str());
		if (bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			initialPublishing = 1;
		}
		return;
	}

	if (initialPublishing == 1)
	{
		bool bError = false;
		bError |= 0 == Publish(String(topic + "/$localip").c_str(), ipub_qos, true, WiFi.localIP().toString().c_str());
		bError |= 0 == Publish(String(topic + "/$mac").c_str(), ipub_qos, true, WiFi.macAddress().c_str());
		bError |= 0 == Publish(String(topic + "/$extensions").c_str(), ipub_qos, true, "");

		if (bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			initialPublishing = 2;
		}
		return;
	}

	if (initialPublishing == 2)
	{
		bool bError = false;

		bError |= 0 == Publish(String(topic + "/$stats").c_str(), ipub_qos, true, "uptime,signal,uptime-wifi,uptime-mqtt");
		bError |= 0 == Publish(String(topic + "/$stats/interval").c_str(), ipub_qos, true, "60");

		String strNodes;
		for (size_t i = 0; i < node.size(); i++)
		{
			strNodes += node[i]->id;
			if (i < node.size() - 1)
				strNodes += ",";
		}

#ifdef HOMIELIB_VERBOSE
		if (debug)
			csprintf("NODES: %s\n", strNodes.c_str());
#endif

		bError |= 0 == Publish(String(topic + "/$nodes").c_str(), ipub_qos, true, strNodes.c_str());

		if (bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			initialPublishing_Node = 0;
			initialPublishing = 3;
		}
		return;
	}

	if (initialPublishing == 3)
	{
		bool bError = false;
		int i = initialPublishing_Node;
		if (i < (int)node.size())
		{
			HomieNode &node = *node[i];
#ifdef HOMIELIB_VERBOSE
			if (debug)
				csprintf("NODE %i: %s\n", i, node.friendlyName.c_str());
#endif

			bError |= 0 == Publish(String(node.topic + "/$name").c_str(), ipub_qos, true, node.friendlyName.c_str());
			bError |= 0 == Publish(String(node.topic + "/$type").c_str(), ipub_qos, true, node.type.c_str());

			String strProperties;
			for (size_t j = 0; j < node.vecProperty.size(); j++)
			{
				strProperties += node.vecProperty[j]->id;
				if (j < node.vecProperty.size() - 1)
					strProperties += ",";
			}

#ifdef HOMIELIB_VERBOSE
			if (debug)
				csprintf("NODE %i: %s has properties %s\n", i, node.friendlyName.c_str(), strProperties.c_str());
#endif

			bError |= 0 == Publish(String(node.topic + "/$properties").c_str(), ipub_qos, true, strProperties.c_str());

			if (bError)
			{
				HandleInitialPublishingError();
			}
			else
			{
				initialPublishing_Node++;
			}

			return;
		}

		if (i >= (int)node.size())
		{
			initialPublishing = 4;
			initialPublishing_Node = 0;
			initialPublishing_Prop = 0;
		}
	}

	if (initialPublishing == 4)
	{
		bool bError = false;
		int i = initialPublishing_Node;

		if (i < (int)node.size())
		{
			HomieNode &node = *node[i];
#ifdef HOMIELIB_VERBOSE
			if (debug)
				csprintf("NODE %i: %s\n", i, node.friendlyName.c_str());
#endif

			int j = initialPublishing_Prop;
			if (j < (int)node.vecProperty.size())
			{
				HomieProperty &prop = *node.vecProperty[j];

#ifdef HOMIELIB_VERBOSE
				if (debug)
					csprintf("NODE %i: %s property %s\n", i, node.friendlyName.c_str(), prop.friendlyName.c_str());
#endif

				if (prop.standardMQTT)
				{
#ifdef HOMIELIB_VERBOSE
					csprintf("SUBSCRIBING to MQTT topic %s\n", prop.topic.c_str());
#endif
					bError |= 0 == mqtt.subscribe(prop.topic.c_str(), sub_qos);
					incoming[prop.topic] = &prop;
				}
				else
				{

					bError |= 0 == Publish(String(prop.topic + "/$name").c_str(), ipub_qos, true, prop.friendlyName.c_str());
					bError |= 0 == Publish(String(prop.topic + "/$settable").c_str(), ipub_qos, true, prop.settable ? "true" : "false");
					bError |= 0 == Publish(String(prop.topic + "/$retained").c_str(), ipub_qos, true, prop.retained ? "true" : "false");
					bError |= 0 == Publish(String(prop.topic + "/$datatype").c_str(), ipub_qos, true, GetHomieDataTypeText(prop.datatype));
					if (prop.unit.length())
					{
						bError |= 0 == Publish(String(prop.topic + "/$unit").c_str(), ipub_qos, true, prop.unit.c_str());
					}
					if (prop.strFormat.length())
					{
						bError |= 0 == Publish(String(prop.topic + "/$format").c_str(), ipub_qos, true, prop.strFormat.c_str());
					}

					if (prop.settable)
					{
						incoming[prop.topic] = &prop;
						incoming[prop.setTopic] = &prop;
						if (prop.retained)
						{
#ifdef HOMIELIB_VERBOSE
							csprintf("SUBSCRIBING to %s\n", prop.topic.c_str());
#endif
							bError |= 0 == mqtt.subscribe(prop.topic.c_str(), sub_qos);
						}
#ifdef HOMIELIB_VERBOSE
						csprintf("SUBSCRIBING to %s\n", prop.setTopic.c_str());
#endif
						bError |= 0 == mqtt.subscribe(prop.setTopic.c_str(), sub_qos);
					}
					else
					{
						bError |= false == prop.Publish();
					}
				}

				if (bError)
				{
					HandleInitialPublishingError();
				}
				else
				{
					initialPublishing_Prop++;
					pubCount_Props++;
				}

				return;
			}

			if (j >= (int)node.vecProperty.size())
			{
				initialPublishing_Prop = 0;
				initialPublishing_Node++;
			}
		}

		if (initialPublishing_Node >= (int)node.size())
		{
			initialPublishing_Node = 0;
			initialPublishing_Prop = 0;
			initialPublishing = 5;
		}
	}

	if (initialPublishing == 5)
	{
		bool bError = false;
		bError |= 0 == Publish(String(topic + "/$state").c_str(), ipub_qos, true, "ready");

		if (bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			doInitialPublishing = false;
			csprintf("Initial publishing complete. %i nodes, %i properties\n", node.size(), pubCount_Props);
			FinishInitialPublishing(this);

			initialPublishingDone = true;

			publishDefaultsTimestamp = millis() + 5000;
			doPublishDefaults = true;
		}
	}
}

uint16_t HomieDevice::PublishDirect(const String &topic, uint8_t qos, bool retain, const String &payload)
{
	return mqtt.publish(topic.c_str(), qos, retain, payload.c_str(), payload.length());
}

bool bFailPublish = false;

uint16_t HomieDevice::Publish(const char *topic, uint8_t qos, bool retain, const char *payload, size_t length, bool dup, uint16_t message_id)
{
	if (!IsConnected())
		return 0;
	uint16_t ret = 0;

	if (!bFailPublish)
	{
		ret = mqtt.publish(topic, qos, retain, payload, length, dup, message_id);
	}

	//csprintf("Publish %s: ret %i\n",topic,ret);

	if (!ret)
	{ //failure
		if (!sendError)
		{
			sendError = true;
			sendErrorTimestamp = millis();
		}
		else
		{
			if ((int)(millis() - sendErrorTimestamp) > 60000) //a full minute with no successes
			{
				csprintf("Full minute with no publish successes, disconnect and try again\n");
				mqtt.disconnect(true);
				sendError = false;
				connecting = false;
			}
		}
	}
	else
	{ //success
		sendError = false;
	}

	return ret;
}

String HomieDeviceName(const char *in)
{
	String ret;

	enum eLast
	{
		last_hyphen,
		last_upper,
		last_lower,
		last_number,
	};

	eLast last = last_hyphen;

	while (*in)
	{
		char input = *in++;

		if (input >= '0' && input <= '9')
		{
			ret += input;
			last = last_number;
		}
		else if (input >= 'A' && input <= 'Z')
		{
			if (last == last_lower)
			{
				ret += '-';
			}

			ret += (char)(input | 0x20);

			last = last_upper;
		}
		else if (input >= 'a' && input <= 'z')
		{
			ret += input;
			last = last_lower;
		}
		else
		{
			if (last != last_hyphen)
			{
				ret += "-";
			}

			last = last_hyphen;
		}
	}

	return ret;
}

bool HomieParseRGB(const char *in, uint32_t &rgb)
{
	int r, g, b;

	if (sscanf(in, "%d,%d,%d", &r, &g, &b) == 3)
	{
		rgb = (r << 16) + (g << 8) + (b << 0);
		return true;
	}
	return false;
}

void HSVtoRGB(float fHueIn, float fSatIn, float fBriteIn, float &fRedOut, float &fGreenOut, float &fBlueOut)
{
	float fHue = (float)fmod(fHueIn / 6.0f, 60);
	float fC = fBriteIn * fSatIn;
	float fL = fBriteIn - fC;
	float fX = (float)(fC * (1.0f - fabs(fmod(fHue * 0.1f, 2.0f) - 1.0f)));

	if (fHue >= 0.0f && fHue < 10.0f)
	{
		fRedOut = fC;
		fGreenOut = fX;
		fBlueOut = 0;
	}
	else if (fHue >= 10.0f && fHue < 20.0f)
	{
		fRedOut = fX;
		fGreenOut = fC;
		fBlueOut = 0;
	}
	else if (fHue >= 20.0f && fHue < 30.0f)
	{
		fRedOut = 0;
		fGreenOut = fC;
		fBlueOut = fX;
	}
	else if (fHue >= 30.0f && fHue < 40.0f)
	{
		fRedOut = 0;
		fGreenOut = fX;
		fBlueOut = fC;
	}
	else if (fHue >= 40.0f && fHue < 50.0f)
	{
		fRedOut = fX;
		fGreenOut = 0;
		fBlueOut = fC;
	}
	else if (fHue >= 50.0f && fHue < 60.0f)
	{
		fRedOut = fC;
		fGreenOut = 0;
		fBlueOut = fX;
	}
	else
	{
		fRedOut = 0;
		fGreenOut = 0;
		fBlueOut = 0;
	}

	fRedOut += fL;
	fGreenOut += fL;
	fBlueOut += fL;
}

bool HomieParseHSV(const char *in, uint32_t &rgb)
{
	int h, s, v;

	if (sscanf(in, "%d,%d,%d", &h, &s, &v) == 3)
	{
		float fR, fG, fB;

		HSVtoRGB(h, s * 0.01f, v * 0.01f, fR, fG, fB);

		int r = fR * 256.0f;
		int g = fG * 256.0f;
		int b = fB * 256.0f;

		if (r > 255)
			r = 255;
		if (g > 255)
			g = 255;
		if (b > 255)
			b = 255;

		rgb = (r << 16) + (g << 8) + (b << 0);
		return true;
	}
	return false;
}

int HomieDevice::GetErrorRetryFrequency()
{
	int iErrorDuration = (int)(millis() - sendErrorTimestamp);
	if (iErrorDuration >= 20000)
	{
		return 10000;
	}
	if (iErrorDuration >= 5000)
	{
		return 5000;
	}

	return 1000;
}

unsigned long HomieDevice::GetUptimeSeconds_WiFi()
{
	return secondCounter_WiFi;
}

unsigned long HomieDevice::GetUptimeSeconds_MQTT()
{
	return secondCounter_MQTT;
}

unsigned long HomieDevice::GetReconnectInterval()
{
	unsigned long interval = 5000;
	if (mqttReconnectCount >= 20)
		interval = 60000;
	else if (mqttReconnectCount >= 15)
		interval = 30000;
	else if (mqttReconnectCount >= 10)
		interval = 20000;
	else if (mqttReconnectCount >= 5)
		interval = 10000;
	return interval;
}

void HomieDevice::setServer(IPAddress ip, uint8_t port, const char *username = NULL, const char *password = NULL)
{
	this->useIp = true;
	this->setServerCredentials(username, password);
	this->mqttServerIP = ip;
	this->mqttServerPort = port;
}
void HomieDevice::setServer(const char *host, uint8_t port, const char *username = NULL, const char *password = NULL)
{
	this->useIp = false;
	this->setServerCredentials(username, password);
	this->mqttServerHost = host;
	this->mqttServerPort = port;
}
void HomieDevice::setServerCredentials(const char *username, const char *password)
{
	this->mqttUsername = username;
	this->mqttPassword = password;
}