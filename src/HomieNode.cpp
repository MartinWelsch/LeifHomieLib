#include "HomieNode.h"
#include "HomieDevice.h"

void HomieLibDebugPrint(const char * szText);

#define csprintf(...) { char szTemp[256]; sprintf(szTemp,__VA_ARGS__ ); HomieLibDebugPrint(szTemp); }

const char * GetHomieDataTypeText(eHomieDataType datatype)
{
	switch(datatype)
	{
	default:
		return "INVALID";
	case homieString:
		return "string";
	case homieInt:
		return "integer";
	case homieFloat:
		return "float";
	case homieBool:
		return "boolean";
	case homieEnum:
		return "enum";
	case homieColor:
		return "color";
	}
};

const char * GetDefaultForHomieDataType(eHomieDataType datatype)
{
	switch(datatype)
	{
	default:
		return "uninitialized";
	case homieString:
		return "";
	case homieInt:
		return "0";
	case homieFloat:
		return "0.0";
	case homieBool:
		return "false";
	case homieColor:
		return "100,100,100";
	}
}

bool HomieDataTypeAllowsEmpty(eHomieDataType datatype)
{
	switch(datatype)
	{
	case homieString:
		return true;
	default:
		return false;
	}
};


HomieProperty::HomieProperty()
{

}

void HomieProperty::SetStandardMQTT(const String & strMqttTopic)
{
	if(initialized) return;

	standardMQTT=true;
	retained=false;
	settable=true;
	topic=strMqttTopic;
	setTopic="";

}

void HomieProperty::Init()
{
	if(!standardMQTT)
	{
		topic=parent->topic+"/"+id;
		setTopic=topic+"/set";
	}
	initialized=true;
}

void HomieProperty::DoCallback()
{
	for(size_t i=0;i<callback.size();i++)
	{
		callback[i](this);
	}

}

void HomieProperty::AddCallback(HomiePropertyCallback cb)
{
	callback.push_back(cb);
}

const String & HomieProperty::GetValue()
{
	return value;
}

void HomieProperty::PublishDefault()
{
	if(settable && retained && !receivedRetained && !standardMQTT)
	{
		receivedRetained=true;
		if(value.length())
		{
#ifdef HOMIELIB_VERBOSE
			csprintf("%s didn't receive initial value for base topic %s so unsubscribe and publish default.\n",friendlyName.c_str(),topic.c_str());
#endif
			parent->parent->mqtt.unsubscribe(topic.c_str());
			Publish();
		}
	}

}

bool HomieProperty::Publish()
{
	if(!initialized) return false;
	if(standardMQTT) return false;

	bool bRet=false;
	String strPublish=value;

	if(!strPublish.length() && !publishEmptyString) return true;

	if(!strPublish.length() && !HomieDataTypeAllowsEmpty(datatype))
	{
		strPublish=GetDefaultForHomieDataType(datatype);
#ifdef HOMIELIB_VERBOSE
		csprintf("Empty value for %s encountered, substituting default. ",id.c_str());
#endif
	}

	if(!parent->parent->mqtt.connected())
	{
#ifdef HOMIELIB_VERBOSE
		csprintf("%s can't publish \"%s\" because not connected\n",friendlyName.c_str(),strPublish.c_str());
#endif
	}
	else
	{
#ifdef HOMIELIB_VERBOSE
		csprintf("%s publishing \"%s\"\n",friendlyName.c_str(),strPublish.c_str());
#endif
		bRet=0!=parent->parent->mqtt.publish(topic.c_str(), 2, retained, strPublish.c_str(), strPublish.length());
	}
	return bRet;
}


void HomieProperty::SetValue(const String & strNewValue)
{
	if(SetValueConstrained(strNewValue))
	{
		Publish();
	}
}

void HomieProperty::SetBool(bool bValue)
{
	String strTemp=bValue?"true":"false";
	SetValue(strTemp);
}


bool HomieProperty::ValidateFormat_Int(int & min, int & max)
{
	int colon=strFormat.indexOf(':');

	if(colon>0)
	{
		min=atoi(strFormat.substring(0, colon).c_str());
		max=atoi(strFormat.substring(colon+1).c_str());
		return true;
	}

	return false;
}

bool HomieProperty::ValidateFormat_Double(double & min, double & max)
{
	int colon=strFormat.indexOf(':');

	if(colon>0)
	{
		min=atof(strFormat.substring(0, colon).c_str());
		max=atof(strFormat.substring(colon+1).c_str());
		return true;
	}

	return false;
}


bool HomieProperty::SetValueConstrained(const String & strNewValue)
{
	switch(datatype)
	{
	default:
		value=strNewValue;
		return true;
	case homieInt:
		{

			int newvalue=atoi(strNewValue.c_str());

			int min,max;

			if(ValidateFormat_Int(min,max))
			{
				if(newvalue<min || newvalue>max)
				{
#ifdef HOMIELIB_VERBOSE
					csprintf("%s ignoring invalid payload %s (int out of range %i:%i)\n",friendlyName.c_str(),strNewValue.c_str(),min,max);
#endif
					return false;
				}
			}

			value=String(newvalue);
			return true;
		}
		break;
	case homieFloat:
		{
			double newvalue=atof(strNewValue.c_str());

			double min,max;

			if(ValidateFormat_Double(min,max))
			{
				if(newvalue<min || newvalue>max)
				{
#ifdef HOMIELIB_VERBOSE
					csprintf("%s ignoring invalid payload %s (float out of range %.04f:%.04f)\n",friendlyName.c_str(),strNewValue.c_str(),min,max);
#endif
					return false;
				}
			}

			value=String(newvalue);
			return true;
		}
	case homieBool:
		if(strNewValue=="true") value="true"; else if(strNewValue=="false") value="false";
		else
		{
#ifdef HOMIELIB_VERBOSE
			csprintf("%s ignoring invalid payload %s (bool needs true or false)\n",friendlyName.c_str(),strNewValue.c_str());
#endif
			return false;
		}
		return true;
	case homieEnum:
		{
			int start=0;
			int comma;

			bool bValid=false;

			while((comma=strFormat.indexOf(',',start))>0)
			{
				if(strNewValue==strFormat.substring(start, comma))
				{
					bValid=true;
					break;
				}
				start=comma+1;
			}

			if(!bValid)
			{
				if(strNewValue==strFormat.substring(start)) bValid=true;
			}

			if(bValid)
			{
				value=strNewValue;
				return true;
			}
			else
			{
#ifdef HOMIELIB_VERBOSE
				csprintf("%s ignoring invalid payload %s (not one of %s)\n",friendlyName.c_str(),strNewValue.c_str(),strFormat.c_str());
#endif
				return false;
			}
		}

		break;
	case homieColor:
		value=strNewValue;
		return true;
		break;
	};

	return true;
}

void HomieProperty::OnMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties & properties, size_t len, size_t index, size_t total)
{
	if(properties.retain || total)	//squelch unused parameter warnings
	{
	}

	if(index==0)
	{

		std::string temp;
		temp.assign(payload,len);

		bool bValid=SetValueConstrained(String(temp.c_str()));
		//pProp->strValue.
		if(bValid)
		{
			DoCallback();
		}

		if(retained && !strcmp(topic,topic.c_str()) && !standardMQTT)
		{
#ifdef HOMIELIB_VERBOSE
			csprintf("%s received initial value for base topic %s. Unsubscribing.\n",friendlyName.c_str(),topic.c_str());
#endif
			parent->parent->mqtt.unsubscribe(topic);
			receivedRetained=true;
		}
		else
		{
			if(bValid)
			{
				Publish();
			}
		}

	}

}


HomieNode::HomieNode()
{

}

void HomieNode::Init()
{

	topic=parent->topic+"/"+id;
	for(size_t a=0;a<vecProperty.size();a++)
	{
		vecProperty[a]->Init();
	}
}


void HomieNode::PublishDefaults()
{
	for(size_t a=0;a<vecProperty.size();a++)
	{
		vecProperty[a]->PublishDefault();
	}
}

HomieProperty * HomieNode::NewProperty()
{
	HomieProperty * ret=new HomieProperty;
	vecProperty.push_back(ret);
	ret->parent=this;
	return ret;
}

