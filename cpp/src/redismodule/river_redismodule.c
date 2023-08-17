
#include <redismodule.h>

#include <rmutil/util.h>
#include <rmutil/strings.h>
#include <rmutil/test_util.h>

#define ASSERT_NOERROR_STRING_FXN(r) \
  if ((r) != REDISMODULE_OK) { \
    return REDISMODULE_ERR; \
  }

int BatchXaddCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 6) {
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
    long long num_samples;
    long long sample_size_bytes;
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[2], &index_start));
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[3], &num_samples));
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[4], &sample_size_bytes));

    size_t value_length;
    const char *value = RedisModule_StringPtrLen(argv[5], &value_length);

    RedisModuleString **xadd_params = (RedisModuleString **) RedisModule_Alloc(sizeof(RedisModuleString *) * 4);
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
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[2], &index_start));

    long long num_samples;
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[3], &num_samples));

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
    ASSERT_NOERROR_STRING_FXN(RedisModule_StringToLongLong(argv[2], &index_start));

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

int RedisModule_OnLoad(RedisModuleCtx *ctx) {
    // Register the module itself
    if (RedisModule_Init(ctx, "river", 1, REDISMODULE_APIVER_1) ==
        REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    // register example.hgetset - using the shortened utility registration macro
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd", BatchXaddCommand);
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd_variable", BatchXaddVariableCommand);
    RMUtil_RegisterWriteCmd(ctx, "river.batch_xadd_compressed", BatchXaddCompressedCommand);

    return REDISMODULE_OK;
}
