/******************************************************************************
	Project: 	MicroMacro
	Author: 	SolarStrike Software
	URL:		www.solarstrike.net
	License:	Modified BSD (see license.txt)
******************************************************************************/

#ifndef SETTINGS_H
#define SETTINGS_H

	#include "types.h"

	#include <string>
	#include <map>


	#define CONFIG_FILENAME								"config.lua"
	#define CONFVAR_MEMORY_STRING_BUFFER_SIZE			"memoryStringBufferSize"
	#define CONFVAR_LOG_DIRECTORY						"logDirectory"
	#define CONFVAR_LOG_REMOVAL_DAYS					"logRemovalDays"
	#define CONFVAR_SCRIPT_DIRECTORY					"scriptDirectory"

	#define CONFDEFAULT_MEMORY_STRING_BUFFER_SIZE		128
	#define CONFDEFAULT_LOG_REMOVAL_DAYS				7
	#define CONFDEFAULT_LOG_DIRECTORY					"logs"
	#define CONFDEFAULT_SCRIPT_DIRECTORY				"scripts"

	class CSettings;
	class CSettingValue;
	typedef CSettings Settings;
	typedef std::map<std::string, CSettingValue> t_settingsmap;

	enum SettingType {ST_FLOAT, ST_INT, ST_STRING, ST_NIL};

	class CSettingValue
	{
		protected:
			SettingType type;
			union
			{
				double fValue;
				int iValue;
			};
			std::string szValue;

		public:
			CSettingValue();
			CSettingValue(double);
			CSettingValue(int);
			CSettingValue(std::string);
			SettingType getType();
			double getFloat();
			void setFloat(double);
			int getInt();
			void setInt(int);
			std::string getString();
			void setString(std::string);
			void setNil();
	};

	class CSettings
	{
		protected:
			t_settingsmap settingsmap;

		public:
			CSettings();
			~CSettings();

			void setFloat(std::string, double);
			void setInt(std::string, int);
			void setString(std::string, std::string);

			double getFloat(std::string, double = 0.0);
			int getInt(std::string, int = 0);
			std::string getString(std::string, std::string = "");

			void clear(std::string);
			void clearAll();
	};


#endif