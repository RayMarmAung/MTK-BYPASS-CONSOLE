#if defined(_WIN32)

/*
 * Copyright (c) 2014 Craig Lilley <cralilley@gmail.com>
 * This software is made available under the terms of the MIT licence.
 * A copy of the licence can be obtained from:
 * http://opensource.org/licenses/MIT
 */

#include "serial/serial.h"
#include <tchar.h>
#include <wchar.h>
#include <stdio.h>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <cstring>
#include <algorithm>

using serial::PortInfo;
using namespace std;
using std::vector;
using std::string;

static const DWORD port_name_max_length = 256;
static const DWORD friendly_name_max_length = 256;
static const DWORD hardware_id_max_length = 256;

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring &wstr)
{
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string strTo( size_needed, 0 );
	WideCharToMultiByte                  (CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}
vector<string> split(string str, char delimeter)
{
    vector<string> internal;
    string word = "";
    for (char s : str)
    {
        if (s == delimeter)
        {
            internal.push_back(word);
            word = "";
        }
        else
            word = word + s;
    }
    internal.push_back(word);

    return internal;
}
bool findStringIC(const std::string &strHayStack, const std::string &strNeedle)
{
    auto it = std::search(strHayStack.begin(), strHayStack.end(),
                          strNeedle.begin(), strNeedle.end(),
                          [](char ch1, char ch2) {return toupper(ch1) == toupper(ch2);});
    return (it != strHayStack.end());
}

vector<PortInfo>
serial::list_ports()
{
	vector<PortInfo> devices_found;

	/*HDEVINFO device_info_set = SetupDiGetClassDevs(
		(const GUID *) &GUID_DEVCLASS_PORTS,
		NULL,
		NULL,
		DIGCF_PRESENT);

	unsigned int device_info_set_index = 0;
	SP_DEVINFO_DATA device_info_data;

	device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	while(SetupDiEnumDeviceInfo(device_info_set, device_info_set_index, &device_info_data))
	{
		device_info_set_index++;

		// Get port name

		HKEY hkey = SetupDiOpenDevRegKey(
			device_info_set,
			&device_info_data,
			DICS_FLAG_GLOBAL,
			0,
			DIREG_DEV,
			KEY_READ);

		TCHAR port_name[port_name_max_length];
		DWORD port_name_length = port_name_max_length;

		LONG return_code = RegQueryValueEx(
					hkey,
					_T("PortName"),
					NULL,
					NULL,
					(LPBYTE)port_name,
					&port_name_length);

		RegCloseKey(hkey);

		if(return_code != EXIT_SUCCESS)
			continue;

		if(port_name_length > 0 && port_name_length <= port_name_max_length)
			port_name[port_name_length-1] = '\0';
		else
			port_name[0] = '\0';

		// Ignore parallel ports

		if(_tcsstr(port_name, _T("LPT")) != NULL)
			continue;

		// Get port friendly name

		TCHAR friendly_name[friendly_name_max_length];
		DWORD friendly_name_actual_length = 0;

		BOOL got_friendly_name = SetupDiGetDeviceRegistryProperty(
					device_info_set,
					&device_info_data,
					SPDRP_FRIENDLYNAME,
					NULL,
					(PBYTE)friendly_name,
					friendly_name_max_length,
					&friendly_name_actual_length);

		if(got_friendly_name == TRUE && friendly_name_actual_length > 0)
			friendly_name[friendly_name_actual_length-1] = '\0';
		else
			friendly_name[0] = '\0';

		// Get hardware ID

		TCHAR hardware_id[hardware_id_max_length];
		DWORD hardware_id_actual_length = 0;

		BOOL got_hardware_id = SetupDiGetDeviceRegistryProperty(
					device_info_set,
					&device_info_data,
					SPDRP_HARDWAREID,
					NULL,
					(PBYTE)hardware_id,
					hardware_id_max_length,
					&hardware_id_actual_length);

		if(got_hardware_id == TRUE && hardware_id_actual_length > 0)
			hardware_id[hardware_id_actual_length-1] = '\0';
		else
			hardware_id[0] = '\0';

		#ifdef UNICODE
			std::string portName = utf8_encode(port_name);
			std::string friendlyName = utf8_encode(friendly_name);
			std::string hardwareId = utf8_encode(hardware_id);
		#else
			std::string portName = port_name;
			std::string friendlyName = friendly_name;
			std::string hardwareId = hardware_id;
		#endif

		PortInfo port_entry;
		port_entry.port = portName;
		port_entry.description = friendlyName;
		port_entry.hardware_id = hardwareId;

		devices_found.push_back(port_entry);
	}

	SetupDiDestroyDeviceInfoList(device_info_set); */

	return devices_found;
}

std::string
serial::list_port(uint16_t vid, uint16_t &pid)
{
    std::string result = "";

    SP_DEVINFO_DATA device_data = {};
    DWORD deviceIndex = 0;
    DEVPROPTYPE ulPropertyType;
    DWORD dwSize = 0;

    HDEVINFO device_list = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);

    wchar_t check[4096] = {};
    swprintf(check, sizeof(check), L"VID_%04X&PID_", vid);

    device_data.cbSize = sizeof(SP_DEVINFO_DATA);
    while (SetupDiEnumDeviceInfo(device_list, deviceIndex, &device_data))
    {
        deviceIndex++;
        wchar_t hid[1024] = {};
        if (!SetupDiGetDeviceRegistryProperty(device_list, &device_data, SPDRP_HARDWAREID, &ulPropertyType, reinterpret_cast<BYTE*>(hid), sizeof(hid), &dwSize))
            continue;

        if (wcsstr(hid, check))
        {
            std::wstring hidwstr(hid);
            string hidstr(hidwstr.begin(), hidwstr.end());
            vector<string> hidvector = split(hidstr, '&');
            for (string a : hidvector)
            {
                if (findStringIC(a, "pid_"))
                {
                    std::transform(a.begin(), a.end(), a.begin(), ::toupper);
                    string::size_type i = a.find("PID_");
                    if (i != string::npos)
                        a.erase(i, 4);
                    sscanf_s(a.c_str(), "%hu", &pid);
                    break;
                }
            }

            HKEY key = SetupDiOpenDevRegKey(device_list, &device_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);

            wchar_t name[20];
            DWORD dwSize = sizeof(name);
            DWORD dwType = 0;
            if ((RegQueryValueEx(key, "PortName", NULL, &dwType, (LPBYTE)name, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ))
            {
                std::wstring ws(name);
                string portname(ws.begin(), ws.end());
                result = portname;
            }
        }
    }

    if (device_list)
        SetupDiDestroyDeviceInfoList(device_list);

    return result;
}

#endif // #if defined(_WIN32)
