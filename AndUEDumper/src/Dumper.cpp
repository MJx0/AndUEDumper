#include "Dumper.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "UE/UEMemory.hpp"
using namespace UEMemory;

#include "UPackageGenerator.hpp"
#include "UECoreEmbed.hpp"
#include "UtfcppEmbed.hpp"

#define kVECTOR_CONTAINS(vec, val) (std::find(vec.begin(), vec.end(), val) != vec.end())

namespace dumper_jf_ns
{
    static uintptr_t base_address = 0;
    struct JsonFunction
    {
        std::string Parent;
        std::string Name;
        uint64_t Address = 0;
    };
    static std::vector<JsonFunction> jsonFunctions;

    void to_json(json &j, const JsonFunction &jf)
    {
        if (jf.Parent.empty() || jf.Parent == "None" || jf.Parent == "null")
            return;
        if (jf.Name.empty() || jf.Name == "None" || jf.Name == "null")
            return;
        if (jf.Address == 0 || jf.Address <= base_address)
            return;

        std::string fname = IOUtils::replace_specials(jf.Parent, '_');
        fname += "$$";
        fname += IOUtils::replace_specials(jf.Name, '_');

        j = json{{"Name", fname}, {"Address", (jf.Address - base_address)}};
    }
}  // namespace dumper_jf_ns

bool UEDumper::Init(IGameProfile *profile)
{
    UEVarsInitStatus initStatus = profile->InitUEVars();
    if (initStatus != UEVarsInitStatus::SUCCESS)
    {
        _lastError = UEVars::InitStatusToStr(initStatus);
        return false;
    }
    _profile = profile;
    return true;
}

bool UEDumper::Dump(std::unordered_map<std::string, BufferFmt> *outBuffersMap)
{
    outBuffersMap->insert({"Logs.txt", BufferFmt()});
    BufferFmt &logsBufferFmt = outBuffersMap->at("Logs.txt");

    {
        if (_dumpExeInfoNotify) _dumpExeInfoNotify(false);
        DumpExecutableInfo(logsBufferFmt);
        if (_dumpExeInfoNotify) _dumpExeInfoNotify(true);
    }

    {
        if (_dumpNamesInfoNotify) _dumpNamesInfoNotify(false);
        DumpNamesInfo(logsBufferFmt);
        if (_dumpNamesInfoNotify) _dumpNamesInfoNotify(true);
    }

    {
        if (_dumpObjectsInfoNotify) _dumpObjectsInfoNotify(false);
        DumpObjectsInfo(logsBufferFmt);
        if (_dumpObjectsInfoNotify) _dumpObjectsInfoNotify(true);
    }

    {
        if (_dumpOffsetsInfoNotify) _dumpOffsetsInfoNotify(false);
        outBuffersMap->insert({"Offsets.hpp", BufferFmt()});
        BufferFmt &offsetsBufferFmt = outBuffersMap->at("Offsets.hpp");
        DumpOffsetsInfo(logsBufferFmt, offsetsBufferFmt);
        if (_dumpOffsetsInfoNotify) _dumpOffsetsInfoNotify(true);
    }

    outBuffersMap->insert({"Objects.txt", BufferFmt()});
    BufferFmt &objsBufferFmt = outBuffersMap->at("Objects.txt");
    std::vector<std::pair<uint8_t *const, std::vector<UE_UObject>>> packages;
    GatherUObjects(logsBufferFmt, objsBufferFmt, packages, _objectsProgressCallback);

    if (packages.empty())
    {
        logsBufferFmt.append("Error: Packages are empty.\n");
        logsBufferFmt.append("==========================\n");
        _lastError = "ERROR_EMPTY_PACKAGES";
        return false;
    }

    BuildProcessedPackages(packages, _dumpProgressCallback);

    outBuffersMap->insert({"AIOHeader.hpp", BufferFmt()});
    BufferFmt &aioBufferFmt = outBuffersMap->at("AIOHeader.hpp");
    DumpAIOHeader(logsBufferFmt, aioBufferFmt);

    // SDK_B (UECore-style) was removed — the SDK_B variant only made sense
    // alongside an AIOHeader that didn't carry its own preamble, and that
    // refactor caused per-TU memory blow-ups when consuming SDK_B's
    // monolithic AIOHeader from hundreds of _functions.cpp at once. Plan A
    // (per-pkg .hpp + .cpp) and the standalone monolithic AIOHeader.hpp
    // cover both use cases without the cross-product cost.
    if (_sdkMode == SDKMode::Both
        || _sdkMode == SDKMode::OnlyA
        || _sdkMode == SDKMode::OnlyB) // OnlyB is legacy; treated as OnlyA
        DumpSDK_PerPackage(logsBufferFmt, *outBuffersMap);

    dumper_jf_ns::base_address = _profile->GetUnrealELF().base();
    if (dumper_jf_ns::jsonFunctions.size())
    {
        logsBufferFmt.append("Generating script json...\nFunctions: {}\n", dumper_jf_ns::jsonFunctions.size());
        logsBufferFmt.append("==========================\n");

        outBuffersMap->insert({"script.json", BufferFmt()});
        BufferFmt &scriptBufferFmt = outBuffersMap->at("script.json");

        json js;
        for (const auto &jf : dumper_jf_ns::jsonFunctions)
        {
            js["Functions"].push_back(jf);
        }

        scriptBufferFmt.append("{}", js.dump(4));
    }

    return true;
}

void UEDumper::DumpExecutableInfo(BufferFmt &logsBufferFmt)
{
    auto ue_elf = _profile->GetUnrealELF();
    logsBufferFmt.append("e_machine: 0x{:X}\n", ue_elf.header().e_machine);
    logsBufferFmt.append("Library: {}\n", ue_elf.realPath().c_str());
    logsBufferFmt.append("BaseAddress: 0x{:X}\n", ue_elf.base());

    for (const auto &it : ue_elf.segments())
        logsBufferFmt.append("{}\n", it.toString());

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpNamesInfo(BufferFmt &logsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t namesPtr = _profile->GetUEVars()->GetNamesPtr();

    if (!_profile->IsUsingFNamePool())
    {
        logsBufferFmt.append("GNames: [<Base> + 0x{:X}] = 0x{:X}\n",
                             namesPtr - baseAddr, namesPtr);
    }
    else
    {
        logsBufferFmt.append("FNamePool: [<Base> + 0x{:X}] = 0x{:X}\n",
                             namesPtr - baseAddr, namesPtr);
    }

    logsBufferFmt.append("Test dumping first 5 name entries\n");
    for (int i = 0; i < 5; i++)
    {
        logsBufferFmt.append("GetNameByID({}): {}\n", i, _profile->GetUEVars()->GetNameByID(i));
    }

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpObjectsInfo(BufferFmt &logsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t objectArrayPtr = _profile->GetUEVars()->GetGUObjectsArrayPtr();
    uintptr_t objObjectsPtr = _profile->GetUEVars()->GetObjObjectsPtr();

    logsBufferFmt.append("GUObjectArray: [<Base> + 0x{:X}] = 0x{:X}\n", objectArrayPtr - baseAddr, objectArrayPtr);
    logsBufferFmt.append("ObjObjects: [<Base> + 0x{:X}] = 0x{:X}\n", objObjectsPtr - baseAddr, objObjectsPtr);
    logsBufferFmt.append("ObjObjects Num: {}\n", UEWrappers::GetObjects()->GetNumElements());

    logsBufferFmt.append("Test Dumping First 5 Name Entries\n");
    for (int i = 0; i < 5; i++)
    {
        UE_UObject obj = UEWrappers::GetObjects()->GetObjectPtr(i);
        logsBufferFmt.append("GetObjectPtr({}): {}\n", i, obj.GetName());
    }

    logsBufferFmt.append("==========================\n");
}

void UEDumper::DumpOffsetsInfo(BufferFmt &logsBufferFmt, BufferFmt &offsetsBufferFmt)
{
    uintptr_t baseAddr = _profile->GetUEVars()->GetBaseAddress();
    uintptr_t namesPtr = _profile->GetUEVars()->GetNamesPtr();
    uintptr_t objectsArrayPtr = _profile->GetUEVars()->GetGUObjectsArrayPtr();
    uintptr_t objObjectsPtr = _profile->GetUEVars()->GetObjObjectsPtr();
    uintptr_t UEnginePtr = 0, UWorldPtr = 0, ProcessEventPtr = 0;
    int ProcessEventIndex = 0;

    // Find UEngine & UWorld
    uint8_t *UEngineObj = nullptr, *UWorldObj = nullptr;
    if (((UE_UObject)UEWrappers::GetObjects()->GetObjectPtr(1)).GetIndex() == 1)
    {
        UE_UClass UEngineClass = UEWrappers::GetObjects()->FindObject("Class Engine.Engine").Cast<UE_UClass>();
        UE_UClass UWorldClass = UEWrappers::GetObjects()->FindObject("Class Engine.World").Cast<UE_UClass>();

        logsBufferFmt.append("Finding GEngine & GWorld...\n");
        logsBufferFmt.append("{} -> 0x{:X}\n", UEngineClass.GetFullName(), uintptr_t(UEngineClass.GetAddress()));
        logsBufferFmt.append("{} -> 0x{:X}\n", UWorldClass.GetFullName(), uintptr_t(UWorldClass.GetAddress()));

        if (UEngineClass || UWorldClass)
        {
            UEWrappers::GetObjects()->ForEachObject([&UEngineClass, &UWorldClass, &UEngineObj, &UWorldObj](UE_UObject object) -> bool
            {
                if (!object.HasFlags(EObjectFlags::ClassDefaultObject))
                {
                    bool isUEngine = UEngineClass && object.IsA(UEngineClass);
                    bool isUWorld = UWorldClass && object.IsA(UWorldClass);
                    if (isUEngine) UEngineObj = object.GetAddress();
                    if (isUWorld) UWorldObj = object.GetAddress();
                }
                return ((!UEngineClass || UEngineObj != nullptr) && (!UWorldClass || UWorldObj != nullptr));
            });
        }

        auto ueSegs = _profile->GetUnrealELF().segments();

        // reverse search, start with .bss
        for (auto it = ueSegs.begin(); it != ueSegs.end(); ++it)
        {
            if (!it->is_rw || it->startAddress == baseAddr)
                continue;

            std::vector<char> buffer(it->length, 0);
            vm_rpm_ptr((void *)it->startAddress, buffer.data(), buffer.size());

            UEnginePtr = FindAlignedPointerRefrence(it->startAddress, buffer, (uintptr_t)UEngineObj);
            UWorldPtr = FindAlignedPointerRefrence(it->startAddress, buffer, (uintptr_t)UWorldObj);

            if (UEnginePtr != 0 || UWorldPtr != 0)
                break;
        }

        if (!UEnginePtr)
            logsBufferFmt.append("Couldn't find refrence to GEngine.\n");
        else
            logsBufferFmt.append("GEngine: [<Base> + 0x{:X}] = 0x{:X}\n", UEnginePtr - baseAddr, UEnginePtr);

        if (!UWorldPtr)
            logsBufferFmt.append("Couldn't find refrence to GWorld.\n");
        else
            logsBufferFmt.append("GWorld: [<Base> + 0x{:X}] = 0x{:X}\n", UWorldPtr - baseAddr, UWorldPtr);

        logsBufferFmt.append("Finding ProcessEvent...\n");
        uint8_t *obj = UEngineObj ? UEngineObj : UWorldObj;
        if (!obj || !_profile->findProcessEvent(obj, &ProcessEventPtr, &ProcessEventIndex))
            logsBufferFmt.append("Couldn't find ProcessEvent.\n");
        else
            logsBufferFmt.append("ProcessEvent: Index({}) | [<Base> + 0x{:X}] = 0x{:X}\n", ProcessEventIndex, ProcessEventPtr - baseAddr, ProcessEventPtr);
    }

    UE_Pointers uEPointers{};
    uEPointers.Names = namesPtr - baseAddr;
    uEPointers.UObjectArray = objectsArrayPtr - baseAddr;
    uEPointers.ObjObjects = objObjectsPtr - baseAddr;
    uEPointers.Engine = UEnginePtr ? (UEnginePtr - baseAddr) : 0;
    uEPointers.World = UWorldPtr ? (UWorldPtr - baseAddr) : 0;
    uEPointers.ProcessEvent = ProcessEventPtr ? (ProcessEventPtr - baseAddr) : 0;
    uEPointers.ProcessEventIndex = ProcessEventIndex;

    _processEventIndex = ProcessEventIndex;

    offsetsBufferFmt.append("#pragma once\n\n#include <cstdint>\n\n\n");
    offsetsBufferFmt.append("{}\n\n{}", _profile->GetOffsets()->ToString(), uEPointers.ToString());

    logsBufferFmt.append("==========================\n");
}

void UEDumper::GatherUObjects(BufferFmt &logsBufferFmt, BufferFmt &objsBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback)
{
    logsBufferFmt.append("Gathering UObjects...\n");

    if (UEWrappers::GetObjects()->GetNumElements() <= 0)
    {
        logsBufferFmt.append("UEWrappers::GetObjects()->GetNumElements() <= 0\n");
        logsBufferFmt.append("==========================\n");
        return;
    }

    if (((UE_UObject)UEWrappers::GetObjects()->GetObjectPtr(1)).GetIndex() != 1)
    {
        logsBufferFmt.append("UEWrappers::GetObjects()->GetObjectPtr(1).GetIndex() != 1\n");
        logsBufferFmt.append("==========================\n");
        return;
    }

    int objectsCount = UEWrappers::GetObjects()->GetNumElements();
    SimpleProgressBar objectsProgress(objectsCount);
    if (progressCallback)
        progressCallback(objectsProgress);

    for (int i = 0; i < objectsCount; i++)
    {
        UE_UObject object = UEWrappers::GetObjects()->GetObjectPtr(i);
        if (object)
        {
            if (object.IsA<UE_UFunction>() || object.IsA<UE_UStruct>() || object.IsA<UE_UEnum>())
            {
                bool found = false;
                auto packageObj = object.GetPackageObject();
                for (auto &pkg : packages)
                {
                    if (pkg.first == packageObj)
                    {
                        pkg.second.push_back(object);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    packages.push_back(std::make_pair(packageObj, std::vector<UE_UObject>(1, object)));
                }
            }

            objsBufferFmt.append("[{:010}]: {}\n", object.GetIndex(), object.GetFullName());
        }

        objectsProgress++;
        if (progressCallback)
            progressCallback(objectsProgress);
    }

    logsBufferFmt.append("Gathered {} Objects (Packages {})\n", objectsCount, packages.size());
    logsBufferFmt.append("==========================\n");
}

// Self-contained preamble with minimal layout-correct stubs for the predefined
// UE types that the dumper references in member type strings (FName, FString,
// containers, smart pointers, delegates, etc.). Sizes match what the dumper
// itself reports in the AIOHeader (// 0xX(0xY) comments).
//
// Guarded by AIOHeader_BASIC_TYPES_DEFINED so callers that already provide
// their own definitions (e.g. UECore in this project) can opt out by
// #defining the guard before including AIOHeader.hpp.
static const char *kAIOPreamble = R"AIOPRE(
#ifndef AIOHeader_BASIC_TYPES_DEFINED
#define AIOHeader_BASIC_TYPES_DEFINED

// Placeholders the dumper emits when an inner type couldn't be resolved.
// Kept incomplete on purpose — only valid through pointer/template usage.
struct None;
struct FNone;

template <typename T>
struct TArray
{
    T* Data;
    int32_t NumElements;
    int32_t MaxElements;
};

struct FString : TArray<wchar_t> {};

// FName matches UECore Basic.h's layout (8 bytes: ComparisonIndex + Number).
// s_NameResolver is the FName -> ANSI string hook the user wires once at
// startup — required because some games encrypt the FName pool, so the
// translation can't be baked at dump time. ToString() also strips the
// outer-package path component (so "Engine.Actor" becomes "Actor"),
// matching UECore's GetName() semantics.
class FName final
{
public:
    static inline std::function<std::string(int32_t)> s_NameResolver;

// `bWITH_CASE_PRESERVING_NAME` is patched at dump time based on the
// game profile's Config.isUsingCasePreservingName. When false, the FName
// is 8 bytes (ComparisonIndex/DisplayIndex aliased via union); when
// true, it's 12 bytes with both fields stored separately.
#define bWITH_CASE_PRESERVING_NAME false
#if !bWITH_CASE_PRESERVING_NAME
    union {
#endif
        int32_t ComparisonIndex;
        int32_t DisplayIndex;
#if !bWITH_CASE_PRESERVING_NAME
    };
#endif
    uint32_t Number;

    static std::string GetPlainANSIString(const FName* N)
    {
        if (s_NameResolver) return s_NameResolver(N->ComparisonIndex);
        return {};
    }
    std::string GetRawString() const { return GetPlainANSIString(this); }
    std::string ToString() const
    {
        std::string s = GetRawString();
        size_t pos = s.rfind('/');
        return pos == std::string::npos ? s : s.substr(pos + 1);
    }
    bool operator==(const FName& O) const
    { return ComparisonIndex == O.ComparisonIndex && Number == O.Number; }
    bool operator!=(const FName& O) const { return !(*this == O); }
};

// Forward decl — UObject defined later by the dumper but referenced from
// FUObjectItem / TUObjectArray below.
struct UObject;

// Per-slot record in the GObjects table. Object pointer at offset 0;
// the rest is engine bookkeeping (flags, cluster index, ...).
struct FUObjectItem
{
    UObject* Object;
    uint8_t Pad_8[0x10];
};

// Chunked global object array. Layout matches UE 4.20+ TUObjectArray.
// NumElementsPerChunk = 0 disables chunking (older UE / non-chunked
// builds); set it to 65536 (the modern default) at runtime if needed.
class TUObjectArray
{
public:
    int32_t NumElementsPerChunk = 0x10000; // 65536
    FUObjectItem** Objects;
    uint8_t Pad_8[0x8];
    int32_t MaxElements;
    int32_t NumElements;
    int32_t MaxChunks;
    int32_t NumChunks;

    inline int32_t Num() const { return NumElements; }

    inline UObject* GetByIndex(const int32_t Index) const
    {
        if (Index < 0 || Index >= NumElements || !Objects)
            return nullptr;
        // Object is at offset 0 of FUObjectItem.
        if (NumElementsPerChunk <= 0)
        {
            return *reinterpret_cast<UObject**>(
                reinterpret_cast<uintptr_t>(Objects) + Index * sizeof(FUObjectItem));
        }
        const int32_t ChunkIndex = Index / NumElementsPerChunk;
        const int32_t WithinChunkIndex = Index % NumElementsPerChunk;
        uintptr_t chunk = *reinterpret_cast<uintptr_t*>(Objects + ChunkIndex);
        if (!chunk) return nullptr;
        return *reinterpret_cast<UObject**>(
            chunk + WithinChunkIndex * sizeof(FUObjectItem));
    }
};

// Late-binding pointer wrapper: user code only knows the address of
// GUObjectArray after dlopen + offset resolution, so GObjects holds a
// void* and lazily reinterprets it as a TUObjectArray*. Wire via
// `UObject::GObjects.InitManually(addr)` once at startup.
class TUObjectArrayWrapper
{
private:
    void* GObjectsAddress = nullptr;
public:
    inline void InitManually(void* Addr) { GObjectsAddress = Addr; }
    inline TUObjectArray* operator->()
    { return reinterpret_cast<TUObjectArray*>(GObjectsAddress); }
    inline const TUObjectArray* operator->() const
    { return reinterpret_cast<const TUObjectArray*>(GObjectsAddress); }
    inline operator bool() const { return GObjectsAddress != nullptr; }
};

struct FText
{
    uint8_t Pad_0[24];
};

template <typename K, typename V>
struct TPair
{
    K Key;
    V Value;
};

template <typename K, typename V>
struct TMap
{
    uint8_t Pad_0[80];
};

template <typename T>
struct TSet
{
    uint8_t Pad_0[80];
};

struct FWeakObjectPtr
{
    int32_t ObjectIndex;
    int32_t ObjectSerialNumber;
};

template <typename T>
struct TWeakObjectPtr : FWeakObjectPtr {};

struct FUniqueObjectGuid
{
    uint32_t A, B, C, D;
};

template <typename T>
struct TLazyObjectPtr
{
    FWeakObjectPtr WeakPtr;
    int32_t TagAtLastTest;
    FUniqueObjectGuid ObjectID;
};

// FSoftObjectPath is emitted by the dump itself (CoreUObject.SoftObjectPath)
// — its layout depends on FName size, which varies per game. We opaque-ify
// FSoftObjectPtr here so it doesn't need a complete FSoftObjectPath at
// preamble-emission time. Size pinned at 40 to match TSoftObjectPtr<T>
// observed in dumps.
struct FSoftObjectPtr
{
    uint8_t Pad_0[40];
};

template <typename T>
struct TSoftObjectPtr : FSoftObjectPtr {};

template <typename T>
struct TSoftClassPtr : FSoftObjectPtr {};

template <typename T>
struct TSubclassOf
{
    void* ClassPtr;
};

struct FScriptInterface
{
    void* ObjectPointer;
    void* InterfacePointer;
};

template <typename T>
struct TScriptInterface : FScriptInterface {};

struct FFieldClass
{
    uint8_t Pad_0[40];
};

struct FProperty
{
    uint8_t Pad_0[136];
};

struct FFieldPath
{
    void* ResolvedField;
    TWeakObjectPtr<void> ResolvedOwner;
    TArray<FName> Path;
};

template <typename T>
struct TFieldPath : FFieldPath {};

struct FScriptDelegate
{
    FWeakObjectPtr Object;
    FName FunctionName;
};

struct FDelegate
{
    FScriptDelegate BoundFunction;
};

struct FMulticastInlineDelegate
{
    TArray<FScriptDelegate> InvocationList;
};

struct FMulticastDelegate
{
    TArray<FScriptDelegate> InvocationList;
};

struct FMulticastSparseDelegate
{
    void* SparseInvocationList;
    uint8_t Pad_8[8];
};

// Forward decl with explicit underlying type. Phase 1.6 emits
// `EClassCastFlags CastFlags` on UClass; the forward decl is enough for
// the member declaration and for static_cast<uint64_t>(flags) inside
// helper bodies. For the actual flag *values*, include AIOMeta.hpp
// alongside this header (it provides the full enum class).
enum class EClassCastFlags : uint64_t;

// Compile-time string literal usable as a non-type template parameter
// (C++20 structural literal type). Backs the
// `DEFINE_UE_CLASS_HELPERS(T, "Name")` macro and the StaticClassImpl<>
// dispatcher below.
template<int Len>
struct StringLiteral
{
    char Chars[Len];
    consteval StringLiteral(const char(&S)[Len])
    {
        for (int i = 0; i < Len; ++i) Chars[i] = S[i];
    }
};

// Per-class static helpers wired by DEFINE_UE_CLASS_HELPERS. Definitions
// live in the AIOCore helpers block emitted at the bottom of this header
// — they call AIOCore::g_FindClassByName / read UClass::DefaultObject.
struct UClass; // declared by the dumper later in this header
struct UObject;
template<StringLiteral Name>
inline struct UClass* StaticClassImpl();

template<typename T>
inline T* GetDefaultObjImpl();

#define DEFINE_UE_CLASS_HELPERS(FullClassName, ClassNameStr) \
    static struct UClass* StaticClass() { return StaticClassImpl<ClassNameStr>(); } \
    static struct FullClassName* GetDefaultObj() { return GetDefaultObjImpl<FullClassName>(); }

// AIOCore namespace exists for the kProcessEventIndex constant emitted
// by the helpers block at the bottom of the header. The other runtime
// glue is on the types themselves: FName::s_NameResolver (FName decode,
// game-specific because some pool layouts are encrypted) and
// UObject::GObjects (wire via InitManually(GUObjectArray address)).
// Both wired once from your bridge.

#endif // AIOHeader_BASIC_TYPES_DEFINED
)AIOPRE";

// Topological sort using DFS — produces a "deps first" ordering. For nodes
// with cycles, we emit them in the order DFS encountered them (this keeps
// behavior deterministic; callers that need full cycle resolution must
// supplement with forward declarations).
template <typename Node, typename DepsFn>
static std::vector<Node> TopoSort(const std::vector<Node> &nodes, DepsFn getDeps)
{
    enum class State : uint8_t { Unvisited, Visiting, Visited };
    std::unordered_map<Node, State> state;
    state.reserve(nodes.size());
    for (const auto &n : nodes) state.emplace(n, State::Unvisited);

    std::vector<Node> out;
    out.reserve(nodes.size());

    std::function<void(const Node &)> dfs = [&](const Node &n)
    {
        auto it = state.find(n);
        if (it == state.end()) return;            // not in our universe
        if (it->second != State::Unvisited) return; // visited or in progress (cycle)
        it->second = State::Visiting;
        for (const auto &dep : getDeps(n))
        {
            dfs(dep);
        }
        it->second = State::Visited;
        out.push_back(n);
    };

    for (const auto &n : nodes) dfs(n);
    return out;
}

void UEDumper::BuildProcessedPackages(UEPackagesArray &packages, const ProgressCallback &progressCallback)
{
    _sdkProcessed.clear();
    _sdkNameToPkg.clear();
    _sdkEnumUnderlying.clear();
    _sdkPkgOrder.clear();
    _sdkPhantomEnums.clear();
    _sdkPhantomStructs.clear();
    _sdkPackagesUnsaved.clear();

    SimpleProgressBar dumpProgress(int(packages.size()));
    if (progressCallback)
        progressCallback(dumpProgress);

    auto excludedObjects = _profile->GetExcludedObjects();

    // ---- Phase 1: process every package and collect type metadata ------------

    _sdkProcessed.reserve(packages.size());

    auto isExcluded = [&excludedObjects](const std::string &fullName)
    {
        return !excludedObjects.empty() && kVECTOR_CONTAINS(excludedObjects, fullName);
    };

    for (auto &raw : packages)
    {
        UE_UPackage pkg(raw);
        pkg.Process();

        dumpProgress++;
        if (progressCallback)
            progressCallback(dumpProgress);

        if (excludedObjects.size())
        {
            pkg.Enums.erase(
                std::remove_if(pkg.Enums.begin(), pkg.Enums.end(),
                               [&](const UE_UPackage::Enum &it){ return isExcluded(it.FullName); }),
                pkg.Enums.end());
            pkg.Structures.erase(
                std::remove_if(pkg.Structures.begin(), pkg.Structures.end(),
                               [&](const UE_UPackage::Struct &it){ return isExcluded(it.FullName); }),
                pkg.Structures.end());
            pkg.Classes.erase(
                std::remove_if(pkg.Classes.begin(), pkg.Classes.end(),
                               [&](const UE_UPackage::Struct &it){ return isExcluded(it.FullName); }),
                pkg.Classes.end());
        }

        _sdkProcessed.push_back(std::move(pkg));
    }

    // ---- Phase 1.5: drop preamble-provided names + collapse duplicates -----
    //
    // The preamble defines FName/FString/FText, FSoftObjectPath, all wrapper
    // templates (TArray/TMap/...), and a handful of basic FProperty/FField
    // helpers. Real UE dumps re-emit some of these as ScriptStructs, so we
    // skip them here to avoid double definitions.
    //
    // Real dumps also produce duplicate names from objects whose FName came
    // back as "None" (UNone × N) or from games that genuinely contain two
    // unrelated classes with the same short name. We keep the first
    // occurrence and drop the rest — references via member type strings
    // resolve to the surviving one.
    {
        static const std::unordered_set<std::string> kPreambleProvided = {
            "FName", "FString", "FText",
            "FWeakObjectPtr", "FUniqueObjectGuid",
            // Note: FSoftObjectPath is NOT skipped — its layout depends on
            // the per-game FName size, so we let the dump emit the real one.
            "FSoftObjectPtr",
            "FScriptInterface", "FFieldPath", "FFieldClass", "FProperty",
            "FScriptDelegate", "FDelegate",
            "FMulticastInlineDelegate", "FMulticastDelegate",
            "FMulticastSparseDelegate",
            "TArray", "TMap", "TSet", "TPair",
            "TWeakObjectPtr", "TLazyObjectPtr",
            "TSoftObjectPtr", "TSoftClassPtr",
            "TSubclassOf", "TScriptInterface", "TFieldPath",
            "None", "FNone",
        };

        std::unordered_set<std::string> seenNames = kPreambleProvided;

        auto dropDups = [&seenNames](auto &vec)
        {
            using ItemT = typename std::remove_reference_t<decltype(vec)>::value_type;
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                               [&seenNames](const ItemT &it)
                               {
                                   if (seenNames.count(it.CppNameOnly)) return true;
                                   seenNames.insert(it.CppNameOnly);
                                   return false;
                               }),
                vec.end());
        };

        for (auto &p : _sdkProcessed)
        {
            dropDups(p.Enums);
            dropDups(p.Structures);
            dropDups(p.Classes);
        }
    }

    // ---- Phase 1.6: augment well-known core reflection classes ------------
    //
    // Property-walker output for UObject / UField / UStruct / UEnum /
    // UFunction is opaque (`Pad_0xN[0xM]`) because their layouts aren't
    // expressible through the FProperty graph. The per-game offsets of
    // their internal slots are already in `UE_Offsets`, so here we stitch
    // typed members back in, replacing those padding regions.
    //
    // Net effect: `Actor->ClassPrivate`, `Func->Func`, `Cls->SuperStruct`
    // etc. become first-class typed accesses rather than raw byte
    // arithmetic. For UObject we also inject a `ProcessEvent` declaration;
    // the inline body is emitted at the very end of the header (Phase 5c)
    // so it can dispatch through UFunction once the type graph is laid out.
    //
    // Runs after Phase 1.5 (so we don't augment structs that are about to
    // be dropped as duplicates) but before Phase 3 (so the new typed
    // members feed into the dependency graph).
    {
        const UE_Offsets &offs = *_profile->GetUEVars()->GetOffsets();
        const uint32_t fnameSize = static_cast<uint32_t>(offs.FName.Size ? offs.FName.Size : 8);

        struct KnownField
        {
            uintptr_t Offset;
            uint32_t  Size;
            std::string Type;
            std::string Name;
        };

        auto fieldsFor = [&](const std::string &cppName) -> std::vector<KnownField>
        {
            std::vector<KnownField> v;
            auto add = [&](uintptr_t off, uint32_t size, std::string type, std::string name,
                           bool allowZeroOffset = false)
            {
                // Reflected offsets that came back as 0 mean "not discovered" —
                // skip those slots so we don't smash the start of the struct.
                if (off == 0 && !allowZeroOffset) return;
                v.push_back({off, size, std::move(type), std::move(name)});
            };

            if (cppName == "UObject")
            {
                // VTable is synthetic: every UObject starts with one but the
                // dumper has no offset for it. Force-emit at 0.
                add(0,                          8, "void**",           "VTable", true);
                add(offs.UObject.ClassPrivate,  8, "struct UClass*",   "ClassPrivate");
                add(offs.UObject.OuterPrivate,  8, "struct UObject*",  "OuterPrivate");
                add(offs.UObject.ObjectFlags,   4, "uint32_t",         "ObjectFlags");
                add(offs.UObject.NamePrivate,   fnameSize, "FName",    "NamePrivate");
                add(offs.UObject.InternalIndex, 4, "int32_t",          "InternalIndex");
            }
            else if (cppName == "UField")
            {
                add(offs.UField.Next, 8, "struct UField*", "Next");
            }
            else if (cppName == "UStruct")
            {
                add(offs.UStruct.PropertiesSize,  4, "int32_t",         "PropertiesSize");
                add(offs.UStruct.SuperStruct,     8, "struct UStruct*", "SuperStruct");
                add(offs.UStruct.Children,        8, "struct UField*",  "Children");
                add(offs.UStruct.ChildProperties, 8, "void*",           "ChildProperties");
            }
            else if (cppName == "UEnum")
            {
                add(offs.UEnum.Names, 16, "TArray<TPair<FName, int64_t>>", "Names");
            }
            else if (cppName == "UFunction")
            {
                add(offs.UFunction.NumParams,      1, "uint8_t",  "NumParams");
                add(offs.UFunction.ParamSize,      2, "uint16_t", "ParamSize");
                add(offs.UFunction.EFunctionFlags, 4, "uint32_t", "EFunctionFlags");
                add(offs.UFunction.Func,           8, "void*",    "Func");
            }
            else if (cppName == "UClass")
            {
                // EClassCastFlags is forward-declared in kAIOPreamble with
                // explicit uint64 underlying type, so the member declaration
                // compiles standalone. For Plan B (namespace SDK), the
                // embedded UECore Basic.h provides the full definition.
                add(offs.UClass.CastFlags,     8, "EClassCastFlags", "CastFlags");
                add(offs.UClass.DefaultObject, 8, "struct UObject*", "DefaultObject");
            }
            return v;
        };

        auto augment = [&](UE_UPackage::Struct &s)
        {
            auto fields = fieldsFor(s.CppNameOnly);
            if (fields.empty()) return;

            std::sort(fields.begin(), fields.end(),
                      [](const KnownField &a, const KnownField &b) { return a.Offset < b.Offset; });

            // Drop fields that fall in the inherited range (already provided
            // by the parent struct) or extend past this struct's reflected
            // size. Without this guard a malformed offset (e.g. 0 reported
            // for a slot that's actually inside the parent) would smash the
            // member layout.
            fields.erase(
                std::remove_if(fields.begin(), fields.end(),
                               [&](const KnownField &f) {
                                   return f.Offset < s.Inherited
                                       || f.Offset + f.Size > s.Size;
                               }),
                fields.end());
            if (fields.empty()) return;

            std::vector<UE_UPackage::Member> rebuilt;
            rebuilt.reserve(fields.size() * 2 + 1);

            uint32_t cursor = s.Inherited;
            for (const auto &f : fields)
            {
                if (f.Offset > cursor)
                {
                    UE_UPackage::Member pad;
                    pad.Type   = "uint8_t";
                    pad.Name   = fmt::format("Pad_0x{:X}[0x{:X}]", cursor, f.Offset - cursor);
                    pad.Offset = cursor;
                    pad.Size   = f.Offset - cursor;
                    rebuilt.push_back(std::move(pad));
                }
                UE_UPackage::Member m;
                m.Type   = f.Type;
                m.Name   = f.Name;
                m.Offset = static_cast<uint32_t>(f.Offset);
                m.Size   = f.Size;
                UE_UPackage::ExtractTypeDeps(m.Type, s.FullDeps, s.ForwardDeps);
                rebuilt.push_back(std::move(m));
                cursor = static_cast<uint32_t>(f.Offset + f.Size);
            }
            if (cursor < s.Size)
            {
                UE_UPackage::Member pad;
                pad.Type   = "uint8_t";
                pad.Name   = fmt::format("Pad_0x{:X}[0x{:X}]", cursor, s.Size - cursor);
                pad.Offset = cursor;
                pad.Size   = s.Size - cursor;
                rebuilt.push_back(std::move(pad));
            }

            // ExtractTypeDeps may have re-introduced self as a dep (e.g.
            // UObject's OuterPrivate references UObject); strip it.
            s.FullDeps.erase(s.CppNameOnly);
            s.ForwardDeps.erase(s.CppNameOnly);

            s.Members = std::move(rebuilt);

            if (s.CppNameOnly == "UObject")
            {
                // === AIO Core helpers (inline bodies at end of header) ===
                //
                // ProcessEvent dispatches via vtable[ProcessEventIndex]; the
                // rest walk the typed members added above (ClassPrivate /
                // OuterPrivate / NamePrivate / ObjectFlags) plus
                // UStruct::SuperStruct (added when UStruct is augmented in
                // this same pass) and UClass::{CastFlags,DefaultObject}.
                // GetName / Name lookup goes through FName::s_NameResolver
                // (game-specific, FName pool can be encrypted). Object
                // lookup (FindObject*, FindClass*) walks GObjects directly
                // — wire via UObject::GObjects.InitManually(addr) once.
                //
                // EClassCastFlags is forward-declared in kAIOPreamble with
                // an explicit uint64_t underlying type — that's enough for
                // function signatures and `EClassCastFlags{}` default args.
                // Include AIOMeta.hpp alongside this header to get the
                // actual flag enumerators (e.g. EClassCastFlags::Actor).
                s.ExtraDecls =
                    "\t// === AIO Core helpers (inline bodies at end of header) ===\n"
                    "\tstatic inline class TUObjectArrayWrapper GObjects;\n"
                    "\n"
                    "\tvoid ProcessEvent(struct UFunction* Function, void* Parms) const;\n"
                    "\n"
                    "\tstd::string GetName() const;\n"
                    "\tstd::string GetFullName() const;\n"
                    "\n"
                    "\tbool IsA(EClassCastFlags TypeFlags) const;\n"
                    "\tbool IsA(struct UClass* cmp) const;\n"
                    "\tbool HasTypeFlag(EClassCastFlags TypeFlags) const;\n"
                    "\tbool IsDefaultObject() const;\n"
                    "\n"
                    "\tvoid TraverseSupers(const std::function<bool(const struct UObject*)>& Callback) const;\n"
                    "\n"
                    "\tstatic struct UObject* FindObjectImpl(const std::string& FullName, EClassCastFlags RequiredType = EClassCastFlags{});\n"
                    "\tstatic struct UObject* FindObjectFastImpl(const std::string& Name, EClassCastFlags RequiredType = EClassCastFlags{});\n"
                    "\n"
                    "\ttemplate<typename UEType = UObject>\n"
                    "\tstatic UEType* FindObject(const std::string& FullName, EClassCastFlags RequiredType = EClassCastFlags{})\n"
                    "\t{ return static_cast<UEType*>(FindObjectImpl(FullName, RequiredType)); }\n"
                    "\n"
                    "\ttemplate<typename UEType = UObject>\n"
                    "\tstatic UEType* FindObjectFast(const std::string& Name, EClassCastFlags RequiredType = EClassCastFlags{})\n"
                    "\t{ return static_cast<UEType*>(FindObjectFastImpl(Name, RequiredType)); }\n"
                    "\n"
                    "\tstatic struct UClass* FindClass(const std::string& ClassFullName);\n"
                    "\tstatic struct UClass* FindClassFast(const std::string& ClassName);\n";
            }
            else if (s.CppNameOnly == "UClass")
            {
                // Lookup helper used by every emitted UFunction body's
                // ProcessEvent dispatch:
                //     ClassPrivate->GetFunction("Owner", "FuncName")
                // Walks SuperStruct + Children, matched by Function-cast flag
                // (EClassCastFlags{0x80000}) and FName equality. Body emitted
                // inline in AIOHeader / out-of-line in CoreUObject_functions.cpp.
                s.ExtraDecls =
                    "\tstruct UFunction* GetFunction(const std::string& ClassName, "
                    "const std::string& FuncName) const;\n";
            }
        };

        for (auto &p : _sdkProcessed)
        {
            for (auto &c : p.Classes)    augment(c);
            for (auto &s : p.Structures) augment(s); // harmless on non-targets
        }

        // ---- Phase 1.6b: emit DEFINE_UE_CLASS_HELPERS on every Class ----
        //
        // For each dumped UClass-derived type, append a static StaticClass()
        // / GetDefaultObj() pair that resolves through StaticClassImpl<>
        // (UE-name -> UClass*) and reads the augmented UClass::DefaultObject
        // member. UScriptStructs (Structures) are skipped — they have no
        // associated UClass. Runs after augment() so the macro lands at the
        // bottom of UObject's existing helper block (after FindClass etc).
        for (auto &p : _sdkProcessed)
        {
            for (auto &c : p.Classes)
            {
                if (!c.ExtraDecls.empty())
                    c.ExtraDecls += "\n";
                c.ExtraDecls += fmt::format(
                    "\tDEFINE_UE_CLASS_HELPERS({}, \"{}\")\n",
                    c.CppNameOnly, c.Name);
            }
        }

        // ---- Phase 1.6c: math operators on well-known math structs ----
        //
        // Inject member operators (+ - * / +=  -= *= /= == != unary-)
        // and helper methods (Length / LengthSquared / Dot / Cross /
        // Distance / IsNearlyZero / GetSafeNormal) on the canonical UE
        // math types. All emitted member-scoped (T::operator+, ...) so
        // they don't leak into namespace SDK and can't conflict with
        // anything else the user pulls in. Not gated by a macro — the
        // imgui IMGUI_DEFINE_MATH_OPERATORS pattern guards free-function
        // operators, which is irrelevant once everything is on the type.
        //
        // Component-wise operations + the Length family assume the
        // canonical UE member layout (X/Y/Z, Pitch/Yaw/Roll, R/G/B/A);
        // games that re-shape these structs would need to override.
        for (auto &p : _sdkProcessed)
        {
            for (auto &s : p.Structures)
            {
                const char* mathOps = nullptr;
                if (s.CppNameOnly == "FVector")
                {
                    mathOps = R"FVOPS(
	// === Math helpers ===
	FVector  operator+(const FVector& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
	FVector  operator-(const FVector& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
	FVector  operator*(const FVector& o) const { return {X * o.X, Y * o.Y, Z * o.Z}; }
	FVector  operator/(const FVector& o) const { return {X / o.X, Y / o.Y, Z / o.Z}; }
	FVector  operator*(float s)          const { return {X * s, Y * s, Z * s}; }
	FVector  operator/(float s)          const { return {X / s, Y / s, Z / s}; }
	FVector  operator-()                 const { return {-X, -Y, -Z}; }
	FVector& operator+=(const FVector& o)      { X += o.X; Y += o.Y; Z += o.Z; return *this; }
	FVector& operator-=(const FVector& o)      { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
	FVector& operator*=(float s)               { X *= s;   Y *= s;   Z *= s;   return *this; }
	FVector& operator/=(float s)               { X /= s;   Y /= s;   Z /= s;   return *this; }
	bool     operator==(const FVector& o) const{ return X == o.X && Y == o.Y && Z == o.Z; }
	bool     operator!=(const FVector& o) const{ return !(*this == o); }
	float    LengthSquared() const             { return X*X + Y*Y + Z*Z; }
	float    Length()        const             { return std::sqrt(LengthSquared()); }
	float    Dot(const FVector& o)  const      { return X*o.X + Y*o.Y + Z*o.Z; }
	FVector  Cross(const FVector& o) const     { return {Y*o.Z - Z*o.Y, Z*o.X - X*o.Z, X*o.Y - Y*o.X}; }
	float    Distance(const FVector& o) const  { return (*this - o).Length(); }
	bool     IsNearlyZero(float t = 1e-4f) const { return std::abs(X) <= t && std::abs(Y) <= t && std::abs(Z) <= t; }
	FVector  GetSafeNormal(float t = 1e-4f) const { float l = Length(); return l > t ? FVector{X/l, Y/l, Z/l} : FVector{0,0,0}; }
)FVOPS";
                }
                else if (s.CppNameOnly == "FVector2D")
                {
                    mathOps = R"FV2OPS(
	// === Math helpers ===
	FVector2D  operator+(const FVector2D& o) const { return {X + o.X, Y + o.Y}; }
	FVector2D  operator-(const FVector2D& o) const { return {X - o.X, Y - o.Y}; }
	FVector2D  operator*(const FVector2D& o) const { return {X * o.X, Y * o.Y}; }
	FVector2D  operator/(const FVector2D& o) const { return {X / o.X, Y / o.Y}; }
	FVector2D  operator*(float s)            const { return {X * s, Y * s}; }
	FVector2D  operator/(float s)            const { return {X / s, Y / s}; }
	FVector2D  operator-()                   const { return {-X, -Y}; }
	FVector2D& operator+=(const FVector2D& o)      { X += o.X; Y += o.Y; return *this; }
	FVector2D& operator-=(const FVector2D& o)      { X -= o.X; Y -= o.Y; return *this; }
	FVector2D& operator*=(float s)                 { X *= s; Y *= s; return *this; }
	FVector2D& operator/=(float s)                 { X /= s; Y /= s; return *this; }
	bool       operator==(const FVector2D& o) const{ return X == o.X && Y == o.Y; }
	bool       operator!=(const FVector2D& o) const{ return !(*this == o); }
	float      LengthSquared() const               { return X*X + Y*Y; }
	float      Length()        const               { return std::sqrt(LengthSquared()); }
	float      Dot(const FVector2D& o) const       { return X*o.X + Y*o.Y; }
	float      Distance(const FVector2D& o) const  { return (*this - o).Length(); }
	bool       IsNearlyZero(float t = 1e-4f) const { return std::abs(X) <= t && std::abs(Y) <= t; }
	FVector2D  GetSafeNormal(float t = 1e-4f) const { float l = Length(); return l > t ? FVector2D{X/l, Y/l} : FVector2D{0,0}; }
)FV2OPS";
                }
                else if (s.CppNameOnly == "FVector4")
                {
                    mathOps = R"FV4OPS(
	// === Math helpers ===
	FVector4  operator+(const FVector4& o) const { return {X + o.X, Y + o.Y, Z + o.Z, W + o.W}; }
	FVector4  operator-(const FVector4& o) const { return {X - o.X, Y - o.Y, Z - o.Z, W - o.W}; }
	FVector4  operator*(float s)           const { return {X * s, Y * s, Z * s, W * s}; }
	FVector4  operator/(float s)           const { return {X / s, Y / s, Z / s, W / s}; }
	FVector4  operator-()                  const { return {-X, -Y, -Z, -W}; }
	FVector4& operator+=(const FVector4& o)      { X += o.X; Y += o.Y; Z += o.Z; W += o.W; return *this; }
	FVector4& operator-=(const FVector4& o)      { X -= o.X; Y -= o.Y; Z -= o.Z; W -= o.W; return *this; }
	FVector4& operator*=(float s)                { X *= s; Y *= s; Z *= s; W *= s; return *this; }
	FVector4& operator/=(float s)                { X /= s; Y /= s; Z /= s; W /= s; return *this; }
	bool      operator==(const FVector4& o) const{ return X == o.X && Y == o.Y && Z == o.Z && W == o.W; }
	bool      operator!=(const FVector4& o) const{ return !(*this == o); }
	float     LengthSquared() const              { return X*X + Y*Y + Z*Z + W*W; }
	float     Length()        const              { return std::sqrt(LengthSquared()); }
	float     Dot(const FVector4& o) const       { return X*o.X + Y*o.Y + Z*o.Z + W*o.W; }
	bool      IsNearlyZero(float t = 1e-4f) const{ return std::abs(X) <= t && std::abs(Y) <= t && std::abs(Z) <= t && std::abs(W) <= t; }
)FV4OPS";
                }
                else if (s.CppNameOnly == "FRotator")
                {
                    mathOps = R"FROPS(
	// === Math helpers (component-wise; angles, no Length-style scalar) ===
	FRotator  operator+(const FRotator& o) const { return {Pitch + o.Pitch, Yaw + o.Yaw, Roll + o.Roll}; }
	FRotator  operator-(const FRotator& o) const { return {Pitch - o.Pitch, Yaw - o.Yaw, Roll - o.Roll}; }
	FRotator  operator*(float s)           const { return {Pitch * s, Yaw * s, Roll * s}; }
	FRotator  operator/(float s)           const { return {Pitch / s, Yaw / s, Roll / s}; }
	FRotator  operator-()                  const { return {-Pitch, -Yaw, -Roll}; }
	FRotator& operator+=(const FRotator& o)      { Pitch += o.Pitch; Yaw += o.Yaw; Roll += o.Roll; return *this; }
	FRotator& operator-=(const FRotator& o)      { Pitch -= o.Pitch; Yaw -= o.Yaw; Roll -= o.Roll; return *this; }
	FRotator& operator*=(float s)                { Pitch *= s; Yaw *= s; Roll *= s; return *this; }
	FRotator& operator/=(float s)                { Pitch /= s; Yaw /= s; Roll /= s; return *this; }
	bool      operator==(const FRotator& o) const{ return Pitch == o.Pitch && Yaw == o.Yaw && Roll == o.Roll; }
	bool      operator!=(const FRotator& o) const{ return !(*this == o); }
	bool      IsNearlyZero(float t = 1e-4f) const{ return std::abs(Pitch) <= t && std::abs(Yaw) <= t && std::abs(Roll) <= t; }
)FROPS";
                }
                else if (s.CppNameOnly == "FLinearColor")
                {
                    mathOps = R"FLCOPS(
	// === Math helpers (component-wise; colors, no Length / Distance) ===
	FLinearColor  operator+(const FLinearColor& o) const { return {R + o.R, G + o.G, B + o.B, A + o.A}; }
	FLinearColor  operator-(const FLinearColor& o) const { return {R - o.R, G - o.G, B - o.B, A - o.A}; }
	FLinearColor  operator*(const FLinearColor& o) const { return {R * o.R, G * o.G, B * o.B, A * o.A}; }
	FLinearColor  operator*(float s)               const { return {R * s, G * s, B * s, A * s}; }
	FLinearColor  operator/(float s)               const { return {R / s, G / s, B / s, A / s}; }
	FLinearColor& operator+=(const FLinearColor& o)      { R += o.R; G += o.G; B += o.B; A += o.A; return *this; }
	FLinearColor& operator-=(const FLinearColor& o)      { R -= o.R; G -= o.G; B -= o.B; A -= o.A; return *this; }
	FLinearColor& operator*=(float s)                    { R *= s; G *= s; B *= s; A *= s; return *this; }
	FLinearColor& operator/=(float s)                    { R /= s; G /= s; B /= s; A /= s; return *this; }
	bool          operator==(const FLinearColor& o) const{ return R == o.R && G == o.G && B == o.B && A == o.A; }
	bool          operator!=(const FLinearColor& o) const{ return !(*this == o); }
)FLCOPS";
                }

                if (mathOps)
                {
                    if (!s.ExtraDecls.empty())
                        s.ExtraDecls += "\n";
                    s.ExtraDecls += mathOps;
                }
            }
        }
    }

    // Drop packages that ended up with nothing to emit
    std::string _sdkPackagesUnsaved;
    {
        std::vector<UE_UPackage> kept;
        kept.reserve(_sdkProcessed.size());
        for (auto &p : _sdkProcessed)
        {
            if (!p.Classes.empty() || !p.Structures.empty() || !p.Enums.empty())
            {
                kept.push_back(std::move(p));
            }
            else
            {
                _sdkPackagesUnsaved += "\t";
                _sdkPackagesUnsaved += p.PackageName + ",\n";
            }
        }
        _sdkProcessed = std::move(kept);
    }

    if (_sdkProcessed.empty())
        return;

    // ---- Phase 2: build name -> package map for cross-package linking --------

    // _sdkNameToPkg is a class member (cleared at the top of DumpSDK).
    // Without the explicit `this->`, a re-declaration here would shadow it
    // and silently leave the real member empty, which broke per-pkg
    // cross-pkg #include emission downstream in DumpSDK_PerPackage.
    _sdkNameToPkg.reserve(_sdkProcessed.size() * 64);

    auto registerType = [&](const std::string &name, size_t pkgIdx)
    {
        if (name.empty()) return;
        _sdkNameToPkg.emplace(name, pkgIdx);
    };

    for (size_t i = 0; i < _sdkProcessed.size(); ++i)
    {
        for (const auto &s : _sdkProcessed[i].Structures) registerType(s.CppNameOnly, i);
        for (const auto &c : _sdkProcessed[i].Classes)    registerType(c.CppNameOnly, i);
        for (const auto &e : _sdkProcessed[i].Enums)
        {
            registerType(e.CppNameOnly, i);
            _sdkEnumUnderlying.emplace(e.CppNameOnly, e.UnderlyingType);
        }
    }

    // ---- Phase 3: build inter-package dependency graph -----------------------

    // Only "full" deps drive package ordering; pure forward-decl deps are
    // satisfied by the global forward-decl block emitted up front.
    std::vector<std::unordered_set<size_t>> pkgDeps(_sdkProcessed.size());

    auto recordDeps = [&](size_t pkgIdx, const UE_UPackage::Struct &s)
    {
        for (const auto &dep : s.FullDeps)
        {
            auto it = _sdkNameToPkg.find(dep);
            if (it == _sdkNameToPkg.end()) continue;
            if (it->second == pkgIdx) continue;
            pkgDeps[pkgIdx].insert(it->second);
        }
    };

    for (size_t i = 0; i < _sdkProcessed.size(); ++i)
    {
        for (const auto &s : _sdkProcessed[i].Structures) recordDeps(i, s);
        for (const auto &c : _sdkProcessed[i].Classes)    recordDeps(i, c);
    }

    // ---- Phase 4: topological sort packages ----------------------------------

    _sdkPkgOrder.reserve(_sdkProcessed.size());
    {
        std::vector<size_t> indices(_sdkProcessed.size());
        for (size_t i = 0; i < _sdkProcessed.size(); ++i) indices[i] = i;

        _sdkPkgOrder = TopoSort<size_t>(indices, [&](size_t i) -> const std::unordered_set<size_t> &
        {
            return pkgDeps[i];
        });
    }

    // ---- Compute phantom forward declarations -------------------------------
    //
    // Type names referenced in member / function signatures but not defined as
    // a dumped struct/enum (typically partially-resolved property kinds) end
    // up here. Forward decls keep pointer / template / signature usages
    // compilable.
    {
        static const std::unordered_set<std::string> kPreambleNames = {
            "FName", "FString", "FText",
            "FWeakObjectPtr", "FUniqueObjectGuid", "FSoftObjectPtr",
            "FScriptInterface", "FFieldPath", "FFieldClass", "FProperty",
            "FScriptDelegate", "FDelegate",
            "FMulticastInlineDelegate", "FMulticastDelegate", "FMulticastSparseDelegate",
            "TArray", "TMap", "TSet", "TPair",
            "TWeakObjectPtr", "TLazyObjectPtr",
            "TSoftObjectPtr", "TSoftClassPtr",
            "TSubclassOf", "TScriptInterface", "TFieldPath",
            "None", "FNone",
        };
        auto considerName = [&](const std::string &name)
        {
            if (name.empty()) return;
            if (kPreambleNames.count(name)) return;
            if (_sdkNameToPkg.count(name)) return;
            if (name.size() < 2 || !std::isupper(static_cast<unsigned char>(name[0])))
                return;
            const char prefix = name[0];
            if (prefix == 'E')
                _sdkPhantomEnums.insert(name);
            else
                _sdkPhantomStructs.insert(name);
        };
        for (const auto &p : _sdkProcessed)
        {
            auto walk = [&](const UE_UPackage::Struct &s)
            {
                for (const auto &n : s.FullDeps)    considerName(n);
                for (const auto &n : s.ForwardDeps) considerName(n);
            };
            for (const auto &s : p.Structures) walk(s);
            for (const auto &c : p.Classes)    walk(c);
        }
    }

    // ---- Intra-package topological sort on structs / classes ----------------
    //
    // Same-package value-type deps must be defined first inside the same
    // header. Run once here so all emit functions share the result.
    auto sortByDeps = [&](std::vector<UE_UPackage::Struct> &items)
    {
        std::unordered_map<std::string, size_t> nameToLocal;
        nameToLocal.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i)
            nameToLocal.emplace(items[i].CppNameOnly, i);

        std::vector<size_t> indices(items.size());
        for (size_t i = 0; i < items.size(); ++i) indices[i] = i;

        std::vector<std::vector<size_t>> deps(items.size());
        for (size_t i = 0; i < items.size(); ++i)
        {
            for (const auto &dep : items[i].FullDeps)
            {
                auto it = nameToLocal.find(dep);
                if (it != nameToLocal.end()) deps[i].push_back(it->second);
            }
        }

        auto sorted = TopoSort<size_t>(indices, [&](size_t i) -> const std::vector<size_t> &
        {
            return deps[i];
        });

        std::vector<UE_UPackage::Struct> reordered;
        reordered.reserve(items.size());
        for (size_t i : sorted) reordered.push_back(std::move(items[i]));
        items = std::move(reordered);
    };

    for (auto &p : _sdkProcessed)
    {
        if (!p.Structures.empty()) sortByDeps(p.Structures);
        if (!p.Classes.empty())    sortByDeps(p.Classes);
    }

    // ---- Collect script.json function entries -------------------------------
    //
    // Done here once so the various emit functions don't double-collect.
    static bool processInternal_once = false;
    for (size_t pkgIdx : _sdkPkgOrder)
    {
        const auto &pkg = _sdkProcessed[pkgIdx];
        for (const auto &cls : pkg.Classes)
        {
            for (const auto &func : cls.Functions)
            {
                if (!processInternal_once && (func.EFlags & FUNC_BlueprintEvent) && func.Func)
                {
                    dumper_jf_ns::jsonFunctions.push_back({"UObject", "ProcessInternal", func.Func});
                    processInternal_once = true;
                }
                if ((func.EFlags & FUNC_Native) && func.Func)
                {
                    std::string execFuncName = "exec";
                    execFuncName += func.Name;
                    dumper_jf_ns::jsonFunctions.push_back({cls.Name, execFuncName, func.Func});
                }
            }
        }
        for (const auto &st : pkg.Structures)
        {
            for (const auto &func : st.Functions)
            {
                if ((func.EFlags & FUNC_Native) && func.Func)
                {
                    std::string execFuncName = "exec";
                    execFuncName += func.Name;
                    dumper_jf_ns::jsonFunctions.push_back({st.Name, execFuncName, func.Func});
                }
            }
        }
    }
}

// ============================================================================
//  AIOCore helper block — emitted at the end of headers that contain
//  UObject's full definition. The augmenter (Phase 1.6) injected a matching
//  declaration on UObject; the inline body below dispatches through
//  vtable[ProcessEventIndex].
// ============================================================================
// emitTemplates: include StaticClassImpl<>/GetDefaultObjImpl<> definitions.
// Plan A and AIOHeader want them (the preamble only forward-declared the
// templates). Plan B sets this false — embedded UECore Basic.h ships its
// own StaticClassImpl<> chained through BasicFilesImpleUtils, so emitting
// ours would cause a duplicate definition.
static void EmitAIOCoreHelpersBlock(BufferFmt &buf, int processEventIndex,
                                    bool emitTemplates = true)
{
    buf.append("\n// === AIO Core Helpers ===\n");
    buf.append("// Generated runtime glue tying the dumped UE reflection layout to\n");
    buf.append("// per-game offsets discovered during the dump.\n");
    buf.append("//\n");
    buf.append("// Wire two things from your bridge once at startup:\n");
    buf.append("{}", "//   FName::s_NameResolver = [](int32_t idx){ return ResolveByID(idx); };\n");
    buf.append("//   UObject::GObjects.InitManually(GUObjectArrayPtr);\n");
    buf.append("// Everything else (ProcessEvent dispatch, FindObject walks, type\n");
    buf.append("// queries) is fully derived from the dumped layout + per-game\n");
    buf.append("// offsets — no other hooks needed.\n\n");
    buf.append("#ifndef AIOHeader_CORE_HELPERS_DEFINED\n");
    buf.append("#define AIOHeader_CORE_HELPERS_DEFINED\n\n");

    // ProcessEventIndex constant — only piece that needs interpolation.
    buf.append("namespace AIOCore\n{{\n");
    buf.append("    // Vtable slot of UObject::ProcessEvent in the target image.\n");
    buf.append("    // Discovered during dumping; override by #define-ing\n");
    buf.append("    // AIOCORE_PROCESS_EVENT_INDEX before #include if you need to\n");
    buf.append("    // swap it for a custom build.\n");
    buf.append("    #ifdef AIOCORE_PROCESS_EVENT_INDEX\n");
    buf.append("    constexpr int kProcessEventIndex = AIOCORE_PROCESS_EVENT_INDEX;\n");
    buf.append("    #else\n");
    buf.append("    constexpr int kProcessEventIndex = {};\n", processEventIndex);
    buf.append("    #endif\n");
    buf.append("}}\n\n");

    // Bodies are emitted via a single {} interpolation so we can keep the
    // raw-string verbatim — {{/}} aren't fmt-escapes here. References to
    // EClassCastFlags use brace-init (`EClassCastFlags{0x20}`) which is
    // valid against the forward-decl with fixed underlying type in the
    // preamble; for the actual flag enumerators include AIOMeta.hpp.
    buf.append("{}", R"AIOIMPL(// ---- ProcessEvent --------------------------------------------------
inline void UObject::ProcessEvent(struct UFunction* Function, void* Parms) const
{
    using FN = void(*)(const UObject*, struct UFunction*, void*);
    auto vtbl = *reinterpret_cast<void* const* const*>(this);
    reinterpret_cast<FN>(vtbl[AIOCore::kProcessEventIndex])(this, Function, Parms);
}

// ---- Name helpers --------------------------------------------------
inline std::string UObject::GetName() const
{
    return NamePrivate.ToString();
}

inline std::string UObject::GetFullName() const
{
    if (!ClassPrivate) return "None";
    std::string Outers;
    for (UObject* o = OuterPrivate; o; o = o->OuterPrivate)
        Outers = o->GetName() + "." + Outers;
    std::string r = ClassPrivate->GetName();
    r += " ";
    r += Outers;
    r += GetName();
    return r;
}

// ---- Type queries --------------------------------------------------
// HasTypeFlag / IsA(EClassCastFlags) read UClass::CastFlags — typed by
// Phase 1.6 augmenter from per-game UE_Offsets::UClass.CastFlags.
inline bool UObject::HasTypeFlag(EClassCastFlags TypeFlags) const
{
    if (!ClassPrivate) return false;
    auto bits = static_cast<uint64_t>(TypeFlags);
    if (bits == 0) return true; // EClassCastFlags::None
    return (static_cast<uint64_t>(ClassPrivate->CastFlags) & bits) != 0;
}

inline bool UObject::IsA(EClassCastFlags TypeFlags) const
{
    return HasTypeFlag(TypeFlags);
}

inline bool UObject::IsA(struct UClass* cmp) const
{
    if (!cmp || !ClassPrivate) return false;
    // Walk the SuperStruct chain of our class. UClass inherits from UStruct,
    // so we can compare UStruct* to UClass* directly via implicit upcast.
    for (const struct UStruct* s = ClassPrivate; s; s = s->SuperStruct)
    {
        if (s == cmp) return true;
    }
    return false;
}

inline bool UObject::IsDefaultObject() const
{
    // EObjectFlags::ClassDefaultObject = 0x10
    return (ObjectFlags & 0x10u) != 0;
}

inline void UObject::TraverseSupers(const std::function<bool(const UObject*)>& Callback) const
{
    // If `this` is itself a UClass (has Class type-flag), walk from it;
    // otherwise walk from our class's SuperStruct chain.
    const struct UStruct* Clss = nullptr;
    if (HasTypeFlag(EClassCastFlags{0x20})) // EClassCastFlags::Class
        Clss = static_cast<const struct UStruct*>(static_cast<const UClass*>(static_cast<const UObject*>(this)));
    else if (ClassPrivate)
        Clss = ClassPrivate;
    while (Clss)
    {
        if (!Callback(Clss)) break;
        Clss = Clss->SuperStruct;
    }
}

// ---- Object lookup --------------------------------------------------
// Walks GObjects directly — same structure as Dumper-7's UECore reference
// (CoreUObject_functions.cpp). Requires UObject::GObjects to be wired.
inline UObject* UObject::FindObjectImpl(const std::string& FullName, EClassCastFlags RequiredType)
{
    if (!GObjects) return nullptr;
    const int32_t N = GObjects->Num();
    for (int32_t i = 0; i < N; ++i)
    {
        UObject* Object = GObjects->GetByIndex(i);
        if (!Object || (reinterpret_cast<uintptr_t>(Object) & 0x7) != 0)
            continue;
        if (Object->HasTypeFlag(RequiredType) && Object->GetFullName() == FullName)
            return Object;
    }
    return nullptr;
}

inline UObject* UObject::FindObjectFastImpl(const std::string& Name, EClassCastFlags RequiredType)
{
    if (!GObjects) return nullptr;
    const int32_t N = GObjects->Num();
    for (int32_t i = 0; i < N; ++i)
    {
        UObject* Object = GObjects->GetByIndex(i);
        if (!Object) continue;
        if (Object->HasTypeFlag(RequiredType) && Object->GetName() == Name)
            return Object;
    }
    return nullptr;
}

inline UClass* UObject::FindClass(const std::string& ClassFullName)
{
    return FindObject<UClass>(ClassFullName, EClassCastFlags{0x20}); // ::Class
}

inline UClass* UObject::FindClassFast(const std::string& ClassName)
{
    return FindObjectFast<UClass>(ClassName, EClassCastFlags{0x20}); // ::Class
}

)AIOIMPL");

    if (emitTemplates)
    {
        buf.append("{}", R"AIOTPL(// ---- StaticClassImpl<> / GetDefaultObjImpl<> templates -------------
// Forward-declared in the preamble; full definitions here so the
// DEFINE_UE_CLASS_HELPERS macro instantiations resolve when user code
// pulls in this header. Caches the resolved UClass* per template
// instantiation (one static per (FullClassName, ClassNameStr) pair).
template<StringLiteral Name>
inline UClass* StaticClassImpl()
{
    static UClass* Cached = nullptr;
    if (!Cached)
    {
        Cached = UObject::FindObjectFast<UClass>(
            std::string(static_cast<const char*>(Name.Chars)),
            EClassCastFlags{0x20}); // ::Class
    }
    return Cached;
}

template<typename T>
inline T* GetDefaultObjImpl()
{
    UClass* C = T::StaticClass();
    return C ? reinterpret_cast<T*>(C->DefaultObject) : nullptr;
}

)AIOTPL");
    }

    buf.append("#endif // AIOHeader_CORE_HELPERS_DEFINED\n");
}

// ============================================================================
//  SDK CoreUObject_classes.hpp tail — minimal helper block.
//
//  AIOCore namespace + UObject::ProcessEvent inline body only. Other
//  helpers (GetName / IsA / FindObject / ...) live out-of-line in
//  CoreUObject_functions.cpp (UECore convention; user must compile and
//  link that .cpp). StaticClassImpl<> / GetDefaultObjImpl<> templates
//  are provided by embedded UECore Basic.h, not emitted here.
// ============================================================================
static void EmitSDKClassesHppHelpersTail(BufferFmt &buf, int processEventIndex)
{
    buf.append("\n// === AIO Core: ProcessEvent dispatch ===\n");
    buf.append("// Other UObject helper bodies (GetName / IsA / FindObject / ...)\n");
    buf.append("// live in CoreUObject_functions.cpp — link that .cpp into your TU.\n");
    buf.append("// FName::s_NameResolver and UObject::GObjects.InitManually() must\n");
    buf.append("// be wired from your bridge once at startup.\n\n");
    buf.append("#ifndef AIOHeader_CORE_HELPERS_DEFINED\n");
    buf.append("#define AIOHeader_CORE_HELPERS_DEFINED\n\n");

    buf.append("namespace AIOCore\n{{\n");
    buf.append("    #ifdef AIOCORE_PROCESS_EVENT_INDEX\n");
    buf.append("    constexpr int kProcessEventIndex = AIOCORE_PROCESS_EVENT_INDEX;\n");
    buf.append("    #else\n");
    buf.append("    constexpr int kProcessEventIndex = {};\n", processEventIndex);
    buf.append("    #endif\n");
    buf.append("}}\n\n");

    buf.append("{}", R"AIOPE(inline void UObject::ProcessEvent(struct UFunction* Function, void* Parms) const
{
    using FN = void(*)(const UObject*, struct UFunction*, void*);
    auto vtbl = *reinterpret_cast<void* const* const*>(this);
    reinterpret_cast<FN>(vtbl[AIOCore::kProcessEventIndex])(this, Function, Parms);
}

)AIOPE");

    buf.append("#endif // AIOHeader_CORE_HELPERS_DEFINED\n");
}

// ============================================================================
//  SDK CoreUObject_functions.cpp body — out-of-line UObject helpers.
//
//  Mirrors source/UEProber/UECore/CoreUObject_functions.cpp. Everything
//  here is non-inline so it compiles into the .cpp's TU only. Caller
//  emits the file's #include + namespace SDK { ... } shell around this.
// ============================================================================
static void EmitSDKFunctionsCppBodies(BufferFmt &buf)
{
    buf.append("{}", R"AIOIMPL(// Mirrors UECore CoreUObject_functions.cpp — bodies for UObject helpers
// declared in CoreUObject_classes.hpp.

// ---- Object lookup -------------------------------------------------
class UObject* UObject::FindObjectImpl(const std::string& FullName, EClassCastFlags RequiredType)
{
    if (!GObjects) return nullptr;
    const int32_t N = GObjects->Num();
    for (int32_t i = 0; i < N; ++i)
    {
        UObject* Object = GObjects->GetByIndex(i);
        if (!Object || (reinterpret_cast<uintptr_t>(Object) & 0x7) != 0)
            continue;
        if (Object->HasTypeFlag(RequiredType) && Object->GetFullName() == FullName)
            return Object;
    }
    return nullptr;
}

class UObject* UObject::FindObjectFastImpl(const std::string& Name, EClassCastFlags RequiredType)
{
    if (!GObjects) return nullptr;
    const int32_t N = GObjects->Num();
    for (int32_t i = 0; i < N; ++i)
    {
        UObject* Object = GObjects->GetByIndex(i);
        if (!Object) continue;
        if (Object->HasTypeFlag(RequiredType) && Object->GetName() == Name)
            return Object;
    }
    return nullptr;
}

class UClass* UObject::FindClass(const std::string& ClassFullName)
{
    return FindObject<UClass>(ClassFullName, EClassCastFlags{0x20}); // ::Class
}

class UClass* UObject::FindClassFast(const std::string& ClassName)
{
    return FindObjectFast<UClass>(ClassName, EClassCastFlags{0x20}); // ::Class
}

// ---- Name helpers --------------------------------------------------
std::string UObject::GetName() const
{
    return NamePrivate.ToString();
}

std::string UObject::GetFullName() const
{
    if (!ClassPrivate) return "None";
    std::string Outers;
    for (UObject* o = OuterPrivate; o; o = o->OuterPrivate)
        Outers = o->GetName() + "." + Outers;
    std::string r = ClassPrivate->GetName();
    r += " ";
    r += Outers;
    r += GetName();
    return r;
}

// ---- Type queries --------------------------------------------------
bool UObject::HasTypeFlag(EClassCastFlags TypeFlags) const
{
    if (!ClassPrivate) return false;
    auto bits = static_cast<uint64_t>(TypeFlags);
    if (bits == 0) return true; // EClassCastFlags::None
    return (static_cast<uint64_t>(ClassPrivate->CastFlags) & bits) != 0;
}

bool UObject::IsA(EClassCastFlags TypeFlags) const
{
    return HasTypeFlag(TypeFlags);
}

bool UObject::IsA(class UClass* cmp) const
{
    if (!cmp || !ClassPrivate) return false;
    for (const struct UStruct* s = ClassPrivate; s; s = s->SuperStruct)
    {
        if (s == cmp) return true;
    }
    return false;
}

bool UObject::IsDefaultObject() const
{
    // EObjectFlags::ClassDefaultObject = 0x10
    return (ObjectFlags & 0x10u) != 0;
}

void UObject::TraverseSupers(const std::function<bool(const UObject*)>& Callback) const
{
    const struct UStruct* Clss = nullptr;
    if (HasTypeFlag(EClassCastFlags{0x20})) // EClassCastFlags::Class
        Clss = static_cast<const struct UStruct*>(static_cast<const UClass*>(static_cast<const UObject*>(this)));
    else if (ClassPrivate)
        Clss = ClassPrivate;
    while (Clss)
    {
        if (!Callback(Clss)) break;
        Clss = Clss->SuperStruct;
    }
}

)AIOIMPL");
}

// ============================================================================
//  EmitUClassGetFunctionBody — body for UClass::GetFunction declared by
//  Phase 1.6 augmenter. Mirrors source/UEProber/UECore/CoreUObject_functions.cpp
//  (UClass::GetFunction) but uses our member naming (SuperStruct / Children /
//  Next) instead of UECore's (Super / Children / Next).
//
//  EClassCastFlags{0x80000} = ::Function (matches a UField that is a UFunction).
// ============================================================================
static void EmitUClassGetFunctionBody(BufferFmt &buf, bool emitInline)
{
    const char *kw = emitInline ? "inline " : "";
    buf.append(
        "// ---- UClass::GetFunction --------------------------------------------\n"
        "{}struct UFunction* UClass::GetFunction(const std::string& ClassName, "
        "const std::string& FuncName) const\n"
        "{{\n"
        "    for (const struct UStruct* Clss = this; Clss; Clss = Clss->SuperStruct)\n"
        "    {{\n"
        "        if (Clss->GetName() != ClassName) continue;\n"
        "        for (struct UField* Field = Clss->Children; Field; Field = Field->Next)\n"
        "        {{\n"
        "            if (Field->HasTypeFlag(EClassCastFlags{{0x80000}}) "
        "&& Field->GetName() == FuncName)\n"
        "                return static_cast<struct UFunction*>(Field);\n"
        "        }}\n"
        "    }}\n"
        "    return nullptr;\n"
        "}}\n\n", kw);
}

// ============================================================================
//  EmitUFunctionBody — emit the C++ body for one dumped UFunction. Body
//  shape mirrors Dumper-7's per-class functions: a static UFunction* cache,
//  an anonymous local Parms struct mirroring the UE param layout, fill-in
//  for in-params, ProcessEvent dispatch, copy-out for out-params, and
//  return of Parms.ReturnValue when applicable.
//
//  - non-static: dispatch via ClassPrivate->GetFunction(...) + this->ProcessEvent.
//  - static    : dispatch via StaticClass()->GetFunction(...) + GetDefaultObj()->ProcessEvent.
// ============================================================================
static void EmitUFunctionBody(BufferFmt &buf,
                              const UE_UPackage::Function &f,
                              bool emitInline,
                              const std::unordered_map<std::string, std::string> &enumUnderlying)
{
    const char *kw = emitInline ? "inline " : "";

    // Strip leading "static " from CppName when emitting the out-of-line
    // definition: `static` is only valid in the class body declaration, not
    // on the out-of-line definition. Same applies to inline AIOHeader bodies.
    std::string headOnly = f.CppName;
    const std::string staticPrefix = "static ";
    if (headOnly.compare(0, staticPrefix.size(), staticPrefix) == 0)
        headOnly = headOnly.substr(staticPrefix.size());

    // headOnly is now "<ReturnType> <FuncName>". Inject the qualifier so it
    // becomes "<ReturnType> <Owner>::<FuncName>".
    const auto spacePos = headOnly.rfind(' ');
    if (spacePos == std::string::npos)
        return; // malformed — skip
    std::string qualifiedHead = headOnly.substr(0, spacePos + 1)
                              + f.OwnerCppName + "::"
                              + headOnly.substr(spacePos + 1);

    const bool hasReturn = (f.ReturnType != "void");

    // For enum types, prefix with `enum` (NOT `enum class`) when used as
    // an elaborated type specifier in the local Parms struct or a cast.
    // C++ accepts `enum X` to refer to either a scoped or unscoped
    // enumeration; `enum class X` is only valid in the declaration itself
    // and clang rejects it as a use-site specifier with
    // -Welaborated-enum-class. The elaborated form bypasses ordinary
    // unqualified lookup, which otherwise hits the (shadowing) parameter
    // name when UE produces things like `EaseType EaseType`.
    auto qualifyType = [&](const std::string &t) -> std::string {
        return enumUnderlying.count(t) ? "enum " + t : t;
    };

    buf.append("{}{}({})\n{{\n", kw, qualifiedHead, f.Params);

    // Function pointer cache + lookup. Static dispatch resolves through
    // StaticClass() (cached per type via DEFINE_UE_CLASS_HELPERS); the
    // instance dispatch reads ClassPrivate, which the Phase 1.6 augmenter
    // typed as `struct UClass*`.
    buf.append("    static struct UFunction* Func = nullptr;\n");
    if (f.IsStatic)
    {
        buf.append("    if (!Func) Func = StaticClass()->GetFunction(\"{}\", \"{}\");\n",
                   f.OwnerUEName, f.Name);
    }
    else
    {
        buf.append("    if (!Func) Func = ClassPrivate->GetFunction(\"{}\", \"{}\");\n",
                   f.OwnerUEName, f.Name);
    }

    // Anonymous local Parms struct mirroring UE's param layout. Field order
    // matches UE's reflection iteration (the order params were pushed into
    // ParamsList by GenerateFunction). The trailing ReturnValue (if any) is
    // emitted last — UE places it at the end of the param block.
    buf.append("    struct\n    {{\n");
    for (const auto &p : f.ParamsList)
    {
        const std::string fieldType = qualifyType(p.Type);
        if (p.ArrayDim > 1)
            buf.append("        {} {}[0x{:X}];\n", fieldType, p.Name, p.ArrayDim);
        else
            buf.append("        {} {};\n", fieldType, p.Name);
    }
    if (hasReturn)
        buf.append("        {} ReturnValue;\n", qualifyType(f.ReturnType));
    buf.append("    }} Parms{{}};\n");

    // Fill in-params. ConstParm + OutParm degenerates to "const Type&" in
    // signature but is treated as an in-param at marshalling time. Pure
    // out-params are *not* filled (callers don't pass anything in).
    //
    // The C-style cast on the assignment is deliberate: `const ConstParm`
    // refs would otherwise refuse to bind to the non-const Parms field
    // (`assigning to T* from const T* discards qualifiers`). For non-const
    // params the cast is a no-op the compiler folds away.
    for (const auto &p : f.ParamsList)
    {
        const bool isOut       = (p.Flags & CPF_OutParm) != 0;
        const bool isConstOut  = isOut && (p.Flags & CPF_ConstParm) != 0;
        const bool fillFromArg = !isOut || isConstOut;
        if (!fillFromArg) continue;

        if (p.ArrayDim > 1)
            buf.append("    memcpy(Parms.{}, {}, sizeof({}) * 0x{:X});\n",
                       p.Name, p.Name, p.Type, p.ArrayDim);
        else
            buf.append("    Parms.{} = ({}){};\n", p.Name, qualifyType(p.Type), p.Name);
    }

    // Dispatch. Static path goes through GetDefaultObj() — DEFINE_UE_CLASS_HELPERS
    // emits this static accessor on every dumped class. Non-static path is
    // a direct member call (ProcessEvent inherited from UObject).
    if (f.IsStatic)
        buf.append("    GetDefaultObj()->ProcessEvent(Func, &Parms);\n");
    else
        buf.append("    this->ProcessEvent(Func, &Parms);\n");

    // Copy out: pure out-params (without ConstParm) get written back from
    // Parms into the caller's reference.
    for (const auto &p : f.ParamsList)
    {
        const bool isOut      = (p.Flags & CPF_OutParm) != 0;
        const bool isConstOut = isOut && (p.Flags & CPF_ConstParm) != 0;
        if (!isOut || isConstOut) continue;

        if (p.ArrayDim > 1)
            buf.append("    memcpy({}, Parms.{}, sizeof({}) * 0x{:X});\n",
                       p.Name, p.Name, p.Type, p.ArrayDim);
        else
            buf.append("    {} = Parms.{};\n", p.Name, p.Name);
    }

    if (hasReturn)
        buf.append("    return Parms.ReturnValue;\n");

    buf.append("}}\n\n");
}

// ============================================================================
//  EmitPackageFunctionBodies — emit ProcessEvent dispatch bodies for every
//  UFunction across every Class in `pkg`. UScriptStructs are skipped — they
//  have no associated UFunction children.
// ============================================================================
static void EmitPackageFunctionBodies(BufferFmt &buf,
                                      const UE_UPackage &pkg,
                                      bool emitInline,
                                      const std::unordered_map<std::string, std::string> &enumUnderlying)
{
    for (const auto &c : pkg.Classes)
    {
        if (c.Functions.empty()) continue;
        buf.append("// ---- {}::* ----\n", c.CppNameOnly);
        for (const auto &f : c.Functions)
            EmitUFunctionBody(buf, f, emitInline, enumUnderlying);
    }
}

// Strip a UTF-8 BOM (EF BB BF) embedded at the start of an embedded source.
// The UECore raw-string embeds reproduce their source files byte-for-byte;
// `source/UEProber/UECore/UnrealContainers.h` ships with a BOM, which makes
// clang reject the dumped output ("unexpected character <U+FEFF>"). The
// embedded raw string starts with a newline (the opening `R"UECoreUC(` is
// on its own line), so the BOM lands at offset 1. Handle both shapes.
static std::string StripUtf8Bom(std::string content)
{
    constexpr const char *kBom = "\xef\xbb\xbf";
    if (content.compare(0, 3, kBom) == 0)
    {
        content.erase(0, 3);
    }
    else if (content.size() >= 4 && content[0] == '\n'
             && content.compare(1, 3, kBom) == 0)
    {
        content.erase(1, 3);
    }
    return content;
}

// Substitute the embedded `#define bWITH_CASE_PRESERVING_NAME false` with
// `... true` when the per-game profile flags case-preserving FName. The
// switch flips ComparisonIndex/DisplayIndex from a 4-byte union into two
// separate 4-byte fields, growing FName from 8 to 12 bytes — matches the
// per-game UE_Offsets::FName.Size that drives Phase 1.6's NamePrivate slot.
static std::string ApplyCasePreservingDefine(std::string content, bool casePreserving)
{
    if (!casePreserving) return content;
    const std::string from = "#define bWITH_CASE_PRESERVING_NAME false";
    const std::string to   = "#define bWITH_CASE_PRESERVING_NAME true";
    auto pos = content.find(from);
    if (pos != std::string::npos)
        content.replace(pos, from.size(), to);
    return content;
}

// Re-target Basic.cpp's `#include "CoreUObject_classes.h"` to the .hpp
// extension the dumper actually emits. Source side keeps the .h spelling
// because it neighbours the prober runtime header at
// source/UEProber/UECore/CoreUObject_classes.h, but the SDK output only
// has CoreUObject_classes.hpp.
static std::string RetargetBasicCppIncludes(std::string content)
{
    const std::string from = "#include \"CoreUObject_classes.h\"";
    const std::string to   = "#include \"CoreUObject_classes.hpp\"";
    auto pos = content.find(from);
    if (pos != std::string::npos)
        content.replace(pos, from.size(), to);
    return content;
}

// ============================================================================
//  EmitSDKCoreFiles — shared core-file emit for SDK_A and SDK_B.
//
//  Lays down at <prefix>:
//    Basic.h                         (UECore embed verbatim)
//    Basic.cpp                       (UECore embed, member names patched)
//    UnrealContainers.h              (UECore embed verbatim)
//    CoreUObject_structs.hpp         (enums + ScriptStructs in namespace SDK)
//    CoreUObject_classes.hpp         (Classes + AIOCore + ProcessEvent inline)
//    CoreUObject_functions.cpp       (UObject + UClass helpers + UFunction bodies)
//
//  Caller is responsible for the SDK.hpp aggregator (they differ between
//  SDK_A and SDK_B) and any per-package files beyond CoreUObject.
// ============================================================================
static void EmitSDKCoreFiles(
    const std::string& prefix,
    const UE_UPackage& corePkg,
    int processEventIndex,
    bool casePreserving,
    const std::unordered_map<std::string, std::string>& enumUnderlying,
    std::unordered_map<std::string, BufferFmt>& outBuffersMap)
{
    // ---- 1. UECore companions (verbatim embed) -------------------------
    // Basic.h's bWITH_CASE_PRESERVING_NAME #define is patched per-profile
    // so the embedded FName layout (8B union vs 12B separate fields)
    // matches the per-game UE_Offsets::FName.Size.
    //
    // StripUtf8Bom is applied to every embed: the source UECore files may
    // ship with a UTF-8 BOM (currently UnrealContainers.h does), which
    // clang rejects as "unexpected character <U+FEFF>" when the dumped
    // output is included in a TU.
    outBuffersMap[prefix + "Basic.h"].append("{}",
        StripUtf8Bom(ApplyCasePreservingDefine(kUECoreBasicH, casePreserving)));
    outBuffersMap[prefix + "Basic.cpp"].append("{}",
        RetargetBasicCppIncludes(StripUtf8Bom(kUECoreBasicCpp)));
    outBuffersMap[prefix + "UnrealContainers.h"].append("{}", StripUtf8Bom(kUECoreUnrealContainersH));

    // utfcpp dependency: UnrealContainers.h's FString::ToString uses
    // utf8::unchecked::utf16to8 on GCC/Clang. Drop the two-file utfcpp
    // subset (unchecked.h + its core.h) under <prefix>utfcpp/ so the
    // SDK directory is fully self-contained.
    outBuffersMap[prefix + "utfcpp/core.h"].append("{}", StripUtf8Bom(kUtfcppCoreH));
    outBuffersMap[prefix + "utfcpp/unchecked.h"].append("{}", StripUtf8Bom(kUtfcppUncheckedH));

    // ---- 2. CoreUObject_structs.hpp ------------------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_structs.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"Basic.h\"\n");
        buf.append("#include \"UnrealContainers.h\"\n");
        // <cmath> for the math-helper ops (Length / Dot / Normalize ...)
        // injected on FVector / FVector2D / FRotator / etc.
        buf.append("#include <cmath>\n\n");

        // DEFINE_UE_CLASS_HELPERS macro — used by every dumped Class via
        // Phase 1.6b. Macros are global; emitting once here covers both
        // _structs.hpp and _classes.hpp (which #includes structs.hpp) plus
        // every non-core Packages/<pkg>.hpp that transitively pulls structs.
        buf.append("{}", R"AIOMACRO(#ifndef DEFINE_UE_CLASS_HELPERS
#define DEFINE_UE_CLASS_HELPERS(FullClassName, ClassNameStr) \
    static struct UClass* StaticClass() { return StaticClassImpl<ClassNameStr>(); } \
    static struct FullClassName* GetDefaultObj() { return GetDefaultObjImpl<FullClassName>(); }
#endif

)AIOMACRO");

        buf.append("namespace SDK\n{{\n\n");
        buf.append("// Package: CoreUObject - Enums({}) + Structs({})\n\n",
                   corePkg.Enums.size(), corePkg.Structures.size());
        if (!corePkg.Enums.empty())
            UE_UPackage::AppendEnumsToBuffer(const_cast<std::vector<UE_UPackage::Enum>&>(corePkg.Enums), &buf);
        if (!corePkg.Structures.empty())
            UE_UPackage::AppendStructsToBuffer(const_cast<std::vector<UE_UPackage::Struct>&>(corePkg.Structures), &buf);
        buf.append("}} // namespace SDK\n");
    }

    // ---- 3. CoreUObject_classes.hpp ------------------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_classes.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"CoreUObject_structs.hpp\"\n\n");
        buf.append("namespace SDK\n{{\n\n");
        buf.append("// Package: CoreUObject - Classes({})\n\n", corePkg.Classes.size());

        if (!corePkg.Classes.empty())
            UE_UPackage::AppendStructsToBuffer(const_cast<std::vector<UE_UPackage::Struct>&>(corePkg.Classes), &buf);

        // AIOCore namespace + ProcessEvent inline body. Other helper bodies
        // (GetName / IsA / FindObject / ...) are out-of-line in
        // CoreUObject_functions.cpp (UECore convention).
        EmitSDKClassesHppHelpersTail(buf, processEventIndex);

        buf.append("\n}} // namespace SDK\n");
    }

    // ---- 4. CoreUObject_functions.cpp (UObject + UClass helpers + bodies) -
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_functions.cpp"];
        buf.append("// CoreUObject helper bodies. Mirrors\n");
        buf.append("// source/UEProber/UECore/CoreUObject_functions.cpp plus the\n");
        buf.append("// ProcessEvent dispatch body for every UFunction in CoreUObject.\n");
        buf.append("//\n");
        buf.append("// Compile and link this .cpp into your TU(s). The bodies depend\n");
        buf.append("// on FName::s_NameResolver and UObject::GObjects being wired\n");
        buf.append("// from your bridge once at startup.\n\n");
        buf.append("#include \"Basic.h\"\n");
        buf.append("#include \"CoreUObject_classes.hpp\"\n");
        buf.append("#include <cstring> // memcpy for ArrayDim>1 param marshalling\n\n");
        buf.append("namespace SDK\n{{\n\n");
        EmitSDKFunctionsCppBodies(buf);
        EmitUClassGetFunctionBody(buf, /*emitInline=*/false);
        EmitPackageFunctionBodies(buf, corePkg, /*emitInline=*/false, enumUnderlying);
        buf.append("}} // namespace SDK\n");
    }
}

// ============================================================================
//  DumpAIOHeader — single monolithic header containing every dumped type.
// ============================================================================
void UEDumper::DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt)
{
    const bool casePreserving =
        _profile && _profile->GetUEVars() && _profile->GetUEVars()->GetOffsets()
            ? _profile->GetUEVars()->GetOffsets()->Config.isUsingCasePreservingName
            : false;

    if (_sdkProcessed.empty())
    {
        aioBufferFmt.append("#pragma once\n\n");
        aioBufferFmt.append("#include <cstdint>\n#include <string>\n#include <functional>\n#include <cmath>\n\n");
        aioBufferFmt.append("{}\n", ApplyCasePreservingDefine(kAIOPreamble, casePreserving));
        logsBufferFmt.append("Saved packages: 0\nSaved classes: 0\nSaved structs: 0\nSaved enums: 0\n");
        if (!_sdkPackagesUnsaved.empty())
        {
            std::string un = _sdkPackagesUnsaved;
            un.erase(un.size() - 2);
            logsBufferFmt.append("Unsaved packages: [\n{}\n]\n", un);
        }
        logsBufferFmt.append("==========================\n");
        return;
    }

    aioBufferFmt.append("#pragma once\n\n");
    aioBufferFmt.append("#include <cstdint>\n#include <string>\n#include <functional>\n#include <cmath>\n\n");
    aioBufferFmt.append("{}\n", ApplyCasePreservingDefine(kAIOPreamble, casePreserving));

    aioBufferFmt.append("// === Forward declarations ===\n\n");
    for (const auto &p : _sdkProcessed)
    {
        for (const auto &e : p.Enums)
            aioBufferFmt.append("enum class {} : {};\n", e.CppNameOnly, e.UnderlyingType);
    }
    aioBufferFmt.append("\n");
    for (const auto &p : _sdkProcessed)
    {
        for (const auto &s : p.Structures) aioBufferFmt.append("struct {};\n", s.CppNameOnly);
        for (const auto &c : p.Classes)    aioBufferFmt.append("struct {};\n", c.CppNameOnly);
    }
    aioBufferFmt.append("\n");

    if (!_sdkPhantomEnums.empty() || !_sdkPhantomStructs.empty())
    {
        aioBufferFmt.append("// === Phantom forward declarations ===\n");
        aioBufferFmt.append("// Type names referenced in member / function signatures but not\n");
        aioBufferFmt.append("// defined as a dumped struct/enum (typically partially-resolved\n");
        aioBufferFmt.append("// property kinds). Forward decls keep pointer / template /\n");
        aioBufferFmt.append("// signature usages compilable.\n");
        for (const auto &n : _sdkPhantomEnums)
            aioBufferFmt.append("enum class {} : uint8_t;\n", n);
        for (const auto &n : _sdkPhantomStructs)
            aioBufferFmt.append("struct {};\n", n);
        aioBufferFmt.append("\n");
    }

    int packages_saved = 0;
    int classes_saved = 0;
    int structs_saved = 0;
    int enums_saved = 0;

    for (size_t pkgIdx : _sdkPkgOrder)
    {
        auto &pkg = _sdkProcessed[pkgIdx];

        aioBufferFmt.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                            pkg.PackageName, pkg.Enums.size(), pkg.Structures.size(), pkg.Classes.size());

        if (!pkg.Enums.empty())
            UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &aioBufferFmt);

        if (!pkg.Structures.empty())
            UE_UPackage::AppendStructsToBuffer(pkg.Structures, &aioBufferFmt);

        if (!pkg.Classes.empty())
            UE_UPackage::AppendStructsToBuffer(pkg.Classes, &aioBufferFmt);

        packages_saved++;
        classes_saved += pkg.Classes.size();
        structs_saved += pkg.Structures.size();
        enums_saved   += pkg.Enums.size();
    }

    EmitAIOCoreHelpersBlock(aioBufferFmt, _processEventIndex);

    // === UFunction ProcessEvent bodies (header-only inline) =============
    // Order matters: CoreUObject's UClass::GetFunction body comes first
    // because every other dumped UFunction's body lookup goes through it.
    // Wrapped in its own include guard so a TU that ends up pulling this
    // header twice doesn't re-define everything.
    aioBufferFmt.append("\n#ifndef AIOHeader_FUNCTION_BODIES_DEFINED\n");
    aioBufferFmt.append("#define AIOHeader_FUNCTION_BODIES_DEFINED\n");
    aioBufferFmt.append("// memcpy for ArrayDim>1 param marshalling.\n");
    aioBufferFmt.append("#include <cstring>\n\n");
    EmitUClassGetFunctionBody(aioBufferFmt, /*emitInline=*/true);
    for (size_t pkgIdx : _sdkPkgOrder)
        EmitPackageFunctionBodies(aioBufferFmt, _sdkProcessed[pkgIdx], /*emitInline=*/true, _sdkEnumUnderlying);
    aioBufferFmt.append("#endif // AIOHeader_FUNCTION_BODIES_DEFINED\n");

    logsBufferFmt.append("Saved packages: {}\nSaved classes: {}\nSaved structs: {}\nSaved enums: {}\n",
                         packages_saved, classes_saved, structs_saved, enums_saved);

    if (!_sdkPackagesUnsaved.empty())
    {
        std::string un = _sdkPackagesUnsaved;
        un.erase(un.size() - 2);
        logsBufferFmt.append("Unsaved packages: [\n{}\n]\n", un);
    }

    logsBufferFmt.append("==========================\n");
}

// ============================================================================
//  DumpSDK_PerPackage (Plan A) — full Dumper-7-style output.
//
//  Layout:
//    SDK_A/
//    ├── Basic.h                         (UECore embed)
//    ├── Basic.cpp                       (UECore embed, member-name patched)
//    ├── UnrealContainers.h              (UECore embed)
//    ├── CoreUObject_classes.hpp         (UECore-style 4-file split for core)
//    ├── CoreUObject_structs.hpp
//    ├── CoreUObject_parameters.hpp
//    ├── CoreUObject_functions.cpp
//    ├── SDK.hpp                         (aggregator)
//    └── Packages/
//        ├── Engine.hpp                  (one .hpp per non-CoreUObject pkg)
//        ├── GameplayAbilities.hpp
//        └── ...
//
//  CoreUObject is the only pkg that gets the UECore 4-file split (it's the
//  reflection foundation everything else builds on). Other pkgs stay as a
//  single self-contained .hpp under Packages/. SDK_B is the same minus the
//  Packages/ subdirectory.
// ============================================================================
void UEDumper::DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap)
{
    if (_sdkProcessed.empty())
    {
        logsBufferFmt.append("SDK Plan A: empty processed packages, skipping.\n");
        return;
    }

    // Locate CoreUObject — it's the foundation, everything else depends on it.
    const size_t kNotFound = static_cast<size_t>(-1);
    size_t coreIdx = kNotFound;
    for (size_t i = 0; i < _sdkProcessed.size(); ++i)
    {
        if (_sdkProcessed[i].PackageName == "CoreUObject")
        {
            coreIdx = i;
            break;
        }
    }
    if (coreIdx == kNotFound)
    {
        logsBufferFmt.append("SDK Plan A: CoreUObject package not found in dump, skipping.\n");
        return;
    }

    const std::string prefix = "SDK_A/";
    const std::string pkgPrefix = prefix + "Packages/";

    // ---- 1. Root files (UECore companions + CoreUObject 4-file split) -----
    const bool casePreserving =
        _profile && _profile->GetUEVars() && _profile->GetUEVars()->GetOffsets()
            ? _profile->GetUEVars()->GetOffsets()->Config.isUsingCasePreservingName
            : false;
    EmitSDKCoreFiles(prefix, _sdkProcessed[coreIdx], _processEventIndex, casePreserving, _sdkEnumUnderlying, outBuffersMap);

    // ---- 2. Per-package: Packages/<pkg>.hpp for every non-CoreUObject pkg ---
    size_t nonCorePkgCount = 0;
    for (size_t pkgIdx : _sdkPkgOrder)
    {
        if (pkgIdx == coreIdx) continue;
        auto &pkg = _sdkProcessed[pkgIdx];
        const std::string fname = pkgPrefix + pkg.PackageName + ".hpp";
        auto &buf = outBuffersMap[fname];

        buf.append("#pragma once\n\n");
        // CoreUObject_classes.hpp transitively pulls _structs.hpp + Basic.h +
        // UnrealContainers.h, giving us UObject + every dumped CoreUObject type.
        buf.append("#include \"../CoreUObject_classes.hpp\"\n");

        // Cross-pkg dep emission strategy:
        //   FullDeps (struct/class) -> #include source pkg's .hpp. FullDeps
        //                              form a DAG on struct/class types
        //                              (Phase 3 tops-sorts on them).
        //   FullDeps (enum)         -> `enum class EFoo : ut;` forward decl.
        //                              Always — even when usage is by-value,
        //                              an opaque-enum-declaration with the
        //                              underlying type is enough to size a
        //                              member field. Enum FullDeps cycle in
        //                              practice (e.g. GPGameInput's enum
        //                              referenced from GPGlobalDefines and
        //                              vice versa), so we MUST avoid the
        //                              #include path for enums.
        //   ForwardDeps (struct)    -> `struct EFoo;` forward decl. Same
        //                              cycle-avoidance reason as enum
        //                              FullDeps (UE Engine <-> UMG etc).
        //   ForwardDeps (enum)      -> `enum class EFoo : ut;` forward decl.
        std::set<std::string> depPkgs;
        std::set<std::string> crossPkgFwdDecls;
        auto recordFwdDecl = [&](const std::string &dep, size_t depPkgIdx) {
            if (depPkgIdx == pkgIdx) return;            // same-pkg fwd block
            if (depPkgIdx == coreIdx) return;            // pulled via CoreUObject_classes
            crossPkgFwdDecls.insert(dep);
        };
        auto collect = [&](const UE_UPackage::Struct &s) {
            for (const auto &dep : s.FullDeps)
            {
                auto it = _sdkNameToPkg.find(dep);
                if (it == _sdkNameToPkg.end()) continue;
                if (_sdkEnumUnderlying.count(dep))
                {
                    recordFwdDecl(dep, it->second);
                    continue;
                }
                if (it->second == pkgIdx) continue;
                if (it->second == coreIdx) continue;
                depPkgs.insert(_sdkProcessed[it->second].PackageName);
            }
            for (const auto &dep : s.ForwardDeps)
            {
                auto it = _sdkNameToPkg.find(dep);
                if (it == _sdkNameToPkg.end()) continue;
                recordFwdDecl(dep, it->second);
            }
        };
        for (const auto &s : pkg.Structures) collect(s);
        for (const auto &c : pkg.Classes)    collect(c);
        for (const auto &dp : depPkgs)
            buf.append("#include \"{}.hpp\"\n", dp);
        buf.append("\n");

        buf.append("namespace SDK\n{{\n\n");
        buf.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                   pkg.PackageName, pkg.Enums.size(), pkg.Structures.size(), pkg.Classes.size());

        // Forward decl block. Covers:
        //   (a) Same-package struct/class — within-file refs through
        //       pointer/handle wrappers across topo-sorted definition order.
        //   (b) Cross-package struct/class pointer-only refs — avoids
        //       #include cycles.
        //   (c) Cross-package enum refs (any usage) — emitted with their
        //       underlying type so the consumer can use them as sized
        //       member fields without #including the defining pkg.
        if (!pkg.Structures.empty() || !pkg.Classes.empty() || !crossPkgFwdDecls.empty())
        {
            for (const auto &n : crossPkgFwdDecls)
            {
                auto eit = _sdkEnumUnderlying.find(n);
                if (eit != _sdkEnumUnderlying.end())
                    buf.append("enum class {} : {};\n", n, eit->second);
                else
                    buf.append("struct {};\n", n);
            }
            for (const auto &s : pkg.Structures)
                buf.append("struct {};\n", s.CppNameOnly);
            for (const auto &c : pkg.Classes)
                buf.append("struct {};\n", c.CppNameOnly);
            buf.append("\n");
        }

        if (!pkg.Enums.empty())      UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &buf);
        if (!pkg.Structures.empty()) UE_UPackage::AppendStructsToBuffer(pkg.Structures, &buf);
        if (!pkg.Classes.empty())    UE_UPackage::AppendStructsToBuffer(pkg.Classes, &buf);

        buf.append("}} // namespace SDK\n");
        ++nonCorePkgCount;

        // ---- 2b. <pkg>_functions.cpp (out-of-line bodies, link-time) -------
        // Bodies need *complete* types (Parms.X member access, memcpy with
        // sizeof). The sibling <pkg>.hpp covers same-package + struct-level
        // FullDeps; we additionally collect every other non-core pkg whose
        // type appears in any function signature, since param types may be
        // forward-decl-only at <pkg>.hpp level.
        //
        // Emitting bodies as non-inline definitions in a separate .cpp keeps
        // SDK.hpp itself a pure declaration aggregator. Each consuming TU
        // pays only for the headers it actually uses; the function bodies
        // compile once when the user adds Packages/*.cpp + CoreUObject_*.cpp
        // + Basic.cpp to their build (e.g. via `file(GLOB_RECURSE ... *.cpp)`
        // in CMake, see misc/sdk_smoke/CMakeLists.txt).
        const std::string fnFname = pkgPrefix + pkg.PackageName + "_functions.cpp";
        auto &fbuf = outBuffersMap[fnFname];
        fbuf.append("#include \"{}.hpp\"\n", pkg.PackageName);

        std::set<std::string> fnDepPkgs;
        auto collectFnDeps = [&](const UE_UPackage::Struct &s) {
            for (const auto &f : s.Functions)
            {
                std::set<std::string> full, fwd;
                UE_UPackage::ExtractTypeDeps(f.CppName, full, fwd);
                UE_UPackage::ExtractTypeDeps(f.Params,  full, fwd);
                for (const auto &set : { full, fwd })
                {
                    for (const auto &dep : set)
                    {
                        auto it = _sdkNameToPkg.find(dep);
                        if (it == _sdkNameToPkg.end()) continue;
                        if (it->second == pkgIdx) continue;
                        if (it->second == coreIdx) continue;
                        fnDepPkgs.insert(_sdkProcessed[it->second].PackageName);
                    }
                }
            }
        };
        for (const auto &c : pkg.Classes) collectFnDeps(c);
        for (const auto &dp : fnDepPkgs)
            fbuf.append("#include \"{}.hpp\"\n", dp);
        fbuf.append("#include <cstring> // memcpy for ArrayDim>1 param marshalling\n\n");
        fbuf.append("namespace SDK\n{{\n\n");
        EmitPackageFunctionBodies(fbuf, pkg, /*emitInline=*/false, _sdkEnumUnderlying);
        fbuf.append("}} // namespace SDK\n");
    }

    // ---- 3. SDK.hpp aggregator -------------------------------------------
    //
    // Pure declaration aggregator: every Packages/*.hpp transitively. Function
    // bodies live in Packages/*_functions.cpp and are compiled into the user's
    // build separately (recommended: `file(GLOB_RECURSE SDK_SRCS SDK_A/*.cpp)`
    // in CMake — see misc/sdk_smoke/CMakeLists.txt). Avoids the per-TU re-
    // instantiation cost that header-only inline bodies caused (a 56-second
    // -fsyntax-only on a single Demo.cpp before this change).
    {
        auto &sdkBuf = outBuffersMap[prefix + "SDK.hpp"];
        sdkBuf.append("#pragma once\n\n");
        sdkBuf.append("// Declaration-only aggregator: CoreUObject_classes.hpp + every\n");
        sdkBuf.append("// Packages/*.hpp. UFunction bodies live in Packages/*_functions.cpp\n");
        sdkBuf.append("// (out-of-line) and CoreUObject_functions.cpp + Basic.cpp must be\n");
        sdkBuf.append("// linked into your build. With CMake, glob the *.cpp files under\n");
        sdkBuf.append("// the SDK_A directory and add them to your target's sources — see\n");
        sdkBuf.append("// misc/sdk_smoke/CMakeLists.txt in the AndUEProber repo for an\n");
        sdkBuf.append("// example.\n\n");
        sdkBuf.append("#include \"CoreUObject_classes.hpp\"\n");
        for (size_t pkgIdx : _sdkPkgOrder)
        {
            if (pkgIdx == coreIdx) continue;
            const auto &pkg = _sdkProcessed[pkgIdx];
            sdkBuf.append("#include \"Packages/{}.hpp\"\n", pkg.PackageName);
        }
    }

    logsBufferFmt.append("SDK Plan A: emitted UECore companions + CoreUObject core files + {}Packages/ ({} pkgs x 2 files) + SDK.hpp\n",
                         prefix, nonCorePkgCount);
    logsBufferFmt.append("==========================\n");
}

