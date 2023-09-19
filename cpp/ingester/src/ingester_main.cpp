#include "ingester.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <fstream>
#include <boost/program_options.hpp>
#include <glog/logging.h>
#include <filesystem>
#include "ingester_http_server.h"

using namespace std;
namespace po = boost::program_options;

bool terminated = false;

void signal_handler(int) {
    LOG(INFO) << "SIGINT/SIGTERM received. Gracefully stopping..." << endl;
    terminated = true;
}

int main(int argc, char **argv) {
    google::InitGoogleLogging("river");

    string redis_hostname;
    int redis_port;
    string redis_password;
    string redis_password_file;
    string output_directory;
    string settings_filename;
    int http_server_port;

    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "produce help message")
            ("redis_hostname,h", po::value<string>(&redis_hostname)->required(), "Redis hostname [required]")
            ("redis_port,p", po::value<int>(&redis_port)->default_value(6379), "Redis port [optional]")
            ("redis_password,w", po::value<string>(&redis_password), "Redis password [optional]")
            ("redis_password_file,f", po::value<string>(&redis_password_file), "Redis password file [optional]")
            ("settings_filename,s", po::value<string>(&settings_filename)->default_value(""),
             "Filename for JSON settings file [optional]")
            ("output_directory,o", po::value<string>(&output_directory)->required(),
             "Output directory for all files [required]")
            ("http_server_port", po::value<int>(&http_server_port)->default_value(7487),
             "HTTP server to start listening on. Defaults to port 7487. Set to 0 or negative to disable. [optional]")
        ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
        cerr << desc << endl;
        return 1;
    }

    try {
        po::notify(vm);
    } catch (boost::program_options::required_option &e) {
        cerr << e.what() << endl;
        cerr << desc << endl;
        return 1;
    }

    if (!redis_password_file.empty() && redis_password.empty()) {
        ifstream infile;
        infile.open(redis_password_file);
        infile >> redis_password;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::unique_ptr<river::IngesterHttpServer> maybe_server;
    if (http_server_port > 0) {
        LOG(INFO) << "Starting HTTP server..." << endl;
        maybe_server = std::make_unique<river::IngesterHttpServer>(output_directory, http_server_port);
        maybe_server->Start();
    }

    std::vector<std::pair<std::regex, StreamIngestionSettings>> settings_by_stream;
    if (settings_filename.empty()) {
        settings_by_stream = DefaultStreamSettings();
    } else {
        if (!std::filesystem::exists(settings_filename)) {
            cerr << "Invalid settings filename provided." << endl;
            return 1;
        }
        settings_by_stream = ParseStreamSettingsJson(settings_filename);
    }

    river::RedisConnection rc(redis_hostname, redis_port, redis_password);
    {
        river::StreamIngester ingester(
                rc,
                output_directory,
                &terminated,
                settings_by_stream);
        LOG(INFO) << "Beginning ingestion forever." << endl;
        while (!terminated) {
            ingester.Ingest();
            this_thread::sleep_for(chrono::seconds(1));
        }
    }
    LOG(INFO) << "Ingestion terminated." << endl;
    if (maybe_server) {
        maybe_server->Stop();
        LOG(INFO) << "HTTP server terminated." << endl;
    }
    return 0;
}
