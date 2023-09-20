//
// Created by Paul Botros on 9/18/23.
//

#ifndef RIVER_INGESTER_SRC_INGESTER_HTTP_SERVER_H_
#define RIVER_INGESTER_SRC_INGESTER_HTTP_SERVER_H_

#include <httplib.h>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace river {

class IngesterHttpServer {
public:
    IngesterHttpServer(std::filesystem::path root_directory, int port);

    // Starts in background
    void Start();

    void Stop();
private:
    const std::filesystem::path root_directory_;
    const int port_;

    void RunServerForever();

    std::unique_ptr<httplib::Server> svr_;
    std::unique_ptr<std::thread> svr_running_thread_;
    void HandleStreamsGet(const httplib::Request &request, httplib::Response &response);
    void HandleStreamGet(const httplib::Request &request, httplib::Response &response, const std::string &stream_name);
    void HandleStreamGetParquet(const httplib::Request &request, httplib::Response &response, const std::string &stream_name);
    std::optional<json> GetStreamJson(const std::string &stream_name);
};

}

#endif //RIVER_INGESTER_SRC_INGESTER_HTTP_SERVER_H_
