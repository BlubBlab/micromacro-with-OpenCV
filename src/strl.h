/******************************************************************************
	Project: 	MicroMacro
	Author: 	SolarStrike Software
	URL:		www.solarstrike.net
	License:	Modified BSD (see license.txt)
******************************************************************************/

#ifndef STRL_H
#define STRL_H

	#include <string>
	#include <cstddef>

	size_t strlcpy(char *dest, const char*src, size_t max_len);
	size_t strlcat(char *dest, const char*src, size_t max_len);
	size_t wstrlcpy(wchar_t *dest, const wchar_t*src, size_t max_len);
	int slprintf(char *dest, size_t size, const char *fmt, ...);
	bool strcontains(const char *haystack, const char **needles);

	void sztolower(char *dest, const char *src, size_t max_len);
	int wildfind(const std::string &format, const std::string &checkstring);

	void securezero(void *, size_t);

	std::string strReplaceAll(std::string instr, std::string search, std::string replace);
#endif
