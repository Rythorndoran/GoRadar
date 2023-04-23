#include <io.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>    
#include <hv/hlog.h>
#include <hv/WebSocketChannel.h>
#include <hv/WebSocketServer.h>
#include <hv/HttpServer.h>
#include <hv/HttpService.h>

namespace web_app
{
	hv::Json MapData;
	hv::HttpService router;
	hv::HttpServer server;

	void init_mapdata()
	{
		char buffer[260];
		getcwd(buffer, 260);
		printf("[+] The current directory is: %s\n", buffer);
		printf("[+] Load map data\n");
		std::string to_search = std::string(buffer) + std::string(R"(\data\*.txt)");
		std::string map_data_dir = std::string(buffer) + std::string(R"(\data\)");
		constexpr auto* comment_string = "//";

		auto read_float = [&](const char* filedata, std::string key)-> double {
			auto key_string = "\"" + key + "\"";
			auto key_str = std::strstr(filedata, key_string.c_str());
			auto format_string = key_string + " \"%f\"";
			auto f_value = 0.0f;
			sscanf(key_str, format_string.c_str(), &f_value);
			return f_value;
		};

		auto parse = [&](std::string filepath, std::string mapname) {
			std::string found_number{};
			auto fileobj = std::ifstream(filepath, std::ios::binary);
			fileobj.seekg(0, std::ios::end);
			unsigned long len = fileobj.tellg();
			auto buffer = new char[len];
			fileobj.seekg(0, std::ios::beg);
			fileobj.read(buffer, len);

			const auto pos_x = read_float(buffer, "pos_x");
			const auto pos_y = read_float(buffer, "pos_y");
			const auto scale = read_float(buffer, "scale");
			std::array <char, 100> fixed_name;
			sscanf(mapname.c_str(), "%[0-9a-zA-Z_]", fixed_name.data());
			MapData[fixed_name.data()] = hv::Json();
			MapData[fixed_name.data()]["pos_x"] = pos_x;
			MapData[fixed_name.data()]["pos_y"] = pos_y;
			MapData[fixed_name.data()]["scale"] = scale;
			fileobj.close();
			delete[] buffer;
		};
		MapData = hv::Json();
		intptr_t handle;
		struct _finddata_t fileinfo;
		handle = _findfirst(to_search.c_str(), &fileinfo);
		if (handle < 1)
		{
			printf("[!] Map Data Not Found!\n");
			return;
		}
		do {
			printf("[+] Load %s\n", fileinfo.name);
			parse(map_data_dir + fileinfo.name, fileinfo.name);
		} while (!_findnext(handle, &fileinfo));

		_findclose(handle);
	}

	void run()
	{

		router.GET
		(
			"/", [](HttpRequest* req, HttpResponse* resp) -> int
			{
				return resp->File("index.html");
			}
		);

		router.GET
		(
			"/data/images/{imgId}", [](HttpRequest* req, HttpResponse* resp) -> int
			{
				return resp->File((std::string("data/images/") + req->GetParam("imgId")).c_str());
			}
		);

		router.GET
		(
			"/data/{dataId}", [](HttpRequest* req, HttpResponse* resp) -> int
			{
				auto game_map_name = req->GetParam("dataId");	
				//printf("GET-Dataid:%s\n", game_map_name.c_str());
				//printf("Data:%s\n", MapData[game_map_name].dump().c_str());
				return resp->Json(MapData[game_map_name]);
			}
		);

		server.service = &router;
		server.setPort(12345);
		server.run(false);
	}

}

namespace socket_app
{
	hv::WebSocketService ws;
	hv::WebSocketServer server;
	std::vector<WebSocketChannelPtr> clients;

	void run()
	{
		ws.onopen = [](const WebSocketChannelPtr& channel, const HttpRequestPtr& req) {
			//printf("[+] onopen: GET %s\n", req->Path().c_str());
			printf("[i] %s connect\n",req->client_addr.ip.c_str());
			clients.push_back(channel);
		};

		ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
			//printf("[+] onmessage: %.*s\n", (int)msg.size(), msg.data());
			for (auto& client : clients)
			{
				client->send(msg);
			}
		};

		ws.onclose = [](const WebSocketChannelPtr& channel) {
			//printf("[+] onclose %s\n", channel->peeraddr().c_str());
			printf("[i] %s disconnect\n", channel->peeraddr().c_str());
			auto it = std::find(clients.begin(), clients.end(), channel);
			if (it != clients.end())
			{
				clients.erase(it);
			}
		};

		server.registerWebSocketService(&ws);
		server.setPort(6969);
		server.setThreadNum(4);
		server.run(false);
	}
}

int main()
{
	hlog_disable();
	web_app::init_mapdata();
	web_app::run();
	socket_app::run();
	printf("[+] Server start\n");
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::minutes(1));
	}
}