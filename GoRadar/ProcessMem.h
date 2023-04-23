#pragma once
#include <stdint.h>
#include <Windows.h>
#include <TlHelp32.h>
class ProcessMem
{
public:

	ProcessMem() = delete;

	ProcessMem(uint64_t pid) :process_id(pid), process_handle(nullptr) {}

	~ProcessMem() {
		if (process_handle)
			CloseHandle(process_handle);
	}

	bool read(uint64_t address, uint64_t size, void* buffer)
	{
		uint64_t read_bytes = 0;
		if (!process_handle) return false;
		if (!ReadProcessMemory(process_handle, reinterpret_cast<void*>(address), buffer, size, &read_bytes)) return false;
		if (read_bytes != size) return false;
		return true;
	}

	bool write(uint64_t address, uint64_t size, void* buffer)
	{
		uint64_t write_bytes = 0;
		if (!process_handle) return false;
		if (!WriteProcessMemory(process_handle, reinterpret_cast<void*>(address), buffer, size, &write_bytes)) return false;
		if (write_bytes != size) return false;
		return true;
	}

	bool attach()
	{
		process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);
		return (process_handle != nullptr);
	}

	uintptr_t scan(uintptr_t start, uintptr_t size, const char* signature, const char* mask)
	{
		auto  compare = [](const BYTE* pData, const BYTE* pMask, const char* pszMask) -> bool
		{
			for (; *pszMask; ++pszMask, ++pData, ++pMask)
			{
				if (*pszMask == 'x' && *pData != *pMask)
				{
					return false;
				}
			}

			return (*pszMask == NULL);
		};

		const auto remote = new BYTE[size];

		if (!read(start, size, remote))
		{
			delete[] remote;
			return NULL;
		}

		for (size_t i = 0; i < size; i++)
		{
			if (compare(static_cast<const BYTE*>(remote + i), reinterpret_cast<const BYTE*>(signature), mask))
			{
				delete[] remote;
				return start + i;
			}
		}
		delete[] remote;

		return NULL;
	}

	uintptr_t get_module(const wchar_t* str_module_name) {
		MODULEENTRY32W Result;
		Result.dwSize = sizeof(Result);
		HANDLE Snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, this->process_id);
		if (Snap == INVALID_HANDLE_VALUE) return 0;
		if (Module32First(Snap, &Result))
		{
			do
			{
				if (wcscmp(str_module_name, Result.szModule) == 0)
				{
					CloseHandle(Snap);
					return  reinterpret_cast<uintptr_t>(Result.modBaseAddr);
				}
			} while (Module32Next(Snap, &Result));
		}
		CloseHandle(Snap);
		return 0;
	}

	template <typename type>
	auto read(uint64_t address) -> type
	{
		type val;
		if (read(address, sizeof(type), &val))
			return val;
		RtlZeroMemory(&val, sizeof(type));
	}

	template <typename type>
	bool write(uint64_t address, type val)
	{
		return write(address, sizeof(type), &val);
	}

private:

	uint64_t process_id;

	HANDLE process_handle;
};
