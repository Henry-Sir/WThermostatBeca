#ifndef BECAMCU_H
#define	BECAMCU_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include "../../WAdapter/Wadapter/WDevice.h"
#include "WClock.h"

const static char HTTP_CONFIG_COMBOBOX_BEGIN[]         PROGMEM = R"=====(
        <div>
			%s<br>
        	<select name="%s">
)=====";
const static char HTTP_CONFIG_PAGE_ITEM[]         PROGMEM = R"=====(        		
				<option value="%s" %s>%s</option>                  
)=====";
const static char HTTP_CONFIG_COMBOBOX_END[]         PROGMEM = R"=====(					
			</select>
        </div>
)=====";
const static char HTTP_CONFIG_CHECKBOX_RELAY[]         PROGMEM = R"=====(					
		<div>
			<label>
				<input type="checkbox" name="rs" value="true" %s>Relay at GPIO 5
			</label>			
			<br>
			<small>* Hardware modification is needed at Thermostat to make this work.</small>
		</div>
)=====";


#define COUNT_DEVICE_MODELS 2
#define MODEL_BHT_002_GBLW 0
#define MODEL_BAC_002_ALW 1
#define HEARTBEAT_INTERVAL 10000
#define MINIMUM_INTERVAL 2000
#define STATE_COMPLETE 5
#define PIN_STATE_HEATING_RELAY 5
#define PIN_STATE_COOLING_RELAY 4

const unsigned char COMMAND_START[] = {0x55, 0xAA};
const char AR_COMMAND_END = '\n';
const String SCHEDULE_WORKDAY = "workday";
const String SCHEDULE_SATURDAY = "saturday";
const String SCHEDULE_SUNDAY = "sunday";
const String SCHEDULE_HOUR = "h";
const String SCHEDULE_TEMPERATURE = "t";
const char* SCHEDULES_MODE_OFF = "off";
const char* SCHEDULES_MODE_AUTO = "auto";
const char* SYSTEM_MODE_NONE = "none";
const char* SYSTEM_MODE_COOL = "cool";
const char* SYSTEM_MODE_HEAT = "heat";
const char* SYSTEM_MODE_FAN = "fan_only";
const char* STATE_OFF = "off";
const char* STATE_HEATING = "heating";
const char* STATE_COOLING = "cooling";
const char* FAN_MODE_NONE = "none";
const char* FAN_MODE_AUTO = "auto";
const char* FAN_MODE_LOW  = "low";
const char* FAN_MODE_MEDIUM  = "medium";
const char* FAN_MODE_HIGH = "high";

const byte STORED_FLAG_BECA = 0x36;

class WBecaDevice: public WDevice {
public:
    typedef std::function<bool()> THandlerFunction;
    typedef std::function<bool(const char*)> TCommandHandlerFunction;

    WBecaDevice(WNetwork* network, WClock* wClock)
    	: WDevice(network, "thermostat", "thermostat", DEVICE_TYPE_THERMOSTAT) {
    	this->logMcu = false;
    	this->receivingDataFromMcu = false;
		this->providingConfigPage = true;
    	this->wClock = wClock;
    	this->systemMode = nullptr;
    	this->actualTemperature = new WTemperatureProperty("temperature", "Actual");
    	this->actualTemperature->setReadOnly(true);
    	this->addProperty(actualTemperature);
    	this->targetTemperature = new WTargetTemperatureProperty("targetTemperature", "Target");//, 12.0, 28.0);
    	this->targetTemperature->setMultipleOf(0.5);
    	this->targetTemperature->setOnChange(std::bind(&WBecaDevice::desiredTemperatureToMcu, this, std::placeholders::_1));
    	this->targetTemperature->setDouble(23);
    	this->addProperty(targetTemperature);
    	this->deviceOn = new WOnOffProperty("deviceOn", "Power");
    	this->deviceOn->setOnChange(std::bind(&WBecaDevice::deviceOnToMcu, this, std::placeholders::_1));
    	this->addProperty(deviceOn);
    	this->schedulesMode = new WProperty("schedulesMode", "Schedules", STRING);
    	this->schedulesMode->setAtType("ThermostatModeProperty");
    	this->schedulesMode->addEnumString(SCHEDULES_MODE_OFF);
    	this->schedulesMode->addEnumString(SCHEDULES_MODE_AUTO);
    	this->schedulesMode->setOnChange(std::bind(&WBecaDevice::schedulesModeToMcu, this, std::placeholders::_1));
    	this->addProperty(schedulesMode);
    	this->ecoMode = new WOnOffProperty("ecoMode", "Eco");
    	this->ecoMode->setOnChange(std::bind(&WBecaDevice::ecoModeToMcu, this, std::placeholders::_1));
    	this->ecoMode->setVisibility(MQTT);
    	this->addProperty(ecoMode);
    	this->locked = new WOnOffProperty("locked", "Lock");
    	this->locked->setOnChange(std::bind(&WBecaDevice::lockedToMcu, this, std::placeholders::_1));
    	this->locked->setVisibility(MQTT);
    	this->addProperty(locked);
    	//Model
    	this->actualFloorTemperature = nullptr;
    	this->thermostatModel = network->getSettings()->registerByte("thermostatModel", MODEL_BHT_002_GBLW);
    	if (getThermostatModel() == MODEL_BHT_002_GBLW) {
    		this->actualFloorTemperature = new WTemperatureProperty("floorTemperature", "Floor");
    		this->actualFloorTemperature->setReadOnly(true);
    		this->actualFloorTemperature->setVisibility(ALL);
    		this->addProperty(actualFloorTemperature);
    	} else if (getThermostatModel() == MODEL_BAC_002_ALW) {
    		this->systemMode = new WProperty("systemMode", "System Mode", STRING);
        	this->systemMode->setAtType("ThermostatModeProperty");
        	this->systemMode->addEnumString(SYSTEM_MODE_NONE);
        	this->systemMode->addEnumString(SYSTEM_MODE_COOL);
        	this->systemMode->addEnumString(SYSTEM_MODE_HEAT);
        	this->systemMode->addEnumString(SYSTEM_MODE_FAN);
        	this->systemMode->setString(SYSTEM_MODE_NONE);
        	this->addProperty(systemMode);
    		this->fanMode = new WProperty("fanMode", "Fan", STRING);
        	this->fanMode->setAtType("FanModeProperty");
        	this->fanMode->addEnumString(FAN_MODE_NONE);
        	this->fanMode->addEnumString(FAN_MODE_LOW);
        	this->fanMode->addEnumString(FAN_MODE_MEDIUM);
        	this->fanMode->addEnumString(FAN_MODE_HIGH);
        	this->fanMode->setString(FAN_MODE_NONE);
        	this->addProperty(fanMode);
    	}
    	//Heating Relay and State property
    	this->state = nullptr;
    	this->supportingHeatingRelay = network->getSettings()->registerBoolean("supportingHeatingRelay", true);
    	this->supportingCoolingRelay = network->getSettings()->registerBoolean("supportingCoolingRelay", false);
    	if (isSupportingHeatingRelay()) pinMode(PIN_STATE_HEATING_RELAY, INPUT);
    	if (isSupportingCoolingRelay()) pinMode(PIN_STATE_COOLING_RELAY, INPUT);
    	if ((isSupportingHeatingRelay()) || (isSupportingCoolingRelay())) {
    		this->state = new WProperty("state", "State", STRING);
    		this->state->setAtType("HeatingCoolingProperty");
    		this->state->setReadOnly(true);
    		this->state->addEnumString(STATE_OFF);
    		this->state->addEnumString(STATE_HEATING);
    		this->state->addEnumString(STATE_COOLING);
    		this->addProperty(state);
    	}

    	//schedulesDayOffset
    	this->schedulesDayOffset = network->getSettings()->registerByte("schedulesDayOffset", 0);

    	lastHeartBeat = lastNotify = lastScheduleNotify = 0;
    	resetAll();
    	for (int i = 0; i < STATE_COMPLETE; i++) {
    		receivedStates[i] = false;
    	}
    	this->schedulesDataPoint = 0x00;
    }

    virtual void printConfigPage(WStringStream* page) {
    	network->log()->notice(F("Beca thermostat config page"));
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_BEGIN), getId());
    	//ComboBox with model selection
    	page->printAndReplace(FPSTR(HTTP_CONFIG_COMBOBOX_BEGIN), "Thermostat model:", "tm");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "0", (getThermostatModel() == 0 ? "selected" : ""), "Floor heating (BHT-002-GBLW)");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "1", (getThermostatModel() == 1 ? "selected" : ""), "Heating, Cooling, Ventilation (BAC-002-ALW)");
    	page->print(FPSTR(HTTP_CONFIG_COMBOBOX_END));
    	//Checkbox with support for relay
    	page->printAndReplace(FPSTR(HTTP_CONFIG_CHECKBOX_RELAY), (this->isSupportingHeatingRelay() ? "checked" : ""));
    	//ComboBox with weekday
    	page->printAndReplace(FPSTR(HTTP_CONFIG_COMBOBOX_BEGIN), "Workday schedules:", "ws");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "0", (getSchedulesDayOffset() == 0 ? "selected" : ""), "Workday (1-5): Mon-Fri; Weekend (6 - 7): Sat-Sun");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "1", (getSchedulesDayOffset() == 1 ? "selected" : ""), "Workday (1-5): Sun-Thu; Weekend (6 - 7): Fri-Sat");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "2", (getSchedulesDayOffset() == 2 ? "selected" : ""), "Workday (1-5): Sat-Wed; Weekend (6 - 7): Thu-Fri");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "3", (getSchedulesDayOffset() == 3 ? "selected" : ""), "Workday (1-5): Fri-Tue; Weekend (6 - 7): Wed-Thu");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "4", (getSchedulesDayOffset() == 4 ? "selected" : ""), "Workday (1-5): Thu-Mon; Weekend (6 - 7): Tue-Wed");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "5", (getSchedulesDayOffset() == 5 ? "selected" : ""), "Workday (1-5): Wed-Sun; Weekend (6 - 7): Mon-Tue");
    	page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_ITEM), "6", (getSchedulesDayOffset() == 6 ? "selected" : ""), "Workday (1-5): Tue-Sat; Weekend (6 - 7): Sun-Mon");
    	page->print(FPSTR(HTTP_CONFIG_COMBOBOX_END));

    	page->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
    }

    void saveConfigPage(ESP8266WebServer* webServer) {
        network->log()->notice(F("Save Beca config page"));
        this->thermostatModel->setByte(webServer->arg("tm").toInt());
        this->schedulesDayOffset->setByte(webServer->arg("ws").toInt());
        this->supportingHeatingRelay->setBoolean(webServer->arg("rs") == "true");
    }

    void loop(unsigned long now) {
    	if (state != nullptr) {
    		bool heating = false;
    		bool cooling = false;
    		if ((isSupportingHeatingRelay()) && (state != nullptr)) {
    			heating = digitalRead(PIN_STATE_HEATING_RELAY);
    		}
    		if ((isSupportingCoolingRelay()) && (state != nullptr)) {
    			cooling = digitalRead(PIN_STATE_COOLING_RELAY);
    		}
    		this->state->setString(heating ? STATE_HEATING : (cooling ? STATE_COOLING : STATE_OFF));
    	}
    	while (Serial.available() > 0) {
    		receiveIndex++;
    		unsigned char inChar = Serial.read();
    		receivedCommand[receiveIndex] = inChar;
    		if (receiveIndex < 2) {
    			//Check command start
    			if (COMMAND_START[receiveIndex] != receivedCommand[receiveIndex]) {
    				resetAll();
    			}
    		} else if (receiveIndex == 5) {
    			//length information now available
    			commandLength = receivedCommand[4] * 0x100 + receivedCommand[5];
    		} else if ((commandLength > -1)
    				&& (receiveIndex == (6 + commandLength))) {
    			//verify checksum
    			int expChecksum = 0;
    			for (int i = 0; i < receiveIndex; i++) {
    				expChecksum += receivedCommand[i];
    			}
    			expChecksum = expChecksum % 0x100;
    			if (expChecksum == receivedCommand[receiveIndex]) {
    				processSerialCommand();
    			}
    			resetAll();
    		}
    	}
    	//Heartbeat
    	//long now = millis();
    	if ((HEARTBEAT_INTERVAL > 0)
    			&& ((lastHeartBeat == 0)
    					|| (now - lastHeartBeat > HEARTBEAT_INTERVAL))) {
    		unsigned char heartBeatCommand[] =
    				{ 0x55, 0xAA, 0x00, 0x00, 0x00, 0x00 };
    		commandCharsToSerial(6, heartBeatCommand);
    		//commandHexStrToSerial("55 aa 00 00 00 00");
    		lastHeartBeat = now;
    	}
    	if (receivedSchedules()) {
    		//Notify schedules
    		if ((lastScheduleNotify == 0) && (now - lastScheduleNotify > MINIMUM_INTERVAL)) {
    			if (onSchedulesChange) {
    				onSchedulesChange();
    			}
    			lastScheduleNotify = now;
    		}
    	}
    }

    unsigned char* getCommand() {
    	return receivedCommand;
    }

    int getCommandLength() {
    	return commandLength;
    }

    String getCommandAsString() {
    	String result = "";
    	if (commandLength > -1) {
    		for (int i = 0; i < 6 + commandLength; i++) {
    			unsigned char ch = receivedCommand[i];
    			result = result + (ch < 16 ? "0" : "") + String(ch, HEX);// charToHexStr(ch);
    			if (i + 1 < 6 + commandLength) {
    				result = result + " ";
    			}
    		}
    	}
    	return result;
    }

    void commandHexStrToSerial(String command) {
    	command.trim();
    	command.replace(" ", "");
    	command.toLowerCase();
    	int chkSum = 0;
    	if ((command.length() > 1) && (command.length() % 2 == 0)) {
    		for (int i = 0; i < (command.length() / 2); i++) {
    			unsigned char chValue = getIndex(command.charAt(i * 2)) * 0x10
    					+ getIndex(command.charAt(i * 2 + 1));
    			chkSum += chValue;
    			Serial.print((char) chValue);
    		}
    		unsigned char chValue = chkSum % 0x100;
    		Serial.print((char) chValue);
    	}
    }

    void commandCharsToSerial(unsigned int length, unsigned char* command) {
    	int chkSum = 0;
    	if (length > 2) {
    		for (int i = 0; i < length; i++) {
    			unsigned char chValue = command[i];
    			chkSum += chValue;
    			Serial.print((char) chValue);
    		}
    		unsigned char chValue = chkSum % 0x100;
    		Serial.print((char) chValue);
    	}
    }

    void queryState() {
    	//55 AA 00 08 00 00
    	unsigned char queryStateCommand[] = { 0x55, 0xAA, 0x00, 0x08, 0x00, 0x00 };
    	commandCharsToSerial(6, queryStateCommand);
    }

    void cancelConfiguration() {
    	unsigned char cancelConfigCommand[] = { 0x55, 0xaa, 0x00, 0x03, 0x00, 0x01,
    			0x02 };
    	commandCharsToSerial(7, cancelConfigCommand);
    }

    void sendActualTimeToBeca() {
    	//Command: Set date and time
    	//                       ?? YY MM DD HH MM SS Weekday
    	//DEC:                   01 19 02 15 16 04 18 05
    	//HEX: 55 AA 00 1C 00 08 01 13 02 0F 10 04 12 05
    	//DEC:                   01 19 02 20 17 51 44 03
    	//HEX: 55 AA 00 1C 00 08 01 13 02 14 11 33 2C 03
    	unsigned long epochTime = wClock->getEpochTime();
    	epochTime = epochTime + (getSchedulesDayOffset() * 86400);
    	byte year = wClock->getYear(epochTime) % 100;
    	byte month = wClock->getMonth(epochTime);
    	byte dayOfMonth = wClock->getDay(epochTime);
    	byte hours = wClock->getHours(epochTime) ;
    	byte minutes = wClock->getMinutes(epochTime);
    	byte seconds = wClock->getSeconds(epochTime);
    	byte dayOfWeek = getDayOfWeek();
    	unsigned char cancelConfigCommand[] = { 0x55, 0xaa, 0x00, 0x1c, 0x00, 0x08,
    											0x01, year, month, dayOfMonth,
    											hours, minutes, seconds, dayOfWeek};
    	commandCharsToSerial(14, cancelConfigCommand);
    }

    /*ToDo
     * void getMqttSchedules(JsonObject json, String dayRange) {
    	int startAddr = 0;
    	if (SCHEDULE_SATURDAY.equals(dayRange)) {
    		startAddr = 18;
    	} else if (SCHEDULE_SUNDAY.equals(dayRange)) {
    		startAddr = 36;
    	}
    	JsonObject jsonDay = json.createNestedObject(dayRange);
    	char timeStr[5];
    	for (int i = 0; i < 6; i++) {
    		JsonObject sch = jsonDay.createNestedObject((new String(i))->c_str());
    		sprintf(timeStr, "%02d:%02d", schedules[startAddr + i * 3 + 1], schedules[startAddr + i * 3 + 0]);
    		sch["h"] = timeStr;
    		sch["t"] = (float) schedules[startAddr + i * 3 + 2]	/ 2.0f;
    	}
    }*/

    void setOnNotifyCommand(TCommandHandlerFunction onNotifyCommand) {
    	this->onNotifyCommand = onNotifyCommand;
    }

    void setOnConfigurationRequest(THandlerFunction onConfigurationRequest) {
    	this->onConfigurationRequest = onConfigurationRequest;
    }

    void setOnSchedulesChange(THandlerFunction onSchedulesChange) {
    	this->onSchedulesChange = onSchedulesChange;
    }

    void setDeviceOn(bool deviceOn) {
    	this->deviceOn->setBoolean(deviceOn);
    }

    void setDesiredTemperature(float desiredTemperature) {
    	if (((int) (desiredTemperature * 10) % 5) == 0) {
    		this->targetTemperature->setDouble(desiredTemperature);
    	}
    }

    /*ToDo
     * bool setSchedules(String payload) {
    	if (receivedSchedules()) {
    		DynamicJsonDocument jsonBuffer(1216);
    		DeserializationError error = deserializeJson(jsonBuffer, payload);
    		//JsonObject& json = jsonBuffer.parseObject(payload);
    		bool changed = false;
    		if (error) {
    			//Nothing to do
    		} else {
    			changed = ((changed) || (setSchedules( 0, jsonBuffer, SCHEDULE_WORKDAY)));
    			changed = ((changed) || (setSchedules(18, jsonBuffer, SCHEDULE_SATURDAY)));
    			changed = ((changed) || (setSchedules(36, jsonBuffer, SCHEDULE_SUNDAY)));
    		}
    		if (changed) {
    			//Changed schedules from MQTT server, send to mcu
    			//send the changed array to MCU
    			//per unit |MM HH TT|
    			//55 AA 00 06 00 3A 65 00 00 36|
    			//00 06 28|00 08 1E|1E 0B 1E|1E 0D 1E|00 11 2C|00 16 1E|
    			//00 06 28|00 08 28|1E 0B 28|1E 0D 28|00 11 28|00 16 1E|
    			//00 06 28|00 08 28|1E 0B 28|1E 0D 28|00 11 28|00 16 1E|
    			unsigned char scheduleCommand[64];
    			scheduleCommand[0] = 0x55;
    			scheduleCommand[1] = 0xaa;
    			scheduleCommand[2] = 0x00;
    			scheduleCommand[3] = 0x06;
    			scheduleCommand[4] = 0x00;
    			scheduleCommand[5] = 0x3a;
    			scheduleCommand[6] = schedulesDataPoint;
    			scheduleCommand[7] = 0x00;
    			scheduleCommand[8] = 0x00;
    			scheduleCommand[9] = 0x36;
    			for (int i = 0; i < 54; i++) {
    				scheduleCommand[i + 10] = schedules[i];
    			}
    			commandCharsToSerial(64, scheduleCommand);
    			//notify change
    			this->notifySchedules();
    		}
    		return changed;
    	} else {
    		return false;
    	}
    }*/

    String getFanMode() {
        return (fanMode != nullptr ? fanMode->c_str() : FAN_MODE_NONE);
    }

    byte getFanModeAsByte() {
    	if (fanMode != nullptr) {
    	   	if (fanMode->equalsString(FAN_MODE_AUTO)) {
    	   		return 0x00;
    	   	} else if (fanMode->equalsString(FAN_MODE_HIGH)) {
    	   		return 0x01;
    	   	} else if (fanMode->equalsString(FAN_MODE_MEDIUM)) {
    	   		return 0x02;
    	   	} else if (fanMode->equalsString(FAN_MODE_LOW)) {
    	   		return 0x03;
    	   	} else {
    	   		return 0xFF;
    	   	}
    	} else {
    		return 0xFF;
    	}
    }

    void fanModeToMcu() {
    	if ((fanMode != nullptr) && (!this->receivingDataFromMcu)) {
    		byte dt = this->getFanModeAsByte();
    		if (dt != 0xFF) {
    			//send to device
    		    //auto:   55 aa 00 06 00 05 67 04 00 01 00
    			//low:    55 aa 00 06 00 05 67 04 00 01 03
    			//medium: 55 aa 00 06 00 05 67 04 00 01 02
    			//high:   55 aa 00 06 00 05 67 04 00 01 01
    			unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
    			                                    0x67, 0x04, 0x00, 0x01, dt};
    			commandCharsToSerial(11, deviceOnCommand);
    		}
    	}
    }

    String getSystemMode() {
    	return (systemMode != nullptr ? systemMode->c_str() : SYSTEM_MODE_NONE);
    }

    byte getSystemModeAsByte() {
    	if (systemMode != nullptr) {
    		if (systemMode->equalsString(SYSTEM_MODE_COOL)) {
    			return 0x00;
    		} else if (systemMode->equalsString(SYSTEM_MODE_HEAT)) {
    			return 0x01;
    		} else if (systemMode->equalsString(SYSTEM_MODE_FAN)) {
    			return 0x02;
    		} else {
    			return 0xFF;
    		}
    	} else {
    		return 0xFF;
    	}
    }

    void systemModeToMcu() {
    	if ((systemMode != nullptr) && (!this->receivingDataFromMcu)) {
    		byte dt = this->getSystemModeAsByte();
    		if (dt != 0xFF) {
    			//send to device
    			//cooling:     55 AA 00 06 00 05 66 04 00 01 00
    			//heating:     55 AA 00 06 00 05 66 04 00 01 01
    			//ventilation: 55 AA 00 06 00 05 66 04 00 01 02
    			unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
    												0x66, 0x04, 0x00, 0x01, dt};
    			commandCharsToSerial(11, deviceOnCommand);
    		}
    	}
    }

    void setLogMcu(bool logMcu) {
    	if (this->logMcu != logMcu) {
    		this->logMcu = logMcu;
    		notifyState();
    	}
    }

    bool isDeviceStateComplete() {
    	if (network->isDebug()) {
    		return true;
    	}
    	for (int i = 0; i < STATE_COMPLETE; i++) {
    		if (receivedStates[i] == false) {
    			return false;
    		}
    	}
    	return true;
    }

    byte getSchedulesDayOffset() {
    	return schedulesDayOffset->getByte();
    }

protected:

    byte getThermostatModel() {
    	return this->thermostatModel->getByte();
    }

    bool isSupportingHeatingRelay() {
        return this->supportingHeatingRelay->getBoolean();
    }

    bool isSupportingCoolingRelay() {
        return this->supportingCoolingRelay->getBoolean();
    }

private:
    WClock *wClock;
    int receiveIndex;
    int commandLength;
    long lastHeartBeat;
    unsigned char receivedCommand[1024];
    bool logMcu;
    boolean receivingDataFromMcu;
    WProperty* deviceOn;
    WProperty* state;
    WProperty* targetTemperature;
    WProperty* actualTemperature;
    WProperty* actualFloorTemperature;
    WProperty* schedulesMode;
    WProperty* systemMode;
    WProperty* fanMode;
    WProperty* ecoMode;
    WProperty* locked;
    byte schedules[54];
    boolean receivedStates[STATE_COMPLETE];
    byte schedulesDataPoint;
    WProperty* thermostatModel;
    WProperty *supportingHeatingRelay;
    WProperty *supportingCoolingRelay;
    WProperty* ntpServer;
    WProperty* schedulesDayOffset;
    THandlerFunction onConfigurationRequest, onSchedulesChange;
    TCommandHandlerFunction onNotifyCommand;
    unsigned long lastNotify, lastScheduleNotify;

    int getIndex(unsigned char c) {
    	const char HEX_DIGITS[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8',
    			'9', 'a', 'b', 'c', 'd', 'e', 'f' };
    	int result = -1;
    	for (int i = 0; i < 16; i++) {
    		if (c == HEX_DIGITS[i]) {
    			result = i;
    			break;
    		}
    	}
    	return result;
    }

    byte getDayOfWeek() {
    	unsigned long epochTime = wClock->getEpochTime();
    	epochTime = epochTime + (getSchedulesDayOffset() * 86400);
    	byte dayOfWeek = wClock->getWeekDay(epochTime);
    	//make sunday a seven
    	dayOfWeek = (dayOfWeek ==0 ? 7 : dayOfWeek);
    	return dayOfWeek;
    }

    bool isWeekend() {
    	byte dayOfWeek = getDayOfWeek();
    	return ((dayOfWeek == 6) || (dayOfWeek == 7));
    }

    void notifyMcuCommand(const char* commandType) {
    	if ((logMcu) && (onNotifyCommand)) {
    		onNotifyCommand(commandType);
    	}
    }

    void processSerialCommand() {
    	if (commandLength > -1) {
    		//unknown
    		//55 aa 00 00 00 00
    		this->receivingDataFromMcu = true;

    		if (receivedCommand[3] == 0x00) {
    			switch (receivedCommand[6]) {
    			case 0x00:
    			case 0x01:
    				//ignore, heartbeat MCU
    				//55 aa 01 00 00 01 01
    				//55 aa 01 00 00 01 00
    				break;
    			//default:
    				//notifyUnknownCommand();
    			}
    		} else if (receivedCommand[3] == 0x03) {
    			//ignore, MCU response to wifi state
    			//55 aa 01 03 00 00
    		} else if (receivedCommand[3] == 0x04) {
    			//Setup initialization request
    			//received: 55 aa 01 04 00 00
    			if (onConfigurationRequest) {
    				//send answer: 55 aa 00 03 00 01 00
    				unsigned char configCommand[] = { 0x55, 0xAA, 0x00, 0x03, 0x00,
    						0x01, 0x00 };
    				commandCharsToSerial(7, configCommand);
    				onConfigurationRequest();
    			}

    		} else if (receivedCommand[3] == 0x07) {
    			bool changed = false;
    			bool schedulesChanged = false;
    			bool newB;
    			float newValue;
    			byte newByte;
    			byte commandLength = receivedCommand[5];
    			bool knownCommand = false;
    			//Status report from MCU
    			switch (receivedCommand[6]) {
    			case 0x01:
    				if (commandLength == 0x05) {
    					//device On/Off
    					//55 aa 00 06 00 05 01 01 00 01 00|01
    					newB = (receivedCommand[10] == 0x01);
    					changed = ((changed) || (newB != deviceOn->getBoolean()));
    					deviceOn->setBoolean(newB);
    					receivedStates[0] = true;
    					notifyMcuCommand("deviceOn_x01");
    					knownCommand = true;
    				}
    				break;
    			case 0x02:
    				if (commandLength == 0x08) {
    					//desired Temperature
    					//e.g. 24.5C: 55 aa 01 07 00 08 02 02 00 04 00 00 00 31
    					newValue = (float) receivedCommand[13] / 2.0f;
    					changed = ((changed) || (!targetTemperature->equalsDouble(newValue)));
    					targetTemperature->setDouble(newValue);
    					receivedStates[1] = true;
    					notifyMcuCommand("desiredTemperature_x02");
    					knownCommand = true;
    				}
    				break;

    			case 0x03:
    				if (commandLength == 0x08) {
    					//actual Temperature
    					//e.g. 23C: 55 aa 01 07 00 08 03 02 00 04 00 00 00 2e
    					newValue = (float) receivedCommand[13] / 2.0f;
    					changed = ((changed) || (!actualTemperature->equalsDouble(newValue)));
    					actualTemperature->setDouble(newValue);
    					notifyMcuCommand("actualTemperature_x03");
    					knownCommand = true;
    				}
    				break;
    			case 0x04:
    				if (commandLength == 0x05) {
    					//manualMode?
    					newB = (receivedCommand[10] == 0x01);
    					changed = ((changed) || ((newB) && (!schedulesMode->equalsString(SCHEDULES_MODE_OFF))) || ((!newB) && (!schedulesMode->equalsString(SCHEDULES_MODE_AUTO))));
    					schedulesMode->setString(newB ? SCHEDULES_MODE_OFF : SCHEDULES_MODE_AUTO);
    					receivedStates[2] = true;
    					notifyMcuCommand("manualMode_x04");
    					knownCommand = true;
    				}
    				break;
    			case 0x05:
    				if (commandLength == 0x05) {
    					//ecoMode
    					newB = (receivedCommand[10] == 0x01);
    					changed = ((changed) || (newB != ecoMode->getBoolean()));
    					ecoMode->setBoolean(newB);
    					receivedStates[3] = true;
    					notifyMcuCommand("ecoMode_x05");
    					knownCommand = true;
    				}
    				break;
    			case 0x06:
    				if (commandLength == 0x05) {
    					//locked
    					newB = (receivedCommand[10] == 0x01);
    					changed = ((changed) || (newB != locked->getBoolean()));
    					locked->setBoolean(newB);
    					receivedStates[4] = true;
    					notifyMcuCommand("locked_x06");
    					knownCommand = true;
    				}
    				break;
    			case 0x65: //MODEL_BHT_002_GBLW
    			case 0x68: //MODEL_BAC_002_ALW
    				if (commandLength == 0x3A) {
    					//schedules 0x65 at heater model, 0x68 at fan model, example
    					//55 AA 00 06 00 3A 65 00 00 36
    					//00 07 28 00 08 1E 1E 0B 1E 1E 0D 1E 00 11 2C 00 16 1E
    					//00 06 28 00 08 28 1E 0B 28 1E 0D 28 00 11 28 00 16 1E
    					//00 06 28 00 08 28 1E 0B 28 1E 0D 28 00 11 28 00 16 1E
    					this->schedulesDataPoint = receivedCommand[6];
    					//this->thermostatModel->setByte(this->schedulesDataPoint == 0x65 ? MODEL_BHT_002_GBLW : MODEL_BAC_002_ALW);
    					for (int i = 0; i < 54; i++) {
    						newByte = receivedCommand[i + 10];
    						schedulesChanged = ((schedulesChanged) || (newByte != schedules[i]));
    						schedules[i] = newByte;
    					}
    					notifyMcuCommand(this->thermostatModel == MODEL_BHT_002_GBLW ? "schedules_x65" : "schedules_x68");
    					knownCommand = true;
    				} else if (receivedCommand[5] == 0x05) {
    					//Unknown permanently sent from MCU
    					//55 aa 01 07 00 05 68 01 00 01 01
    					knownCommand = true;
    				}
    				break;
    			case 0x66:
    				if (commandLength == 0x08) {
    					//MODEL_BHT_002_GBLW - actualFloorTemperature
    					//55 aa 01 07 00 08 66 02 00 04 00 00 00 00
    					newValue = (float) receivedCommand[13] / 2.0f;
    					if (actualFloorTemperature != nullptr) {
    						changed = ((changed) || (!actualFloorTemperature->equalsDouble(newValue)));
    						actualFloorTemperature->setDouble(newValue);
    					}
    					notifyMcuCommand("actualFloorTemperature_x66");
    					knownCommand = true;
    				} else if (commandLength == 0x05) {
    					//MODEL_BAC_002_ALW - systemMode
    					//cooling:     55 AA 00 06 00 05 66 04 00 01 00
    					//heating:     55 AA 00 06 00 05 66 04 00 01 01
    					//ventilation: 55 AA 00 06 00 05 66 04 00 01 02
    					//this->thermostatModel->setByte(MODEL_BAC_002_ALW);
    					changed = ((changed) || (receivedCommand[10] != this->getSystemModeAsByte()));
    					if (systemMode != nullptr) {
    						switch (receivedCommand[10]) {
    						case 0x00 :
    							systemMode->setString(SYSTEM_MODE_COOL);
    							break;
    						case 0x01 :
    							systemMode->setString(SYSTEM_MODE_HEAT);
    							break;
    						case 0x02 :
    							systemMode->setString(SYSTEM_MODE_FAN);
    							break;
    						}
    					}
    					notifyMcuCommand("systemMode_x66");
    					knownCommand = true;
    				}
    				break;
    			case 0x67:
    				if (commandLength == 0x05) {
    					//fanSpeed
    					//auto   - 55 aa 01 07 00 05 67 04 00 01 00
    					//low    - 55 aa 01 07 00 05 67 04 00 01 03
    					//medium - 55 aa 01 07 00 05 67 04 00 01 02
    					//high   - 55 aa 01 07 00 05 67 04 00 01 01
    					changed = ((changed) || (receivedCommand[10] != this->getFanModeAsByte()));
    					if (fanMode != nullptr) {
    						switch (receivedCommand[10]) {
    						case 0x00 :
    							fanMode->setString(FAN_MODE_AUTO);
    							break;
    						case 0x03 :
    							fanMode->setString(FAN_MODE_LOW);
    							break;
    						case 0x02 :
    							fanMode->setString(FAN_MODE_MEDIUM);
    							break;
    						case 0x01 :
    							fanMode->setString(FAN_MODE_HIGH);
    							break;
    						}
    					}
    					notifyMcuCommand("fanSpeed_x67");
    					knownCommand = true;
    				}
    				break;
    			}
    			if (!knownCommand) {
    				notifyUnknownCommand();
    			} else if (changed) {
    				notifyState();
    			} else if (schedulesChanged) {
    				notifySchedules();
    			}

    		} else if (receivedCommand[3] == 0x1C) {
    			//Request for time sync from MCU : 55 aa 01 1c 00 00
    			this->sendActualTimeToBeca();
    		} else {
    			notifyUnknownCommand();
    		}
    		this->receivingDataFromMcu = false;
    	}
    }

    void deviceOnToMcu(WProperty* property) {
    	if (!this->receivingDataFromMcu) {
       		//55 AA 00 06 00 05 01 01 00 01 01
       		byte dt = (this->deviceOn->getBoolean() ? 0x01 : 0x00);
       		unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
       		                                    0x01, 0x01, 0x00, 0x01, dt};
       		commandCharsToSerial(11, deviceOnCommand);
       		//notifyState();
     	}
    }

    void desiredTemperatureToMcu(WProperty* property) {
    	if (!this->receivingDataFromMcu) {
    		network->log()->notice(F("Set desired Temperature to %D"), this->targetTemperature->getDouble());
    	    //55 AA 00 06 00 08 02 02 00 04 00 00 00 2C
    	    byte dt = (byte) (this->targetTemperature->getDouble() * 2);
    	    unsigned char setTemperatureCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x08,
    	    		0x02, 0x02, 0x00, 0x04,
					0x00, 0x00, 0x00, dt};
    	    commandCharsToSerial(14, setTemperatureCommand);
    	}
    }

    void schedulesModeToMcu(WProperty* property) {
    	if (!this->receivingDataFromMcu) {
        	//55 AA 00 06 00 05 04 04 00 01 01
        	byte dt = (schedulesMode->equalsString(SCHEDULES_MODE_OFF) ? 0x01 : 0x00);
        	unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
        	                                    0x04, 0x04, 0x00, 0x01, dt};
        	commandCharsToSerial(11, deviceOnCommand);
        }
    }

    void ecoModeToMcu(WProperty* property) {
       	if (!this->receivingDataFromMcu) {
       		//55 AA 00 06 00 05 05 01 00 01 01
       		byte dt = (this->ecoMode->getBoolean() ? 0x01 : 0x00);
       		unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
       		                                    0x05, 0x01, 0x00, 0x01, dt};
       		commandCharsToSerial(11, deviceOnCommand);
       		//notifyState();
       	}
    }

    void lockedToMcu(WProperty* property) {
       	if (!this->receivingDataFromMcu) {
       		//55 AA 00 06 00 05 06 01 00 01 01
       		byte dt = (this->locked->getBoolean() ? 0x01 : 0x00);
       		unsigned char deviceOnCommand[] = { 0x55, 0xAA, 0x00, 0x06, 0x00, 0x05,
       		                                    0x06, 0x01, 0x00, 0x01, dt};
       		commandCharsToSerial(11, deviceOnCommand);
       		//notifyState();
       	}
    }

    boolean receivedSchedules() {
    	return (this->schedulesDataPoint != 0x00);
    }

    /*ToDo
     * bool addSchedules(int startAddr, JsonArray& array);
    void addSchedules(int startAddr, JsonObject& json);

    bool setSchedules(int startAddr, JsonDocument json, String dayRange) {
       	bool changed = false;
       	if (json.containsKey(dayRange)) {
       		JsonObject jsonDayRange = json[dayRange];
       		for (int i = 0; i < 6; i++) {
       			if (jsonDayRange.containsKey(String(i))) {
       				JsonObject jsonItem = jsonDayRange[String(i)];
       				if (jsonItem.containsKey(SCHEDULE_HOUR)) {
       					String timeStr = jsonItem[SCHEDULE_HOUR];
       					timeStr = (timeStr.length() == 4 ? "0" + timeStr : timeStr);
       					byte hh = timeStr.substring(0, 2).toInt();
       					byte mm = timeStr.substring(3, 5).toInt();
       					changed = changed || (schedules[startAddr + i * 3 + 1] != hh);
       					schedules[startAddr + i * 3 + 1] = hh;
       					changed = changed || (schedules[startAddr + i * 3 + 0] != mm);
       					schedules[startAddr + i * 3 + 0] = mm;
       				}
       				if (jsonItem.containsKey(SCHEDULE_TEMPERATURE)) {
       					byte tt = (int) ((float) jsonItem[SCHEDULE_TEMPERATURE] * 2);
       					changed = changed || (schedules[startAddr + i * 3 + 2] != tt);
       					schedules[startAddr + i * 3 + 2] = tt;
       				}
       			}
       		}
       	}
       	return changed;
    }*/

    void notifyState() {
    	lastNotify = 0;
    }

    void notifySchedules() {
    	lastScheduleNotify = 0;
    }

    void resetAll() {
       	receiveIndex = -1;
       	commandLength = -1;
    }

    void notifyUnknownCommand() {
    	if (onNotifyCommand) {
    		onNotifyCommand("unknown");
    	}
    }

};


#endif
