// Steam-local client bridge for the server-side virtual contact. It only opens
// existing scene panels; purchases and fish sales still use the normal game
// protocol and server handlers.
namespace remote_assistant {

constexpr long long contactUid = 2147483645LL;

using DomainGet = void* (*)();
using DomainAssemblies = const void** (*)(void*, size_t*);
using ThreadAttach = void* (*)(void*);
using ThreadDetach = void (*)(void*);
using AssemblyImage = void* (*)(const void*);
using ImageName = const char* (*)(void*);
using ClassFromName = void* (*)(void*, const char*, const char*);
using ClassParent = void* (*)(void*);
using FieldFromName = void* (*)(void*, const char*);
using FieldOffset = size_t (*)(void*);
using MethodFromName = const void* (*)(void*, const char*, int);
using ClassGetType = const void* (*)(void*);
using TypeGetObject = void* (*)(const void*);
using RuntimeInvoke = void* (*)(const void*, void*, void**, void**);
using StringLength = int (*)(void*);
using StringChars = const wchar_t* (*)(void*);
using SubmitMessage = void (*)(void*, const void*);
using ObjectUnbox = void* (*)(void*);
using FormatException = void (*)(void*, char*, int);

struct Api {
    DomainGet domainGet{};
    DomainAssemblies assemblies{};
    ThreadAttach attach{};
    ThreadDetach detach{};
    AssemblyImage assemblyImage{};
    ImageName imageName{};
    ClassFromName classFromName{};
    ClassParent classParent{};
    FieldFromName fieldFromName{};
    FieldOffset fieldOffset{};
    MethodFromName methodFromName{};
    ClassGetType classGetType{};
    TypeGetObject typeGetObject{};
    RuntimeInvoke runtimeInvoke{};
    StringLength stringLength{};
    StringChars stringChars{};
    FormatException formatException{};
    ObjectUnbox objectUnbox{};
};

struct State {
    Api api{};
    void* conversationClass{};
    void* inputClass{};
    void* modelClass{};
    const void* findObjectsOfType{};
    int findObjectsParamCount{};
    const void* submitMethod{};
    SubmitMessage originalSubmit{};
    size_t conversationInputOffset{};
    size_t conversationModelOffset{};
    size_t inputValueOffset{};
    size_t modelUidOffset{};
    const void* getActiveScene{};
    const void* getSceneName{};
    UINT_PTR timer{};
    int pending{};
};

static State state;

struct Panel {
    const char* label;
    const char* className;
    const char* methodClassName;
    const char* methodName;
    void* type{};
    const void* open{};
    size_t idOffset{};
    bool idReady{};
};

enum PanelIndex {
    shopWorkshopStore,
    shop,
    fishMarket,
    cafe,
    shopWorkshopRepair,
    workshop,
    panelCount,
};

static Panel panels[panelCount] = {
    {"shop-workshop-store", "ShopWorkshop", nullptr, "mhpbpfjpnkh"},
    {"shop", "Shop", nullptr, "mhpbpfjpnkh"},
    {"fish-market", "FishMarket", nullptr, "aiikndchcbn"},
    {"cafe", "Cafe", nullptr, "pcneepaekla"},
    {"shop-workshop-repair", "ShopWorkshop", nullptr, "nlobihfhpjn"},
    {"workshop", "Workshop", nullptr, "aiikndchcbn"},
};

struct Facility {
    const wchar_t* level;
    int code;
    const wchar_t* name;
    const wchar_t* category;
    const wchar_t* id;
    PanelIndex panel;
};
#include "remote-facilities.h"

static void* findImage(void* domain, const char* wanted) {
    size_t count = 0;
    const auto list = state.api.assemblies(domain, &count);
    for (size_t i = 0; list && i < count; ++i) {
        auto image = state.api.assemblyImage(list[i]);
        const char* name = image ? state.api.imageName(image) : nullptr;
        if (name && !strcmp(name, wanted)) return image;
    }
    return nullptr;
}

static bool fieldOffsetInHierarchy(void* cls, const char* name, size_t& offset) {
    for (void* current = cls; current; current = state.api.classParent(current)) {
        if (auto field = state.api.fieldFromName(current, name)) {
            offset = state.api.fieldOffset(field);
            return true;
        }
    }
    return false;
}

static bool invoke(const void* method, void* instance, void** args, const char* label, void** output = nullptr) {
    if (!method) return false;
    void* exception = nullptr;
    void* result = state.api.runtimeInvoke(method, instance, args, &exception);
    if (exception) {
        char detail[512]{};
        if (state.api.formatException) state.api.formatException(exception, detail, sizeof(detail));
        log(std::string("remote-assistant invoke-failed ") + label + " " + detail);
        return false;
    }
    if (output) *output = result;
    return true;
}

static void* findObjects(void* typeObject) {
    if (!typeObject || !state.findObjectsOfType) return nullptr;
    void* result = nullptr;
    if (state.findObjectsParamCount == 1) {
        void* args[] = {typeObject};
        if (!invoke(state.findObjectsOfType, nullptr, args, "FindObjectsOfType", &result)) return nullptr;
        return result;
    }
    bool includeInactive = false;
    void* args[] = {typeObject, &includeInactive};
    if (!invoke(state.findObjectsOfType, nullptr, args, "FindObjectsOfType", &result)) return nullptr;
    return result;
}

static std::wstring managedText(void* value) {
    if (!value) return {};
    const int length = state.api.stringLength(value);
    const wchar_t* chars = state.api.stringChars(value);
    return length > 0 && chars ? std::wstring(chars, chars + length) : std::wstring();
}

static std::wstring inputText(void* input) {
    if (!input) return {};
    auto value = *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(input) + state.inputValueOffset);
    return managedText(value);
}

static std::wstring currentLevel() {
    void* scene = nullptr;
    if (!invoke(state.getActiveScene, nullptr, nullptr, "GetActiveScene", &scene) || !scene) return {};
    auto value = state.api.objectUnbox(scene);
    void* name = nullptr;
    if (!value || !invoke(state.getSceneName, value, nullptr, "Scene.get_name", &name)) return {};
    return managedText(name);
}

static std::wstring categoryForText(const std::wstring& text) {
    if (text == L"商店" || text == L"钓具" || text == L"钓具商店") return L"商店";
    if (text == L"食品" || text == L"食品商店") return L"食品";
    if (text == L"咖啡") return L"咖啡";
    if (text == L"修理" || text == L"工坊") return L"修理";
    if (text == L"管理" || text == L"管理处") return L"管理";
    if (text == L"银行" || text == L"ATM" || text == L"atm") return L"取款";
    if (text == L"工具" || text == L"工具店" || text == L"工具商店") return L"工具";
    if (text == L"特色" || text == L"特色店" || text == L"特色商店" ||
        text == L"农贸店" || text == L"农贸商店") return L"特色";
    return text;
}

static std::wstring normalizedName(std::wstring name) {
    for (auto& ch : name) if (ch == 0xa0) ch = L' ';
    return name;
}

// Return a table index only for an unambiguous map-local selection.
static int selectFacility(const std::wstring& level, const std::wstring& text) {
    int selected = 0, count = 0;
    for (size_t i = 0; i < std::size(facilities); ++i) {
        const auto& f = facilities[i];
        if (level == f.level && (text == std::to_wstring(f.code) || normalizedName(text) == normalizedName(f.name))) {
            selected = static_cast<int>(i + 1); ++count;
        }
    }
    if (count) return count == 1 ? selected : 0;
    const auto category = categoryForText(text);
    for (size_t i = 0; i < std::size(facilities); ++i) {
        const auto& f = facilities[i];
        if (level == f.level && category == f.category) {
            selected = static_cast<int>(i + 1); ++count;
        }
    }
    return count == 1 ? selected : 0;
}

static int captureCommand(void* widget) {
    if (!widget) return 0;
    auto input = *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(widget) + state.conversationInputOffset);
    auto model = *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(widget) + state.conversationModelOffset);
    if (!input || !model) return 0;
    const auto uid = *reinterpret_cast<int32_t*>(reinterpret_cast<BYTE*>(model) + state.modelUidOffset);
    if (uid != contactUid) return 0;
    auto text = inputText(input);
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return 0;
    text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    const auto level = currentLevel();
    std::string asciiLevel;
    for (auto ch : level) asciiLevel.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
    log("remote-assistant command-scene=" + asciiLevel);
    return selectFacility(level, text);
}

static bool openPanel(int command) {
    if (command < 1 || static_cast<size_t>(command) > std::size(facilities)) return false;
    const auto& facility = facilities[command - 1];
    if (currentLevel() != facility.level) return false;
    auto& binding = panels[facility.panel];
    if (!binding.type || !binding.open || !binding.idReady) return false;
    auto array = findObjects(binding.type);
    if (!array) return false;
    const size_t count = *reinterpret_cast<size_t*>(reinterpret_cast<BYTE*>(array) + 0x18);
    if (count > 256) return false;
    auto items = reinterpret_cast<void**>(reinterpret_cast<BYTE*>(array) + 0x20);
    for (size_t i = 0; i < count; ++i) {
        auto object = items[i];
        if (!object) continue;
        auto id = managedText(*reinterpret_cast<void**>(reinterpret_cast<BYTE*>(object) + binding.idOffset));
        if (id != facility.id) continue;
        if (!invoke(binding.open, object, nullptr, binding.label)) return false;
        std::string asciiID;
        for (auto ch : id) asciiID.push_back(ch <= 0x7f ? static_cast<char>(ch) : '?');
        log(std::string("remote-assistant opened ") + binding.label + " id=" + asciiID);
        return true;
    }
    log(std::string("remote-assistant object-missing ") + binding.label);
    return false;
}

static void CALLBACK openPending(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    if (state.timer != timer) return;
    state.timer = 0;
    const int command = state.pending;
    state.pending = 0;
    if (command && !openPanel(command))
        log("remote-assistant open-failed id=" + std::to_string(command));
}

static void submitted(void* widget, const void* method) {
    // Read the actual submitting widget before the client clears its input.
    // Both the Return key and the Send button call this method on Unity's thread.
    log("remote-assistant message-submitted");
    const int command = captureCommand(widget);
    state.originalSubmit(widget, method);
    if (!command) return;
    log("remote-assistant command-captured source=SubmitMessage id=" + std::to_string(command));
    if (state.timer) KillTimer(nullptr, state.timer);
    state.pending = command;
    state.timer = SetTimer(nullptr, 0, 250, openPending);
    if (!state.timer) {
        state.pending = 0;
        log("remote-assistant timer-failed error=" + std::to_string(GetLastError()));
    }
}

static void absoluteJump(BYTE* at, const void* destination) {
    const BYTE prefix[] = {0xff, 0x25, 0, 0, 0, 0};
    memcpy(at, prefix, sizeof(prefix));
    memcpy(at + sizeof(prefix), &destination, sizeof(destination));
}

static bool installSubmitHook() {
    // Current 4.0.25034 SubmitMessage begins with one position-independent
    // instruction: mov [rsp+10h], rbx. Only this complete instruction is moved.
    // Resolve the address from metadata and reject a changed prologue.
    auto target = state.submitMethod ? *reinterpret_cast<BYTE* const*>(state.submitMethod) : nullptr;
    const BYTE prologue[] = {0x48, 0x89, 0x5c, 0x24, 0x10};
    if (!target || memcmp(target, prologue, sizeof(prologue))) {
        log("remote-assistant submit-prologue-mismatch");
        return false;
    }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t base = reinterpret_cast<uintptr_t>(target) & ~(granularity - 1);
    BYTE* bridge = nullptr;
    for (uintptr_t delta = granularity; delta < 0x7fff0000 && !bridge; delta += granularity) {
        for (int direction : {1, -1}) {
            const uintptr_t at = direction > 0 ? base + delta : base - delta;
            if (at < granularity || at > reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress)) continue;
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region)) || region.State != MEM_FREE) continue;
            bridge = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(at), 64,
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (bridge) break;
        }
    }
    if (!bridge) {
        log("remote-assistant submit-bridge-allocation-failed");
        return false;
    }
    memcpy(bridge, target, sizeof(prologue));
    absoluteJump(bridge + sizeof(prologue), target + sizeof(prologue));
    absoluteJump(bridge + 32, reinterpret_cast<const void*>(submitted));
    DWORD oldProtect = 0;
    if (!VirtualProtect(bridge, 64, PAGE_EXECUTE_READ, &oldProtect)) {
        VirtualFree(bridge, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), bridge, 64);
    BYTE patch[sizeof(prologue)] = {0xe9};
    const auto relative = reinterpret_cast<intptr_t>(bridge + 32) - reinterpret_cast<intptr_t>(target + sizeof(patch));
    const auto displacement = static_cast<int32_t>(relative);
    if (relative != displacement || !VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(bridge, 0, MEM_RELEASE);
        return false;
    }
    state.originalSubmit = reinterpret_cast<SubmitMessage>(bridge);
    memcpy(patch + 1, &displacement, sizeof(displacement));
    memcpy(target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    DWORD unused = 0;
    VirtualProtect(target, sizeof(patch), oldProtect, &unused);
    log("remote-assistant submit-hook-ready");
    return true;
}

static bool resolve(HMODULE module) {
    auto proc = [&](const char* name) { return realGetProc(module, name); };
    state.api.domainGet = reinterpret_cast<DomainGet>(proc("il2cpp_domain_get"));
    state.api.assemblies = reinterpret_cast<DomainAssemblies>(proc("il2cpp_domain_get_assemblies"));
    state.api.attach = reinterpret_cast<ThreadAttach>(proc("il2cpp_thread_attach"));
    state.api.detach = reinterpret_cast<ThreadDetach>(proc("il2cpp_thread_detach"));
    state.api.assemblyImage = reinterpret_cast<AssemblyImage>(proc("il2cpp_assembly_get_image"));
    state.api.imageName = reinterpret_cast<ImageName>(proc("il2cpp_image_get_name"));
    state.api.classFromName = reinterpret_cast<ClassFromName>(proc("il2cpp_class_from_name"));
    state.api.classParent = reinterpret_cast<ClassParent>(proc("il2cpp_class_get_parent"));
    state.api.fieldFromName = reinterpret_cast<FieldFromName>(proc("il2cpp_class_get_field_from_name"));
    state.api.fieldOffset = reinterpret_cast<FieldOffset>(proc("il2cpp_field_get_offset"));
    state.api.methodFromName = reinterpret_cast<MethodFromName>(proc("il2cpp_class_get_method_from_name"));
    state.api.classGetType = reinterpret_cast<ClassGetType>(proc("il2cpp_class_get_type"));
    state.api.typeGetObject = reinterpret_cast<TypeGetObject>(proc("il2cpp_type_get_object"));
    state.api.runtimeInvoke = reinterpret_cast<RuntimeInvoke>(proc("il2cpp_runtime_invoke"));
    state.api.stringLength = reinterpret_cast<StringLength>(proc("il2cpp_string_length"));
    state.api.stringChars = reinterpret_cast<StringChars>(proc("il2cpp_string_chars"));
    state.api.objectUnbox = reinterpret_cast<ObjectUnbox>(proc("il2cpp_object_unbox"));
    state.api.formatException = reinterpret_cast<FormatException>(proc("il2cpp_format_exception"));
    if (!state.api.domainGet || !state.api.assemblies || !state.api.attach || !state.api.detach ||
        !state.api.assemblyImage || !state.api.imageName || !state.api.classFromName || !state.api.classParent ||
        !state.api.fieldFromName || !state.api.fieldOffset || !state.api.methodFromName || !state.api.classGetType ||
        !state.api.typeGetObject || !state.api.runtimeInvoke || !state.api.stringLength || !state.api.stringChars || !state.api.objectUnbox) {
        log("remote-assistant missing-il2cpp-api");
        return false;
    }
    void* domain = nullptr;
    void* gameImage = nullptr;
    void* unityImage = nullptr;
    for (int attempt = 0; attempt < 3000 && !stopping; ++attempt) {
        domain = state.api.domainGet();
        if (domain) {
            gameImage = findImage(domain, "Assembly-CSharp.dll");
            unityImage = findImage(domain, "UnityEngine.CoreModule.dll");
        }
        if (gameImage && unityImage) break;
        Sleep(100);
    }
    if (!domain || !gameImage || !unityImage) {
        log("remote-assistant assembly-missing");
        return false;
    }
    auto thread = state.api.attach(domain);
    state.conversationClass = state.api.classFromName(gameImage, "RF4.Client.UI.HUD", "RUIConversationWidget");
    state.inputClass = state.api.classFromName(gameImage, "", "UIInput");
    state.modelClass = state.api.classFromName(gameImage, "", "kjbnbfcfikl");
    auto shopClass = state.api.classFromName(gameImage, "RF4.Client.FishingScene", "Shop");
    auto objectClass = state.api.classFromName(unityImage, "UnityEngine", "Object");
    const bool classes = state.conversationClass && state.inputClass && state.modelClass && shopClass && objectClass;
    const bool inputField = fieldOffsetInHierarchy(state.conversationClass, "m_input", state.conversationInputOffset);
    const bool modelField = fieldOffsetInHierarchy(state.conversationClass, "nnghlepcnlf", state.conversationModelOffset);
    const bool valueField = fieldOffsetInHierarchy(state.inputClass, "mValue", state.inputValueOffset);
    const bool uidField = fieldOffsetInHierarchy(state.modelClass, "belnckkjghg", state.modelUidOffset);
    auto sceneManager = state.api.classFromName(unityImage, "UnityEngine.SceneManagement", "SceneManager");
    auto sceneClass = state.api.classFromName(unityImage, "UnityEngine.SceneManagement", "Scene");
    state.getActiveScene = sceneManager ? state.api.methodFromName(sceneManager, "GetActiveScene", 0) : nullptr;
    state.getSceneName = sceneClass ? state.api.methodFromName(sceneClass, "get_name", 0) : nullptr;
    state.findObjectsOfType = state.api.methodFromName(objectClass, "FindObjectsOfType", 2);
    state.findObjectsParamCount = state.findObjectsOfType ? 2 : 0;
    if (!state.findObjectsOfType) {
        state.findObjectsOfType = state.api.methodFromName(objectClass, "FindObjectsOfType", 1);
        state.findObjectsParamCount = state.findObjectsOfType ? 1 : 0;
    }
    state.submitMethod = state.api.methodFromName(state.conversationClass, "SubmitMessage", 0);
    const bool methods = state.findObjectsOfType && state.submitMethod && state.getActiveScene && state.getSceneName;
    bool ok = classes && inputField && modelField && valueField && uidField && methods;
    int availablePanels = 0;
    if (ok) {
        for (auto& panel : panels) {
            auto panelClass = state.api.classFromName(gameImage, "RF4.Client.FishingScene", panel.className);
            auto methodClass = panel.methodClassName
                ? state.api.classFromName(gameImage, "RF4.Client.FishingScene", panel.methodClassName)
                : panelClass;
            panel.type = panelClass ? state.api.typeGetObject(state.api.classGetType(panelClass)) : nullptr;
            panel.open = methodClass ? state.api.methodFromName(methodClass, panel.methodName, 0) : nullptr;
            const char* idField = !strcmp(panel.label, "shop-workshop-repair") || !strcmp(panel.label, "workshop")
                ? "workshopID" : !strcmp(panel.label, "fish-market") || !strcmp(panel.label, "cafe") ? "marketID" : "storeID";
            panel.idReady = panelClass && fieldOffsetInHierarchy(panelClass, idField, panel.idOffset);
            if (panel.type && panel.open && panel.idReady) {
                ++availablePanels;
                log(std::string("remote-assistant panel-ready ") + panel.label);
            } else {
                log(std::string("remote-assistant panel-metadata-missing ") + panel.label +
                    " class=" + std::to_string(panelClass != nullptr) +
                    " method=" + std::to_string(panel.open != nullptr) +
                    " type=" + std::to_string(panel.type != nullptr));
            }
        }
        ok = availablePanels > 0;
    }
    state.api.detach(thread);
    if (!ok) {
        log("remote-assistant metadata-mismatch classes=" + std::to_string(classes) +
            " input-field=" + std::to_string(inputField) + " model-field=" + std::to_string(modelField) +
            " value-field=" + std::to_string(valueField) + " uid-field=" + std::to_string(uidField) +
            " methods=" + std::to_string(methods) +
            " panels=" + std::to_string(availablePanels));
    }
    return ok;
}

static bool start(HMODULE module) {
    if (!resolve(module)) return false;
    if (!installSubmitHook()) return false;
    log("remote-assistant bridge-ready");
    return true;
}

} // namespace remote_assistant
