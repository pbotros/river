//
// Created by Paul Botros on 9/18/23.
//

#include "ingester_http_server.h"
#include <set>
#include <optional>
#include <filesystem>
#include <utility>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace river {

IngesterHttpServer::IngesterHttpServer(std::filesystem::path root_directory, int port) :
    root_directory_(std::move(root_directory)), port_(port) {}

void IngesterHttpServer::Start() {
    svr_ = std::make_unique<httplib::Server>();
    svr_running_thread_ = std::make_unique<std::thread>(&IngesterHttpServer::RunServerForever, this);
}

void IngesterHttpServer::Stop() {
    if (svr_) {
        svr_->stop();
    }
    if (svr_running_thread_) {
        if (svr_running_thread_->joinable()) {
            svr_running_thread_->join();
        }
        svr_running_thread_.reset();
    }
    svr_.reset();
}

void IngesterHttpServer::RunServerForever() {
    svr_->Get("/api/streams", [this](const httplib::Request &req, httplib::Response &res) {
        this->HandleStreamsGet(req, res);
    });
    svr_->Get(R"(/api/streams/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
        auto stream_name = req.matches[1];
        this->HandleStreamGet(req, res, stream_name);
    });
    svr_->Get(R"(/api/streams/([^/]+)/data.parquet)", [this](const httplib::Request &req, httplib::Response &res) {
        auto stream_name = req.matches[1];
        this->HandleStreamGetParquet(req, res, stream_name);
    });

    svr_->listen("0.0.0.0", port_);
}

void IngesterHttpServer::HandleStreamsGet(const httplib::Request &request, httplib::Response &response) {
    //--- filenames are unique so we can use a set
    std::set<std::filesystem::path> sorted_by_name;

    for (auto &entry : std::filesystem::directory_iterator(root_directory_)) {
        sorted_by_name.insert(entry.path());
    }

    json streams_json;
    for (const auto &stream_name : sorted_by_name) {
        auto stream_json = GetStreamJson(stream_name);
        if (stream_json) {
            streams_json.push_back(stream_json.value());
        }
    }

    json response_json;
    response_json["streams"] = streams_json;
    response.set_content(response_json.dump(), "application/json");
}

void IngesterHttpServer::HandleStreamGet(const httplib::Request &request,
                                         httplib::Response &response,
                                         const std::string &stream_name) {
    json response_json;
    auto val = GetStreamJson(stream_name);
    if (!val.has_value()) {
        response.set_content("Invalid stream name", "text/plain");
        response.status = 404;
        return;
    }

    response_json["stream"] = val.value();
    response.set_content(response_json.dump(), "application/json");
}

void IngesterHttpServer::HandleStreamGetParquet(const httplib::Request &request,
                                                httplib::Response &response,
                                                const std::string &stream_name) {
    auto data_filename = root_directory_ / stream_name / "data.parquet";
    if (!std::filesystem::exists(data_filename)) {
        response.set_content("Could not find data file", "text/plain");
        response.status = 404;
        return;
    }

    std::streampos data_f_size;
    {
        std::ifstream data_f(data_filename, std::ios::binary);
        data_f_size = data_f.tellg();
        data_f.seekg(0, std::ios::end);
        data_f_size = data_f.tellg() - data_f_size;
        data_f.seekg(0);
        data_f.close();
    }

    // Not too big to overload memory: 16 MB as an arbitrary setting.
    const int max_read_data_size = 16 * 1024 * 1024;
    auto *read_data_buffer = new std::vector<char>(max_read_data_size);
    auto *num_bytes_remaining_ptr = new std::streamsize();
    *num_bytes_remaining_ptr = data_f_size;
    response.set_chunked_content_provider(
        "application/octet-stream", // Content type
        [data_filename, read_data_buffer, num_bytes_remaining_ptr](size_t offset, httplib::DataSink &sink)mutable {
            std::ifstream data_f(data_filename, std::ios::binary);
            // If it's ever bad for whatever reason, terminate the stream
            if (!data_f) {
                return false;
            }

            data_f.seekg(offset);
            std::streamsize bytes_to_read = (std::min<std::streamsize>)(
                read_data_buffer->size(), *num_bytes_remaining_ptr);
            if (bytes_to_read == 0) {
                // Shouldn't ever get here; the "done" condition should be handled below.
                return false;
            }

            data_f.read(read_data_buffer->data(), bytes_to_read);
            std::streamsize num_bytes_read_this_time = data_f.gcount();
            (*num_bytes_remaining_ptr) -= num_bytes_read_this_time;

            sink.write(read_data_buffer->data(), num_bytes_read_this_time);
            if (*num_bytes_remaining_ptr == 0) {
                // Done!
                sink.done();
            }

            return true;
        },
        [read_data_buffer, num_bytes_remaining_ptr](bool success) {
            delete read_data_buffer;
            delete num_bytes_remaining_ptr;
        });
}

std::optional<json> IngesterHttpServer::GetStreamJson(const std::string &stream_name) {
    auto metadata_filename = root_directory_ / stream_name / "metadata.json";
    if (!std::filesystem::exists(metadata_filename)) {
        return std::nullopt;
    }

    std::ifstream metadata_f(metadata_filename);
    try {
        json metadata;
        metadata = json::parse(metadata_f);
        metadata["initialized_at_us"] = std::stoull((std::string) metadata["initialized_at_us"]);
        metadata["local_minus_server_clock_us"] = std::stoll((std::string) metadata["local_minus_server_clock_us"]);
        return metadata;
    } catch (json::exception &e) {
        spdlog::info("Failed to parse metadata for stream {}", stream_name);
        return std::nullopt;
    }
}

}
