#include <gtest/gtest.h>
#include <windows.h>
#include <cstring>
#include "core/detour.h"

namespace {

// We use __declspec(noinline) so the compiler generates standard function prologues
__declspec(noinline) int AddNumbers(int a, int b) {
    volatile int x = a;
    volatile int y = b;
    return x + y;
}

static int s_detourCallCount = 0;
static int (*s_originalAddNumbers)(int, int) = nullptr;

int DetourAddNumbers(int a, int b) {
    s_detourCallCount++;
    if (s_originalAddNumbers) {
        return s_originalAddNumbers(a * 2, b * 3);
    }
    return 0;
}

__declspec(noinline) uint64_t MultiParamFunction(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    volatile uint64_t v1 = a;
    volatile uint64_t v2 = b;
    volatile uint64_t v3 = c;
    volatile uint64_t v4 = d;
    volatile uint64_t v5 = e;
    volatile uint64_t v6 = f;
    return v1 + v2 + v3 + v4 + v5 + v6;
}

static uint64_t (*s_originalMultiParam)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = nullptr;
static uint64_t s_capturedParams[6] = {0};

uint64_t DetourMultiParam(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    s_capturedParams[0] = a;
    s_capturedParams[1] = b;
    s_capturedParams[2] = c;
    s_capturedParams[3] = d;
    s_capturedParams[4] = e;
    s_capturedParams[5] = f;
    if (s_originalMultiParam) {
        return s_originalMultiParam(a, b, c, d, e, f) + 1000;
    }
    return 0;
}

} // anonymous namespace

class DetourTest : public ::testing::Test {
protected:
    void SetUp() override {
        s_detourCallCount = 0;
        s_originalAddNumbers = nullptr;
        s_originalMultiParam = nullptr;
        for (int i = 0; i < 6; ++i) s_capturedParams[i] = 0;
    }
};

TEST_F(DetourTest, HooksAndForwardsFunctionCall) {
    fs25vr::FunctionDetour detour;

    EXPECT_EQ(AddNumbers(2, 3), 5);
    EXPECT_EQ(s_detourCallCount, 0);

    bool installed = detour.Install(reinterpret_cast<void*>(&AddNumbers),
                                   reinterpret_cast<void*>(&DetourAddNumbers));
    ASSERT_TRUE(installed);
    EXPECT_TRUE(detour.IsInstalled());

    s_originalAddNumbers = reinterpret_cast<int(*)(int, int)>(detour.GetOriginal());
    ASSERT_NE(s_originalAddNumbers, nullptr);

    // DetourAddNumbers multiplies a by 2 and b by 3, then calls original: 2*2 + 3*3 = 4 + 9 = 13
    int result = AddNumbers(2, 3);
    EXPECT_EQ(result, 13);
    EXPECT_EQ(s_detourCallCount, 1);

    detour.Uninstall();
    EXPECT_FALSE(detour.IsInstalled());

    // After uninstall, original behavior must be restored
    int restoredResult = AddNumbers(2, 3);
    EXPECT_EQ(restoredResult, 5);
    EXPECT_EQ(s_detourCallCount, 1); // Not called again
}

TEST_F(DetourTest, PreservesRegisterAndStackState) {
    fs25vr::FunctionDetour detour;

    bool installed = detour.Install(reinterpret_cast<void*>(&MultiParamFunction),
                                   reinterpret_cast<void*>(&DetourMultiParam));
    ASSERT_TRUE(installed);

    s_originalMultiParam = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(detour.GetOriginal());
    ASSERT_NE(s_originalMultiParam, nullptr);

    uint64_t res = MultiParamFunction(10, 20, 30, 40, 50, 60);
    // Captured parameters in detour
    EXPECT_EQ(s_capturedParams[0], 10ULL);
    EXPECT_EQ(s_capturedParams[1], 20ULL);
    EXPECT_EQ(s_capturedParams[2], 30ULL);
    EXPECT_EQ(s_capturedParams[3], 40ULL);
    EXPECT_EQ(s_capturedParams[4], 50ULL);
    EXPECT_EQ(s_capturedParams[5], 60ULL);

    // MultiParamFunction returns sum (210) + 1000 = 1210
    EXPECT_EQ(res, 1210ULL);

    detour.Uninstall();
}

TEST_F(DetourTest, SafelyRejectsShortFunctionsContainingRet) {
    fs25vr::FunctionDetour detour;

    // Simulate a 3-byte stub: xor eax, eax (31 C0); ret (C3)
    // Surrounding bytes are non-padding (push rbp) so MinHook cannot fall back to a hot-patch hook.
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    ASSERT_NE(page, nullptr);
    std::memset(page, 0x55, 4096);
    uint8_t* shortStub = page + 64;
    shortStub[0] = 0x31;
    shortStub[1] = 0xC0;
    shortStub[2] = 0xC3;

    bool installed = detour.Install(shortStub, reinterpret_cast<void*>(&DetourAddNumbers));
    EXPECT_FALSE(installed) << "Detour must safely reject functions containing ret before hook size";
    EXPECT_FALSE(detour.IsInstalled());
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_F(DetourTest, TrampolineIsExecutable) {
    fs25vr::FunctionDetour detour;

    ASSERT_TRUE(detour.Install(reinterpret_cast<void*>(&AddNumbers),
                               reinterpret_cast<void*>(&DetourAddNumbers)));

    void* originalTrampoline = detour.GetOriginal();
    ASSERT_NE(originalTrampoline, nullptr);

    MEMORY_BASIC_INFORMATION mbi{};
    ASSERT_GT(VirtualQuery(originalTrampoline, &mbi, sizeof(mbi)), 0U);
    EXPECT_TRUE(mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE);

    detour.Uninstall();
}

namespace {
int (*s_originalRelJump)(int) = nullptr;
int DetourRelJump(int a) { return s_originalRelJump(a * 2); }
} // namespace

TEST_F(DetourTest, HooksFunctionWhosePrologueContainsRelativeJump) {
    // int f(int a) { return a + 5; } laid out so the prologue holds a rel32 jmp:
    //   mov eax, ecx        89 C8
    //   jmp +9              E9 09 00 00 00   (lands on the add below)
    //   int3 x9
    //   add eax, 5          83 C0 05
    //   ret                 C3
    const uint8_t code[] = {
        0x89, 0xC8,
        0xE9, 0x09, 0x00, 0x00, 0x00,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0x83, 0xC0, 0x05,
        0xC3,
    };
    void* mem = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ASSERT_NE(mem, nullptr);
    std::memcpy(mem, code, sizeof(code));
    auto fn = reinterpret_cast<int (*)(int)>(mem);
    ASSERT_EQ(fn(3), 8);

    fs25vr::FunctionDetour detour;
    ASSERT_TRUE(detour.Install(mem, reinterpret_cast<void*>(&DetourRelJump)));
    s_originalRelJump = reinterpret_cast<int (*)(int)>(detour.GetOriginal());
    ASSERT_NE(s_originalRelJump, nullptr);

    EXPECT_EQ(fn(3), 11);               // hooked: (3*2) + 5
    EXPECT_EQ(s_originalRelJump(3), 8); // trampoline relocated the jmp correctly

    detour.Uninstall();
    EXPECT_EQ(fn(3), 8);
    VirtualFree(mem, 0, MEM_RELEASE);
}

TEST_F(DetourTest, TargetCanBeHookedAgainAfterUninstall) {
    for (int round = 0; round < 3; ++round) {
        fs25vr::FunctionDetour detour;
        ASSERT_TRUE(detour.Install(reinterpret_cast<void*>(&AddNumbers),
                                   reinterpret_cast<void*>(&DetourAddNumbers)));
        s_originalAddNumbers = reinterpret_cast<int(*)(int, int)>(detour.GetOriginal());
        EXPECT_EQ(AddNumbers(2, 3), 13);
        EXPECT_TRUE(detour.Uninstall());
        EXPECT_EQ(AddNumbers(2, 3), 5);
    }
}
