#include <gtest/gtest.h>

#include <iostream>
#include <windows.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int res = RUN_ALL_TESTS();
    TerminateProcess(GetCurrentProcess(), res);
    return res;
}
