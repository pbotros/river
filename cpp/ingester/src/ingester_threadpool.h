//
// Created by Paul Botros on 11/17/19.
//

#ifndef PARENT_INGESTER_THREADPOOL_H
#define PARENT_INGESTER_THREADPOOL_H

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <iostream>
#include <queue>
#include <utility>
#include <unordered_map>
#include <boost/variant.hpp>
#include <spdlog/fmt/fmt.h>
#include <fmt/printf.h>
#include <spdlog/spdlog.h>

#ifndef _MSC_VER
#include <boost/stacktrace.hpp>
#endif

using namespace std;

template <class KeyT, class RetT>
class IngesterThreadPool {
private:
    boost::asio::io_service io_service_;
    boost::asio::io_service::work work_;
    boost::thread_group threads_;
    size_t available_threads_;
    boost::mutex mutex_;
    queue<KeyT> queued_streams_;
    atomic_bool is_stopped;
    boost::function<RetT(KeyT)> task_;
    boost::condition_variable cv;

    unordered_map<KeyT, boost::variant<exception, RetT>> results;

public:
    IngesterThreadPool(size_t pool_size, boost::function<RetT(const KeyT&)> task) :
            work_(io_service_),
            available_threads_(pool_size),
            is_stopped(false),
            task_(std::move(task)) {
        for (size_t i = 0; i < pool_size; i++) {
            threads_.create_thread(boost::bind(&boost::asio::io_service::run, &io_service_));
        }
    }

    ~IngesterThreadPool() {
        stop();
    }

    void stop() {
        bool expected = false;
        if (!is_stopped.compare_exchange_strong(expected, true)) {
            return;
        }

        while (true) {
            boost::mutex::scoped_lock lock(mutex_);
            if (queued_streams_.empty() && available_threads_ == threads_.size()) {
                break;
            }

            cv.wait(lock);
        }

        io_service_.stop();
        try {
            threads_.join_all();
        } catch (const std::exception &e) {
            spdlog::info("Exception received while joining threads. {}", e.what());
        }
    }

    bool visit_result(const KeyT &key,
                      const boost::function<void(exception)>& exception_visitor,
                      const boost::function<void(RetT)>& ret_visitor) {
        boost::mutex::scoped_lock lock(mutex_);

        if (results.find(key) == results.end()) {
            return false;
        }

        class Visitor : public boost::static_visitor<> {
        public:
            const boost::function<void(exception)> &exception_visitor_;
            const boost::function<void(RetT)> &ret_visitor_;

            Visitor(const boost::function<void(exception)> &exception_visitor,
                    const boost::function<void(RetT)> &ret_visitor) :
                    exception_visitor_(exception_visitor),
                    ret_visitor_(ret_visitor) {}

            void operator()(RetT &r) const {
                ret_visitor_(r);
            }

            void operator()(exception &e) const {
                exception_visitor_(e);
            }
        };
        Visitor visitor(exception_visitor, ret_visitor);
        boost::apply_visitor(visitor, results[key]);
        return true;
    }

    void enqueue_stream(KeyT key) {
        boost::lock_guard<boost::mutex> lock(mutex_);

        if (is_stopped) {
            // Discard anything attempted to be enqueued after stopping
            fmt::printf("Threadpool is stopped. %s discarded.\n", key);
            return;
        }

        if (available_threads_ == 0) {
            queued_streams_.push(key);
            return;
        }

        available_threads_--;
        io_service_.post(boost::bind(&IngesterThreadPool::wrap_task, this, key));
    }

private:
    void wrap_task(KeyT stream_name) {
        // Run the user supplied task.
        boost::variant<exception, RetT> v;
        try {
            RetT ret = task_(stream_name);
            v = ret;
        } catch (const std::exception &e) {
            v = e;
#ifdef _MSC_VER
            spdlog::info("[Stream {}] Exception while executing task: {}", stream_name, e.what());
#else
            std:stringstream ss;
            ss << boost::stacktrace::stacktrace();
            spdlog::info("[Stream {}] Exception while executing task: {}\nStacktrace: {}",
                         stream_name, e.what(), ss.str());
#endif
        }

        boost::lock_guard<boost::mutex> lock(mutex_);
        results.insert({{stream_name, v}});
        cv.notify_one();

        if (queued_streams_.empty()) {
            available_threads_++;
            return;
        }

        string next_stream_name = queued_streams_.front();
        queued_streams_.pop();
        io_service_.post(boost::bind(&IngesterThreadPool::wrap_task, this, next_stream_name));
    }
};

#endif //PARENT_INGESTER_THREADPOOL_H
