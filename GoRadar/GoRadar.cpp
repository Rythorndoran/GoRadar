#include <iostream>
#include <thread>
#include <hv/WebSocketClient.h>
#include <Windows.h>
#include <TlHelp32.h>
#include "json.hpp"
#include "csgo.hpp"
#include "ProcessMem.h"

using json = nlohmann::json;

class web_radar
{
public:
	web_radar()
	{

	}

	~web_radar()
	{
		//ws.close();
		printf("Close");
	}

	void run(ProcessMem* proc)
	{
		ws.onopen = []() {
			printf("onopen\n");
		};

		ws.onmessage = [](const std::string& msg) {
			//printf("onmessage: %.*s\n", (int)msg.size(), msg.data());
		};

		ws.onclose = []() {
			printf("onclose\n");
		};

		// reconnect: 1,2,4,8,10,10,10...
		reconn_setting_t reconn;
		reconn_setting_init(&reconn);
		reconn.min_delay = 1000;
		reconn.max_delay = 10000;
		reconn.delay_policy = 2;
		ws.setReconnect(&reconn);
		Sleep(1000);
		ws.open("ws://127.0.0.1:6969/");


		uintptr_t m_engine = 0;
		uintptr_t m_client = 0;
		std::string map{ 0 };
		while (true)
		{
			m_engine = proc->get_module(L"engine.dll");
			m_client = proc->get_module(L"client.dll");
			printf("m_engine 0x%x\n", m_engine);
			printf("m_client 0x%x\n", m_client);

			if (m_engine && m_client)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}

		uint32_t game_status_ptr = proc->read<uint32_t>(m_engine + hazedumper::signatures::dwClientState) + hazedumper::signatures::dwClientState_State;
		auto game_status = proc->read<uint32_t>(game_status_ptr);
		while (game_status != 6)
		{
			game_status = proc->read<uint32_t>(game_status_ptr);
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}

		while (true)
		{
			std::array<char, 0x80> mapName;
			uint32_t clientState = proc->read<uint32_t>(m_engine + hazedumper::signatures::dwClientState);
			mapName = proc->read<std::array<char, 0x80>>(clientState + hazedumper::signatures::dwClientState_Map);
			map = mapName.data();
			if (mapName[0])
				break;
		}


		printf("Map %s\n", map);

		Sleep(1000);

		while (true)
		{
			json data;
			data["map_name"] = map;
			data["localplayer"] = json();
			data["enemylist"] = json::array();
			data["teamlist"] = json::array();

			uint32_t l_player = proc->read<uint32_t>(m_client + hazedumper::signatures::dwLocalPlayer);
			if (l_player <= 0)
				continue;
			int my_team = proc->read<int32_t>(l_player + hazedumper::netvars::m_iTeamNum);
			float my_pos_x = proc->read<float>(l_player + hazedumper::netvars::m_vecOrigin);
			float my_pos_y = proc->read<float>(l_player + hazedumper::netvars::m_vecOrigin + 0x04);
			int my_health = proc->read<int>(l_player + hazedumper::netvars::m_iHealth);
			float my_rot_y_ang = proc->read<float>(l_player + hazedumper::netvars::m_angEyeAnglesY);

			data["localplayer"]["x_position"] = my_pos_x;
			data["localplayer"]["y_position"] = my_pos_y;
			data["localplayer"]["health"] = my_health;
			data["localplayer"]["rot_y_ang"] = my_rot_y_ang;

			for (int i = 0; i < 32; i++)
			{

				uint32_t entity_base = proc->read<uint32_t>(m_client + hazedumper::signatures::dwEntityList + i * 0x10);

				if (entity_base == 0)
					continue;

				bool dormant = proc->read<int>(entity_base + hazedumper::signatures::m_bDormant);
				int health = proc->read<int>(entity_base + hazedumper::netvars::m_iHealth);
				int team = proc->read<int>(entity_base + hazedumper::netvars::m_iTeamNum);



				std::array<char, 0x80> entityName;
				DWORD RadarBase = proc->read<DWORD>(m_client + hazedumper::signatures::dwRadarBase);
				DWORD HudRadar = proc->read<DWORD>(RadarBase + 0x78);
				DWORD NamePoint = HudRadar + (0x174 * (i + 2)) + 0x18;
				entityName = proc->read<std::array<char, 0x80>>(NamePoint);

				if (health < 1)
					continue;

				float rot_y_ang = proc->read<float>(entity_base + hazedumper::netvars::m_angEyeAnglesY);
				float pos_x = proc->read<float>(entity_base + hazedumper::netvars::m_vecOrigin);
				float pos_y = proc->read<float>(entity_base + hazedumper::netvars::m_vecOrigin + 0x04);
				float simulation_time = proc->read<float>(entity_base + hazedumper::netvars::m_flSimulationTime);

				//std::array<char, 32> last_place;
				//last_place = proc->read<std::array<char, 32>>(entity_base + hazedumper::netvars::m_szLastPlaceName);
				json this_entity;
				this_entity["x_position"] = pos_x;
				this_entity["y_position"] = pos_y;
				this_entity["health"] = health;
				this_entity["simulation_time"] = simulation_time;
				this_entity["rot_y_ang"] = rot_y_ang;
				this_entity["game_name"] = entityName.data();

				if (team == my_team)
				{
					data["teamlist"].push_back(this_entity);
				}
				else
				{
					data["enemylist"].push_back(this_entity);
				}

			}
			ws.send(data.dump());
			data.clear();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	void stop()
	{
		ws.close();
	}
private:

	hv::WebSocketClient ws;
};

auto get_process_id(std::wstring name) -> int
{
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(PROCESSENTRY32W);
	Process32FirstW(snapshot, &entry);
	do
	{
		if (!name.compare(entry.szExeFile))
		{
			return entry.th32ProcessID;
		}

	} while (Process32NextW(snapshot, &entry));
	return 0;
}

web_radar radar;
int main()
{

	auto process_id = get_process_id(L"csgo.exe");
	while (!process_id) {
		process_id = get_process_id(L"csgo.exe");
		Sleep(1000);
	}
	ProcessMem* mem_ctx = new ProcessMem(process_id);
	mem_ctx->attach();
	radar.run(mem_ctx);
	delete mem_ctx;
}
