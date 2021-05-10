#ifndef DEVICEIO_H
#define DEVICEIO_H

#include <WbemIdl.h>
#include <sstream>
#include <algorithm>
#include "serial/serial.h"
#include <SetupAPI.h>

using namespace std;
using namespace serial;

bool find_string(const string &strHayStack, const string &strNeedle)
{
	auto it = search(strHayStack.begin(), strHayStack.end(), strNeedle.begin(), strNeedle.end(),
		[](char ch1, char ch2) { return toupper(ch1) == toupper(ch2); });
	return (it != strHayStack.end());
}
vector<string> splitted(string str, char d)
{
	vector<string> internal;
	string word = "";
	for (char s : str)
	{
		if (s == d)
		{
			internal.push_back(word);
			word = "";
		}
		else
		{
			word = word + s;
		}
	}
	internal.push_back(word);
	return internal;
}

class Entry 
{
public:
	explicit Entry() :
		hw_code(0),
		wdg_addr(0x10007000),
		payload_addr(0x100a00),
		uart_base(0x11002000),
		var0(-1),
		var1(0xa),
		crash_method(0),
		payload_name("") {}
	
	uint16_t hw_code;
	uint32_t wdg_addr;
	uint32_t payload_addr;
	uint32_t uart_base;
	int var0;
	int var1;
	int crash_method;
	string payload_name;
};
class SerialPort 
{
public:
	explicit SerialPort(Serial *p) : p(p) {}
	~SerialPort() {}
		
	void set_portname(string name) 
	{
		portname = name;
	}
	string listen_port(uint16_t vid, uint16_t &pid) 
	{		
		string port = "";
		while (true) 
		{
			SP_DEVINFO_DATA device_data = {};
			DWORD deviceIndex = 0;
			DEVPROPTYPE ulPropertyType;
			DWORD dwSize = 0;

			HDEVINFO device_list = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
			
			device_data.cbSize = sizeof(SP_DEVINFO_DATA);
			
			char check[1024] = { 0 };
			sprintf_s(check, "VID_%04X&PID_", vid);

			while (SetupDiEnumDeviceInfo(device_list, deviceIndex, &device_data)) 
			{
				deviceIndex++;
				char hid[1024] = { 0 };
				if (!SetupDiGetDeviceRegistryPropertyA(device_list, &device_data, SPDRP_HARDWAREID, &ulPropertyType, (PBYTE)hid, sizeof(hid), &dwSize))
					continue;

				char *ret = 0;
				ret = strstr(hid, check);
				if (ret) 
				{
					string hidstr(hid);
					vector<string> hidvec = splitted(hidstr, '&');
					for (string a : hidvec) 
					{
						if (find_string(a, "pid_")) 
						{
							transform(a.begin(), a.end(), a.begin(), ::toupper);
							string::size_type i = a.find("PID_");
							if (i != string::npos)
								a.erase(i, 4);
							sscanf_s(a.c_str(), "%hu", &pid);
							break;
						}
					}

					HKEY key = SetupDiOpenDevRegKey(device_list, &device_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
					char name[20] = { 0 };
					DWORD dwSize = sizeof(name);
					DWORD dwType = 0;
					if ((RegQueryValueExA(key, "PortName", 0, &dwType, (LPBYTE)name, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ)) 
					{
						string portname(name);
						port = portname;
						break;
					}
				}
			}

			if (device_list)
				SetupDiDestroyDeviceInfoList(device_list);

			if (!port.empty()) 
			{
				fprintf(stdout, "Found on %s\n", port.c_str());
				break;
			}

			Sleep(300);
		}
		return port;		
	}

	int open() 
	{
		if (p->isOpen())
			p->close();

		p->setPort(portname);
		p->setBaudrate(115200);
		p->setStopbits(stopbits_one);
		p->setBytesize(eightbits);
		p->setFlowcontrol(flowcontrol_none);
		p->setTimeout(Timeout::max(), 250, 0, 250, 0);
		p->open();

		if (!p->isOpen()) 
		{
			fprintf(stderr, "Unable to open port\n");
			return -1;
		}
		fprintf(stdout, "DONE\n");
		return 0;
	}

	bool write_data(void *data, int len) 
	{
		try 
		{
			int writelen = p->write((uint8_t*)data, len);
			if (writelen != len)
				throw "Failed to write all data";
		}
		catch (exception e) 
		{
			fprintf(stderr, "%s\n", e.what());
			return false;
		}
		catch (string err) 
		{
			fprintf(stderr, "%s\n", err.c_str());
			return false;
		}
		return true;
	}
	bool read_data(void *data, int len) 
	{
		int total = 0;
		try 
		{
			while (true) 
			{
				int readlen = p->read((uint8_t*)data + total, len);
				total += readlen;
				len -= readlen;
				if (readlen == 0)
					break;
				if (!p->available())
					break;
			}
		}
		catch(exception e) 
		{
			fprintf(stderr, "%s\n", e.what());
			return false;
		}
		catch(string err) 
		{
			fprintf(stderr, "%s\n", err.c_str());
			return false;
		}
		return total;
	}

	bool write8bit(uint8_t data) 
	{
		return write_data(&data, sizeof(uint8_t));
	}
	bool read8bit(uint8_t *data) 
	{
		if (read_data(data, sizeof(uint8_t)))
			return true;
		return false;
	}
	bool writeread8bit(uint8_t in, uint8_t *out) 
	{
		if (write8bit(in))
			return read8bit(out);
		return false;
	}

	bool write16bit(uint16_t data) 
	{
		return write_data(&data, sizeof(uint16_t));
	}
	bool read16bit(uint16_t *data) 
	{
		if (read_data(data, sizeof(uint16_t)))
			return true;
		return false;
	}
	bool writeread16bit(uint16_t in, uint16_t *out) 
	{
		if (write16bit(in))
			return read16bit(out);
		return false;
	}

	bool write32bit(uint32_t data)
	{
		return write_data(&data, sizeof(uint32_t));
	}
	bool read32bit(uint32_t *data)
	{
		if (read_data(data, sizeof(uint32_t)))
			return true;
		return false;
	}
	bool writeread32bit(uint32_t in, uint32_t *out)
	{
		if (write32bit(in))
			return read32bit(out);
		return false;
	}

	bool echo_data(uint32_t in, int size) 
	{
		switch (size) 
		{
			case sizeof(uint8_t): 
			{
				uint8_t c;
				if (!writeread8bit(in, &c))
					return false;
				if (c != (uint8_t)in) 
				{
					fprintf(stderr, "Unexcepted response: 0x%02X\n", c);
					return false;
				}
			} break;
			case sizeof(uint16_t): 
			{
				uint16_t c;
				if (!writeread16bit(in, &c))
					return false;
				if (c != (uint16_t)in)
				{
					fprintf(stderr, "Unexcepted response: 0x%04X\n", c);
					return false;
				}
			} break;
			case sizeof(uint32_t) :
			{
				uint32_t c;
				if (!writeread32bit(in, &c))
					return false;
				if (c != (uint32_t)in)
				{
					fprintf(stderr, "Unexcepted response: 0x%08X\n", c);
					return false;
				}
			} break;
		}
		return true;
	}

private:
	Serial *p = 0;
	string portname;
};
class Proc 
{
public:
	explicit Proc(string path) : path(path) {}
	~Proc() {}
		
	bool execute_app(string args, string &out) 
	{
		HANDLE outWr = 0;
		HANDLE outRd = 0;
		SECURITY_ATTRIBUTES saAttr = {0};
		{
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = NULL;

			if (!CreatePipe(&outRd, &outWr, &saAttr, 0)) 
			{
				fprintf(stderr, "Failed to create pipe.\n");
				return false;
			}
			if (!SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0)) 
			{
				fprintf(stderr, "Stdout SetHandleInformation.\n");
				return false;
			}
		}

		PROCESS_INFORMATION piProcInfo;
		STARTUPINFO siStartInfo;
		BOOL bSuccess = FALSE;

		ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&siStartInfo, sizeof(STARTUPINFO)); 
		{
			siStartInfo.cb = sizeof(STARTUPINFO);
			siStartInfo.hStdError = outWr;
			siStartInfo.hStdOutput = outWr;
			siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
		}

		string cmd = path + args;

		bSuccess = CreateProcess(NULL,
			(char*)cmd.c_str(),
			0,
			0,
			TRUE,
			0,
			0,
			0,
			&siStartInfo,
			&piProcInfo);

		if (!bSuccess) 
		{
			fprintf(stderr, "Failed to create process.\n");
			return false;
		}

		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);
		CloseHandle(outWr);

		char buff[4096];
		DWORD dwRead;
		int len = 0;
		for (;;) 
		{
			bSuccess = ReadFile(outRd, buff + len, 4096, &dwRead, NULL);
			if (!bSuccess || dwRead == 0)
				break;
			len += dwRead;
		}

		string tmpstr(buff, len);
		out = tmpstr;
		return true;
	}
	bool list_driver(string &driverClassName) 
	{
		string args = " list --class=Ports";
		string out;
		if (!execute_app(args, out))
			return false;

		istringstream is(out);
		string line;
		vector<string> list;
		driverClassName.clear();

		while (getline(is, line)) 
		{
			if (find_string(line, "vid_0e8d&pid_0003") && find_string(line, "&rev")) 
			{
				vector<string> vec = splitted(line, ' ');
				for (string l : vec) 
				{
					if (find_string(l, "usb\\vid_"))
						driverClassName = l;
				}
			}
		}

		if (driverClassName.empty()) 
		{
			fprintf(stderr, "Mediatek driver classname not found.\n");
			return false;
		}
		return true;
	}
	bool install_driver() 
	{
		string dClassName;
		if (!list_driver(dClassName))
			return false;

		string args = " install --device=\"" + dClassName + "\"";
		string out;
		if (!execute_app(args, out))
			return false;

		bool success = false;

		istringstream is(out);
		string line;
		while (getline(is, line)) 
		{
			if (find_string(line, "inserting"))
				success = true;
			if (find_string(line, "creating"))
				success = true;
			if (find_string(line, "restarting"))
				success = true;
			if (find_string(line, "starting"))
				success = true;
		}
		if (!success) 
		{
			fprintf(stderr, "Failed to install libusb driver.\n");
			return false;
		}

		fprintf(stdout, "DONE\n");
		return true;
	}
	bool uninstall_driver() 
	{
		string dClassName;
		if (!list_driver(dClassName))
			return false;

		string args = " uninstall --device=\"" + dClassName + "\"";
		string out;
		if (!execute_app(args, out))
			return false;

		bool success = false;

		istringstream is(out);
		string line;
		while (getline(is, line))
		{
			if (find_string(line, "removing"))
				success = true;
			if (find_string(line, "deleting"))
				success = true;
			if (find_string(line, "stopping"))
				success = true;
		}
		if (!success)
		{
			fprintf(stderr, "Failed to uninstall libusb driver.\n");
			return false;
		}

		fprintf(stdout, "DONE\n");
		return true;
	}
	bool driverSignOff() 
	{
		return true;
	}
private:
	string path;
};

#endif // !DEVICEIO_H
