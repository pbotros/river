//
// Created by Paul Botros on 11/15/19.
//

#ifndef PARENT_INGESTER_TEST_UTILS_H
#define PARENT_INGESTER_TEST_UTILS_H

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/io/file.h>
#include <arrow/io/file.h>
#include <arrow/io/file.h>
#include <arrow/util/logging.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <chrono>
#include <cstdlib>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <parquet/api/reader.h>
#include <parquet/api/writer.h>
#include <parquet/api/writer.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>
#include <parquet/file_writer.h>
#include <parquet/properties.h>
#include <utility>

#include "river.h"
#include "ingester.h"

/**
 * Given a directory containing River ingested files and a stream name to parse, load all the relevant data files and
 * concatenate them into a single Arrow table. Returns null if there are no data files to load.
 */
namespace river {
namespace internal {

inline shared_ptr<arrow::Table> read_data_file(const string &directory, const string &stream_name) {
    boost::filesystem::path path = boost::filesystem::path(directory) / stream_name / "data.parquet";
    if (!boost::filesystem::exists(path)) {
        return nullptr;
    }

    std::shared_ptr<arrow::Table> table;
    auto data_filename = path.make_preferred().string();

#ifdef PARQUET_ASSIGN_OR_THROW
    PARQUET_ASSIGN_OR_THROW(
            std::shared_ptr<arrow::io::ReadableFile> infile,
            arrow::io::ReadableFile::Open(data_filename,
                                          arrow::default_memory_pool()));
#else
    std::shared_ptr<arrow::io::ReadableFile> infile;
    PARQUET_THROW_NOT_OK(arrow::io::ReadableFile::Open(
            data_filename, arrow::default_memory_pool(), &infile));
#endif

    std::unique_ptr<parquet::arrow::FileReader> reader;
    PARQUET_THROW_NOT_OK(
            parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader));
    PARQUET_THROW_NOT_OK(reader->ReadTable(&table));

    spdlog::info("Loaded {} rows in {} columns. [filename {}].",
                 table->num_rows(), table->num_columns(),
                 data_filename);
    return table;
}
}
}

#endif //PARENT_INGESTER_TEST_UTILS_H
