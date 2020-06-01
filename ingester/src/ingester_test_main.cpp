#include "gtest/gtest.h"
#include <glog/logging.h>

int main(int argc, char **argv) {
    google::InitGoogleLogging("river");
    FLAGS_alsologtostderr = true;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

