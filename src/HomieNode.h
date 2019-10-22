#pragma once
#include "Arduino.h"

#include <functional>

class HomieProperty;
class HomieNode;
class HomieDevice;

typedef std::function<void(HomieProperty *pSource)> HomiePropertyCallback;

enum eHomieDataType
{
	homieString,
	homieInt,
	homieInteger = homieInt,
	homieFloat,
	homieBool,
	homieBoolean = homieBool,
	homieEnum,
	homieColor,
	homieColour = homieColor,
};

const char *GetHomieDataTypeText(eHomieDataType datatype);
bool HomieDataTypeAllowsEmpty(eHomieDataType datatype);
const char *GetDefaultForHomieDataType(eHomieDataType datatype);

struct AsyncMqttClientMessageProperties;

class HomieProperty
{
public:
	HomieProperty();

	void SetStandardMQTT(const String &strMqttTopic); //call before init to subscribe to a standard MQTT topic. Receive only.

	String id;
	String friendlyName;
	bool settable = false;
	bool retained = true;
	bool publishEmptyString = true;
	String unit;
	eHomieDataType datatype = homieString;
	String strFormat;

	void Init();

	void AddCallback(HomiePropertyCallback cb);

	const String &GetValue();
	void SetValue(const String &newValue);
	void SetBool(bool value);

	bool Publish();

	void OnMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties &properties, size_t len, size_t index, size_t total);

private:
	String topic;
	String setTopic;
	HomieNode *parent;
	String value;
	std::vector<HomiePropertyCallback> callback;

	bool initialized = false;

	friend class HomieDevice;
	friend class HomieNode;
	void DoCallback();

	bool SetValueConstrained(const String &strNewValue);

	bool ValidateFormat_Int(int &min, int &max);
	bool ValidateFormat_Double(double &min, double &max);

	void PublishDefault();

	bool receivedRetained = false;

	bool standardMQTT = false;
};

class HomieNode
{
public:
	HomieNode();

	String id;
	String friendlyName;
	String type;

	HomieProperty *NewProperty();

private:
	void Init();
	std::vector<HomieProperty *> vecProperty;

	friend class HomieDevice;
	friend class HomieProperty;
	HomieDevice *parent;
	String topic;

	void PublishDefaults();
};
