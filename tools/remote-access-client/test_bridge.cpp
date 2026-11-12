#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static auto realGetProc = ::GetProcAddress;
static std::atomic<bool> stopping{false};
static void log(const std::string& message) { puts(message.c_str()); }
#include "remote-assistant.h"

int main() {
    using namespace remote_assistant;
    int originalCalls = 0;
    const BYTE prologue[] = {0x48, 0x89, 0x5c, 0x24, 0x10};
    auto code = static_cast<BYTE*>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code) return 1;
    memcpy(code, prologue, sizeof(prologue));
    code[5] = 0x48; code[6] = 0xb8;
    auto counter = &originalCalls;
    memcpy(code + 7, &counter, sizeof(counter));
    code[15] = 0xff; code[16] = 0x00; code[17] = 0xc3;
    DWORD old = 0;
    if (!VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old)) return 2;
    FlushInstructionCache(GetCurrentProcess(), code, 64);
    const void* metadata[] = {code};
    state.submitMethod = metadata;
    auto submit = reinterpret_cast<SubmitMessage>(code);
    submit(nullptr, nullptr);
    if (originalCalls != 1 || !installSubmitHook()) return 3;
    submit(nullptr, nullptr);
    if (originalCalls != 2 || state.timer) return 4;

    struct Model { long long messageId; int32_t uid; int32_t adjacent; } model{123, static_cast<int32_t>(contactUid), 77};
    std::wstring text = L" 商店 \r\n";
    struct Input { std::wstring* value; } input{&text};
    struct Widget { Input* input; Model* model; } widget{&input, &model};
    state.conversationInputOffset = offsetof(Widget, input);
    state.conversationModelOffset = offsetof(Widget, model);
    state.modelUidOffset = offsetof(Model, uid);
    state.inputValueOffset = offsetof(Input, value);
    state.api.stringLength = [](void* value) { return static_cast<int>(static_cast<std::wstring*>(value)->size()); };
    state.api.stringChars = [](void* value) { return static_cast<std::wstring*>(value)->c_str(); };
    static std::wstring sceneName = L"level_001_lake";
    static int activeMarker, nameMarker;
    state.getActiveScene = &activeMarker;
    state.getSceneName = &nameMarker;
    state.api.objectUnbox = [](void* value) { return value; };
    state.api.runtimeInvoke = [](const void* method, void*, void**, void**) -> void* {
        return method == &activeMarker || method == &nameMarker ? &sceneName : nullptr;
    };
    submit(&widget, nullptr);
    if (originalCalls != 3 || state.pending != 1 || !state.timer) return 5;
    KillTimer(nullptr, state.timer); state.timer = 0; state.pending = 0;
    text = L"菜单";
    submit(&widget, nullptr);
    if (originalCalls != 4 || state.timer) return 6;
    text = L"商店";
    model.uid = 123;
    model.messageId = contactUid;
    submit(&widget, nullptr);
    if (originalCalls != 5 || state.timer) return 7;
    const auto sawmill = selectFacility(L"level_002_kirzah_river", L"9");
    const auto tools = selectFacility(L"level_002_kirzah_river", L"10");
    if (!sawmill || !tools || sawmill == tools || std::wstring(facilities[sawmill-1].id) != L"sawmill_02" || std::wstring(facilities[tools-1].id) != L"tool_store_002") return 8;
    if (selectFacility(L"level_002_kirzah_river", L"工具") || selectFacility(L"level_005_volhov", L"钓鱼站")) return 9;
    if (selectFacility(L"level_007_grass_lake", L"小锯木厂")) return 10;
    if (!selectFacility(L"level_002_kirzah_river", L"Monkey-Cola 自动贩卖机")) return 11;
    puts("PASS: native submit preserved; remote shop scheduled; menu and other contacts ignored.");
    puts("PASS: map-local numbers select distinct facilities; ambiguous names do not open; aliases normalized.");
    return 0;
}
