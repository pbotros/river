#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#include <redismodule.h>
#include <rmutil/util.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstring>

#define ASSERT_NOERROR_REDIS_FXN(r) \
  if ((r) != REDISMODULE_OK) { \
    return REDISMODULE_ERR; \
  }

class QueuedBatchXaddData {
public:
    QueuedBatchXaddData(RedisModuleString *key,
                        RedisModuleString *metadata_key,
                        long long int index_start,
                        long long int num_samples,
                        long long int sample_size_bytes,
                        RedisModuleString *data,
                        RedisModuleBlockedClient *bc)
        : key_(key),
          metadata_key_(metadata_key),
          index_start_(index_start),
          num_samples_(num_samples),
          data_(data),
          bc_(bc), sample_size_bytes_(sample_size_bytes) {}

    RedisModuleString *Key() const {
        return key_;
    }
    RedisModuleString *MetadataKey() const {
        return metadata_key_;
    }
    long long int IndexStart() const {
        return index_start_;
    }
    long long int NumSamples() const {
        return num_samples_;
    }
    RedisModuleString *Data() const {
        return data_;
    }
    RedisModuleBlockedClient *Bc() const {
        return bc_;
    }
    long long int SampleSizeBytes() const {
        return sample_size_bytes_;
    }
private:
    RedisModuleString *key_;
    RedisModuleString *metadata_key_;
    long long index_start_;
    long long num_samples_;
    long long sample_size_bytes_;
    RedisModuleString *data_;
    RedisModuleBlockedClient *bc_;
};

struct customLess {
    bool operator()(QueuedBatchXaddData *l, QueuedBatchXaddData *r) const {
        return l->IndexStart() > r->IndexStart();
    }
};

class QueuedBatchManager {
public:
    void push(const char *stream_name, QueuedBatchXaddData *data) {
        std::string sn(stream_name);
        if (queue_by_stream_name_.find(sn) == queue_by_stream_name_.end()) {
            queue_by_stream_name_[sn] = {};
        }
        queue_by_stream_name_.at(sn).push(data);
    }

    bool clear(const char *stream_name) {
        std::string sn(stream_name);
        if (queue_by_stream_name_.find(sn) != queue_by_stream_name_.end()) {
            queue_by_stream_name_.erase(sn);
            return true;
        } else {
            return false;
        }
    }

    std::optional<long long> top_index_start(const char *stream_name) const {
        std::string sn(stream_name);
        if (queue_by_stream_name_.find(sn) == queue_by_stream_name_.end()) {
            return std::nullopt;
        }

        auto ret = queue_by_stream_name_.at(sn).top();
        return ret->IndexStart();
    }

    QueuedBatchXaddData *pop(const char *stream_name) {
        std::string sn(stream_name);
        auto ret = queue_by_stream_name_.at(sn).top();
        queue_by_stream_name_.at(sn).pop();
        return ret;
    }

private:
    std::unordered_map<
        std::string,
        std::priority_queue<QueuedBatchXaddData *, std::vector<QueuedBatchXaddData *>, customLess>>
        queue_by_stream_name_;
};

static QueuedBatchManager *queued_batch_manager;

void free_privdata(RedisModuleCtx *ctx, void *data) {
    RedisModule_Free(data);
}

int BatchXaddStopCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // batch_xadd_stop <metadata key>
    if (argc != 2) {
        return RedisModule_WrongArity(ctx);
    }

    RedisModuleString *metadata_key_str = argv[1];
    size_t metadata_value_len;
    const char *metadata_key_value = RedisModule_StringPtrLen(metadata_key_str, &metadata_value_len);
    queued_batch_manager->clear(metadata_key_value);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

int BatchXaddCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // batch_xadd <key> <metadata key> <index start> <n samples> <sample size bytes> <data bytes>
    RedisModule_AutoMemory(ctx);

    RedisModuleString *key_str;
    RedisModuleString *metadata_key_str;
    RedisModuleString *data_str;
    long long index_start;
    long long num_samples;
    long long sample_size_bytes;

    if (RedisModule_IsBlockedTimeoutRequest(ctx)) {
        return RedisModule_ReplyWithNull(ctx);
    } else if (RedisModule_IsBlockedTimeoutRequest(ctx)) {
        auto data_ptr = (QueuedBatchXaddData *) RedisModule_GetBlockedClientPrivateData(ctx);
        key_str = data_ptr->Key();
        data_str = data_ptr->Data();
        index_start = data_ptr->IndexStart();
        num_samples = data_ptr->NumSamples();
        sample_size_bytes = data_ptr->SampleSizeBytes();
    } else {
        if (argc != 7) {
            return RedisModule_WrongArity(ctx);
        }

        // open the key and make sure it's indeed a STREAM
        key_str = argv[1];
        metadata_key_str = argv[2];
        ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[3], &index_start));
        ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[4], &num_samples));
        ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[5], &sample_size_bytes));
        data_str = argv[6];
    }

    RedisModuleKey *key = RedisModule_OpenKey(ctx, key_str, REDISMODULE_READ | REDISMODULE_WRITE);
    if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STREAM &&
        RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_EMPTY) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    RedisModuleKey *metadata_key = RedisModule_OpenKey(ctx, metadata_key_str, REDISMODULE_READ | REDISMODULE_WRITE);
    if (RedisModule_KeyType(metadata_key) != REDISMODULE_KEYTYPE_HASH) {
        RedisModule_CloseKey(metadata_key);
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    RedisModuleString *total_samples_written_str;
    long long total_samples_written;
    ASSERT_NOERROR_REDIS_FXN(
        RedisModule_HashGet(
            metadata_key, REDISMODULE_HASH_CFIELDS, "total_samples_written", &total_samples_written_str, NULL));
    ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(total_samples_written_str, &total_samples_written));
    RedisModule_FreeString(ctx, total_samples_written_str);

    size_t metadata_value_len;
    const char *metadata_key_value = RedisModule_StringPtrLen(metadata_key_str, &metadata_value_len);

    if (index_start != total_samples_written) {
        if (index_start < total_samples_written) {
            return RedisModule_ReplyWithError(ctx, "Received invalid index_start, behind total_samples_written.");
        }

        // We've received a write request that's in the future; queue this blocked client. Setting a timeout of 60s
        // assumes that all writers uploading out-of-order data should all be relatively "close" to one another, i.e.
        // all blocked clients should be resolved with 60s.
        RedisModuleBlockedClient *bc =
            RedisModule_BlockClient(ctx, BatchXaddCommand, BatchXaddCommand, free_privdata, 60000);
        RedisModule_RetainString(ctx, argv[1]);
        RedisModule_RetainString(ctx, argv[2]);
        auto *queued_data = (QueuedBatchXaddData *) RedisModule_Alloc(sizeof(QueuedBatchXaddData));
        *queued_data = QueuedBatchXaddData{
            argv[1],
            argv[2],
            index_start,
            num_samples,
            sample_size_bytes,
            argv[6],
            bc};

        queued_batch_manager->push(metadata_key_value, queued_data);

        RedisModule_CloseKey(key);
        RedisModule_CloseKey(metadata_key);
        return REDISMODULE_OK;
    }

    size_t value_length;
    const char *value = RedisModule_StringPtrLen(data_str, &value_length);

    auto **xadd_params = (RedisModuleString **) RedisModule_Alloc(sizeof(RedisModuleString *) * 4);
    for (long long i = 0; i < num_samples; i++) {
        const char *i_str = "i";
        xadd_params[0] = RedisModule_CreateString(ctx, i_str, strlen(i_str));
        xadd_params[1] = RedisModule_CreateStringFromLongLong(ctx, index_start + i);

        const char *val_str = "val";
        xadd_params[2] = RedisModule_CreateString(ctx, val_str, strlen(val_str));

        size_t sample_start = i * sample_size_bytes;
        xadd_params[3] = RedisModule_CreateString(ctx, &value[sample_start], sample_size_bytes);

        // fields and values;
        int stream_add_resp = RedisModule_StreamAdd(key, REDISMODULE_STREAM_ADD_AUTOID, NULL, xadd_params, 2);
        if (stream_add_resp != REDISMODULE_OK) {
            return RedisModule_ReplyWithError(ctx, "ERR Xadd failed.");
        }
        for (int j = 0; j < 4; j++) {
            RedisModule_FreeString(ctx, xadd_params[j]);
        }
     }
    RedisModule_Free(xadd_params);

    // Increment total_samples_written appropriately
    long new_total_samples_written  = total_samples_written + num_samples;
    RedisModuleString *new_total_samples_written_str = RedisModule_CreateStringFromLongLong(
        ctx, new_total_samples_written);
    int num_fields_updated = RedisModule_HashSet(
        metadata_key,
        REDISMODULE_HASH_CFIELDS | REDISMODULE_HASH_XX,
        "total_samples_written",
        new_total_samples_written_str,
        NULL);
    if (num_fields_updated != 1) {
        return RedisModule_ReplyWithError(ctx, "ERR HSET failed.");
    }
    RedisModule_FreeString(ctx, new_total_samples_written_str);

    // And then dequeue the next pending batch command
    auto queued_min_index_start = queued_batch_manager->top_index_start(metadata_key_value);
    if (queued_min_index_start.has_value() && *queued_min_index_start == new_total_samples_written) {
        auto data = queued_batch_manager->pop(metadata_key_value);
        RedisModule_UnblockClient(data->Bc(), data);
    }

    RedisModule_CloseKey(key);
    RedisModule_CloseKey(metadata_key);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

int BatchXaddCompressedCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // batch_xadd_compressed <key> <index start> <n samples> <value in bytes>
    if (argc != 5) {
        return RedisModule_WrongArity(ctx);
    }
    RedisModule_AutoMemory(ctx);

    // open the key and make sure it's indeed a STREAM
    RedisModuleKey *key =
        RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STREAM &&
        RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_EMPTY) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    long long index_start;
    ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[2], &index_start));

    long long num_samples;
    ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[3], &num_samples));

    size_t value_length;
    const char *value = RedisModule_StringPtrLen(argv[4], &value_length);

    RedisModuleString **xadd_params = (RedisModuleString **) RedisModule_Alloc(sizeof(RedisModuleString *) * 4);

    const char *i_str = "i";
    xadd_params[0] = RedisModule_CreateString(ctx, i_str, strlen(i_str));
    xadd_params[1] = RedisModule_CreateStringFromLongLong(ctx, index_start);

    const char *val_str = "val";
    xadd_params[2] = RedisModule_CreateString(ctx, val_str, strlen(val_str));
    xadd_params[3] = RedisModule_CreateString(ctx, value, value_length);

    RedisModuleStreamID reference_id;
    int stream_add_resp = RedisModule_StreamAdd(key, REDISMODULE_STREAM_ADD_AUTOID, &reference_id, xadd_params, 2);
    if (stream_add_resp != REDISMODULE_OK) {
        return RedisModule_ReplyWithError(ctx, "ERR Xadd failed.");
    }
    for (int i = 0; i < 4; i++) {
        RedisModule_FreeString(ctx, xadd_params[i]);
    }

    const char *reference = "reference";
    RedisModuleString *reference_str = RedisModule_CreateString(ctx, reference, strlen(reference));

    RedisModuleString *reference_id_str = RedisModule_CreateStringFromStreamID(ctx, &reference_id);
    size_t reference_id_str_len;
    RedisModule_StringPtrLen(reference_id_str, &reference_id_str_len);
    for (int sample_idx = 1; sample_idx < num_samples; sample_idx++) {
        xadd_params[0] = RedisModule_CreateString(ctx, i_str, strlen(i_str));
        xadd_params[1] = RedisModule_CreateStringFromLongLong(ctx, index_start + sample_idx);

        xadd_params[2] = reference_str;
        xadd_params[3] = reference_id_str;

        stream_add_resp = RedisModule_StreamAdd(key, REDISMODULE_STREAM_ADD_AUTOID, NULL, xadd_params, 2);
        if (stream_add_resp != REDISMODULE_OK) {
            return RedisModule_ReplyWithError(ctx, "ERR Xadd failed.");
        }

        for (int i = 0; i < 2; i++) {
            RedisModule_FreeString(ctx, xadd_params[i]);
        }
    }

    RedisModule_Free(xadd_params);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

int BatchXaddVariableCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // batch_xadd_variable <key> <index start> <sizes in ints> <value in bytes>
    if (argc != 5) {
        return RedisModule_WrongArity(ctx);
    }
    RedisModule_AutoMemory(ctx);

    // open the key and make sure it's indeed a STREAM
    RedisModuleKey *key =
        RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STREAM &&
        RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_EMPTY) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    long long index_start;
    ASSERT_NOERROR_REDIS_FXN(RedisModule_StringToLongLong(argv[2], &index_start));

    size_t sizes_length_raw;
    const char *sizes_raw = RedisModule_StringPtrLen(argv[3], &sizes_length_raw);
    const int *sizes = (const int *) sizes_raw;
    long long num_samples = sizes_length_raw / sizeof(int);

    size_t value_length;
    const char *value = RedisModule_StringPtrLen(argv[4], &value_length);

    RedisModuleString **xadd_params = (RedisModuleString **) RedisModule_Alloc(sizeof(RedisModuleString *) * 4);
    size_t sample_start = 0;
    for (long long i = 0; i < num_samples; i++) {
        const char *i_str = "i";
        xadd_params[0] = RedisModule_CreateString(ctx, i_str, strlen(i_str));
        xadd_params[1] = RedisModule_CreateStringFromLongLong(ctx, index_start + i);

        const char *val_str = "val";
        xadd_params[2] = RedisModule_CreateString(ctx, val_str, strlen(val_str));
        xadd_params[3] = RedisModule_CreateString(ctx, &value[sample_start], sizes[i]);

        // fields and values;
        int stream_add_resp = RedisModule_StreamAdd(key, REDISMODULE_STREAM_ADD_AUTOID, NULL, xadd_params, 2);
        if (stream_add_resp != REDISMODULE_OK) {
            return RedisModule_ReplyWithError(ctx, "ERR Xadd failed.");
        }

        for (int j = 0; j < 4; j++) {
            RedisModule_FreeString(ctx, xadd_params[j]);
        }
        sample_start += sizes[i];
    }
    RedisModule_Free(xadd_params);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

EXTERNC int RedisModule_OnLoad(RedisModuleCtx *ctx) {
    // Register the module itself
    if (RedisModule_Init(ctx, "river", 1, REDISMODULE_APIVER_1) ==
        REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    queued_batch_manager = new QueuedBatchManager();

    // register example.hgetset - using the shortened utility registration macro
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd", BatchXaddCommand);
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd_stop", BatchXaddStopCommand);
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd_variable", BatchXaddVariableCommand);
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd_compressed", BatchXaddCompressedCommand);

    return REDISMODULE_OK;
}
