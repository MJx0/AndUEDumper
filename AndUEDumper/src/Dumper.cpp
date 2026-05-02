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

    if (_sdkMode == SDKMode::Both || _sdkMode == SDKMode::OnlyA)
        DumpSDK_PerPackage(logsBufferFmt, *outBuffersMap);

    if (_sdkMode == SDKMode::Both || _sdkMode == SDKMode::OnlyB)
        DumpSDK_UECoreStyle(logsBufferFmt, *outBuffersMap);

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

struct FName
{
    int32_t ComparisonIndex;
    uint32_t Number;
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

// Runtime hooks consumed by the AIOCore inline helpers. Wire these from
// your bridge code once at startup. Function pointers (not std::function)
// keep the generated header self-contained.
namespace AIOCore
{
    // Resolve an FName ComparisonIndex to its ANSI string. Used by
    // UObject::GetName / GetFullName.
    using NameResolverFn = const char* (*)(int32_t comparisonIndex);
    inline NameResolverFn g_NameResolver = nullptr;

    // Combined object lookup. `isFullName=false` means short-name lookup
    // (FindObjectFast / FindClassFast / StaticClassImpl path);
    // `isFullName=true` matches against `Class Outer.Object` paths
    // (FindObject / FindClass). `castFlagsBits` is the EClassCastFlags
    // value-as-uint64; pass 0 to disable type filtering.
    using FindObjectFn = struct UObject* (*)(const char* nameOrFullName,
                                             bool isFullName,
                                             uint64_t castFlagsBits);
    inline FindObjectFn g_FindObject = nullptr;
}

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
                // remaining helpers walk the typed members added above
                // (ClassPrivate / OuterPrivate / NamePrivate / ObjectFlags)
                // plus UStruct::SuperStruct (added when UStruct is augmented
                // in this same pass) and UClass::{CastFlags,DefaultObject}.
                // Lookups (FindObject*, FindClass*, GetName) route through
                // the user-wired AIOCore::g_* hooks declared in kAIOPreamble.
                //
                // EClassCastFlags is forward-declared in kAIOPreamble with
                // an explicit uint64_t underlying type — that's enough for
                // function signatures and `EClassCastFlags{}` default args.
                // Include AIOMeta.hpp alongside this header to get the
                // actual flag enumerators (e.g. EClassCastFlags::Actor).
                s.ExtraDecls =
                    "\t// === AIO Core helpers (inline bodies at end of header) ===\n"
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

    std::unordered_map<std::string, size_t> _sdkNameToPkg;
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
        for (const auto &e : _sdkProcessed[i].Enums)      registerType(e.CppNameOnly, i);
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
    buf.append("// per-game offsets discovered during the dump. The ProcessEvent\n");
    buf.append("// vtable slot baked in here lets `Obj->ProcessEvent(Func, &Parms)`\n");
    buf.append("// dispatch correctly without a separate runtime initialiser.\n//\n");
    buf.append("// Other helpers (GetName, IsA, FindObject, ...) route through the\n");
    buf.append("// AIOCore::g_NameResolver / g_FindObject hooks declared in the\n");
    buf.append("// preamble. Wire those once from your bridge before calling them.\n\n");
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
    if (!AIOCore::g_NameResolver) return {};
    const char* s = AIOCore::g_NameResolver(NamePrivate.ComparisonIndex);
    return s ? std::string(s) : std::string();
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

// ---- Object lookup -------------------------------------------------
inline UObject* UObject::FindObjectImpl(const std::string& FullName, EClassCastFlags RequiredType)
{
    if (!AIOCore::g_FindObject) return nullptr;
    return AIOCore::g_FindObject(FullName.c_str(), /*isFullName*/true,
                                 static_cast<uint64_t>(RequiredType));
}

inline UObject* UObject::FindObjectFastImpl(const std::string& Name, EClassCastFlags RequiredType)
{
    if (!AIOCore::g_FindObject) return nullptr;
    return AIOCore::g_FindObject(Name.c_str(), /*isFullName*/false,
                                 static_cast<uint64_t>(RequiredType));
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
// pulls in this header.
template<StringLiteral Name>
inline UClass* StaticClassImpl()
{
    static UClass* Cached = nullptr;
    if (!Cached && AIOCore::g_FindObject)
    {
        Cached = static_cast<UClass*>(
            AIOCore::g_FindObject(static_cast<const char*>(Name.Chars),
                                  /*isFullName*/false,
                                  /*castFlagsBits*/0x20)); // ::Class
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
//  DumpAIOHeader — single monolithic header containing every dumped type.
// ============================================================================
void UEDumper::DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt)
{
    if (_sdkProcessed.empty())
    {
        aioBufferFmt.append("#pragma once\n\n");
        aioBufferFmt.append("#include <cstdint>\n#include <string>\n#include <functional>\n\n");
        aioBufferFmt.append("{}\n", kAIOPreamble);
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
    aioBufferFmt.append("#include <cstdint>\n#include <string>\n#include <functional>\n\n");
    aioBufferFmt.append("{}\n", kAIOPreamble);

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
//  DumpSDK_PerPackage (Plan A) — Basic.hpp + <pkg>.hpp x N + SDK.hpp.
//
//  Each package gets a single header that #includes Basic.hpp + every
//  cross-package full-type dep. Lets callers `#include "SDK_A/CoreUObject.hpp"`
//  and pull just FVector/UObject/etc without the rest of the dump.
// ============================================================================
void UEDumper::DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap)
{
    if (_sdkProcessed.empty())
    {
        logsBufferFmt.append("SDK Plan A: empty processed packages, skipping.\n");
        return;
    }

    const std::string prefix = "SDK_A/";

    // Basic.hpp — preamble + global forward decls + phantom decls
    {
        auto &basicBuf = outBuffersMap[prefix + "Basic.hpp"];
        basicBuf.append("#pragma once\n\n");
        basicBuf.append("#include <cstdint>\n#include <string>\n#include <functional>\n\n");
        basicBuf.append("{}\n", kAIOPreamble);
        basicBuf.append("// === Forward declarations of all dumped types ===\n\n");
        for (const auto &p : _sdkProcessed)
        {
            for (const auto &e : p.Enums)
                basicBuf.append("enum class {} : {};\n", e.CppNameOnly, e.UnderlyingType);
        }
        basicBuf.append("\n");
        for (const auto &p : _sdkProcessed)
        {
            for (const auto &s : p.Structures) basicBuf.append("struct {};\n", s.CppNameOnly);
            for (const auto &c : p.Classes)    basicBuf.append("struct {};\n", c.CppNameOnly);
        }
        basicBuf.append("\n");
        if (!_sdkPhantomEnums.empty() || !_sdkPhantomStructs.empty())
        {
            basicBuf.append("// === Phantom forward declarations ===\n");
            for (const auto &n : _sdkPhantomEnums)
                basicBuf.append("enum class {} : uint8_t;\n", n);
            for (const auto &n : _sdkPhantomStructs)
                basicBuf.append("struct {};\n", n);
            basicBuf.append("\n");
        }
    }

    // Per-package: <pkg>.hpp
    for (size_t pkgIdx : _sdkPkgOrder)
    {
        auto &pkg = _sdkProcessed[pkgIdx];
        const std::string fname = prefix + pkg.PackageName + ".hpp";
        auto &buf = outBuffersMap[fname];

        buf.append("#pragma once\n\n");
        buf.append("#include \"Basic.hpp\"\n");

        std::set<std::string> depPkgs;
        auto collect = [&](const UE_UPackage::Struct &s) {
            for (const auto &dep : s.FullDeps)
            {
                auto it = _sdkNameToPkg.find(dep);
                if (it == _sdkNameToPkg.end()) continue;
                if (it->second == pkgIdx) continue;
                depPkgs.insert(_sdkProcessed[it->second].PackageName);
            }
        };
        for (const auto &s : pkg.Structures) collect(s);
        for (const auto &c : pkg.Classes)    collect(c);
        for (const auto &dp : depPkgs)
            buf.append("#include \"{}.hpp\"\n", dp);
        buf.append("\n");

        buf.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                   pkg.PackageName, pkg.Enums.size(), pkg.Structures.size(), pkg.Classes.size());

        if (!pkg.Enums.empty())      UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &buf);
        if (!pkg.Structures.empty()) UE_UPackage::AppendStructsToBuffer(pkg.Structures, &buf);
        if (!pkg.Classes.empty())    UE_UPackage::AppendStructsToBuffer(pkg.Classes, &buf);

        if (pkg.PackageName == "CoreUObject")
            EmitAIOCoreHelpersBlock(buf, _processEventIndex);
    }

    // SDK.hpp aggregator
    {
        auto &sdkBuf = outBuffersMap[prefix + "SDK.hpp"];
        sdkBuf.append("#pragma once\n\n");
        sdkBuf.append("// Aggregator: every per-package header in topological order.\n\n");
        for (size_t pkgIdx : _sdkPkgOrder)
        {
            const auto &pkg = _sdkProcessed[pkgIdx];
            sdkBuf.append("#include \"{}.hpp\"\n", pkg.PackageName);
        }
    }

    logsBufferFmt.append("SDK Plan A: emitted {}Basic.hpp + {} package files + {}SDK.hpp\n",
                         prefix, _sdkPkgOrder.size(), prefix);
    logsBufferFmt.append("==========================\n");
}

// ============================================================================
//  DumpSDK_UECoreStyle (Plan B) — UECore-style core-only split.
//
//  Emits the CoreUObject reflection package as four files plus an SDK.hpp
//  aggregator, alongside the three UECore companions (Basic.h, Basic.cpp,
//  UnrealContainers.h) embedded verbatim from source/UEProber/UECore/ so
//  the resulting SDK_B/ directory is self-contained — 8 files total, no
//  manual file copying required.
//
//  Dumped content lives in `namespace SDK { ... }` to match UECore's
//  convention; the embedded Basic.h declares its types in the same
//  namespace, and `using namespace UC;` therein pulls TArray / int32 /
//  etc into scope without needing per-class qualification.
//
//  Helper methods (GetName / IsA / FindObject / ...) come from
//  EmitAIOCoreHelpersBlock with `emitTemplates=false` — embedded Basic.h
//  ships its own StaticClassImpl<> / GetDefaultObjImpl<> templates, so
//  emitting ours would duplicate-define them. The macro
//  DEFINE_UE_CLASS_HELPERS (used by the dumped Classes via Phase 1.6b)
//  is emitted once in CoreUObject_structs.hpp's prelude — macros are
//  global so this is enough for both _structs and _classes to see it.
// ============================================================================
void UEDumper::DumpSDK_UECoreStyle(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap)
{
    if (_sdkProcessed.empty())
    {
        logsBufferFmt.append("SDK Plan B: empty processed packages, skipping.\n");
        return;
    }

    // Plan B targets only the CoreUObject package — locate it.
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
        logsBufferFmt.append("SDK Plan B: CoreUObject package not found in dump, skipping.\n");
        return;
    }

    const std::string prefix = "SDK_B/";
    auto &pkg = _sdkProcessed[coreIdx];

    // ---- 1. UECore companions (verbatim embed) -------------------------
    // Basic.cpp's references to UObject members were patched in
    // UECoreEmbed.hpp: Class->Index → ClassPrivate->InternalIndex,
    // &Class->Name → &Class->NamePrivate, Object->Name → Object->NamePrivate,
    // Other->Index → Other->InternalIndex. UObject::GObjects is provided
    // by an injected static member on UObject (see classes.hpp emit below).
    outBuffersMap[prefix + "Basic.h"].append("{}", kUECoreBasicH);
    outBuffersMap[prefix + "Basic.cpp"].append("{}", kUECoreBasicCpp);
    outBuffersMap[prefix + "UnrealContainers.h"].append("{}", kUECoreUnrealContainersH);

    // ---- 2. CoreUObject_structs.hpp ------------------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_structs.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"Basic.h\"\n");
        buf.append("#include \"UnrealContainers.h\"\n\n");

        // DEFINE_UE_CLASS_HELPERS macro — used by every dumped Class via
        // Phase 1.6b. Macros are global so emitting once here covers
        // _structs.hpp and _classes.hpp transitively via the include chain.
        // Routed through "{}" so the macro body's literal { } don't get
        // interpreted as fmt placeholders.
        buf.append("{}", R"AIOMACRO(#ifndef DEFINE_UE_CLASS_HELPERS
#define DEFINE_UE_CLASS_HELPERS(FullClassName, ClassNameStr) \
    static struct UClass* StaticClass() { return StaticClassImpl<ClassNameStr>(); } \
    static struct FullClassName* GetDefaultObj() { return GetDefaultObjImpl<FullClassName>(); }
#endif

)AIOMACRO");

        buf.append("namespace SDK\n{{\n\n");
        buf.append("// Package: CoreUObject - Enums({}) + Structs({})\n\n",
                   pkg.Enums.size(), pkg.Structures.size());
        if (!pkg.Enums.empty())      UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &buf);
        if (!pkg.Structures.empty()) UE_UPackage::AppendStructsToBuffer(pkg.Structures, &buf);
        buf.append("}} // namespace SDK\n");
    }

    // ---- 3. CoreUObject_classes.hpp ------------------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_classes.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"CoreUObject_structs.hpp\"\n\n");
        buf.append("namespace SDK\n{{\n\n");
        buf.append("// Package: CoreUObject - Classes({})\n\n", pkg.Classes.size());

        if (!pkg.Classes.empty())
        {
            // Splice Plan B-only static into UObject: the embedded Basic.cpp
            // and FWeakObjectPtr operators reach into UObject::GObjects;
            // user wires it via SDK::UObject::GObjects.InitManually(addr)
            // at startup. (Plan A's preamble has no TUObjectArrayWrapper
            // type so we don't add this on the shared ExtraDecls.)
            std::vector<UE_UPackage::Struct> classesCopy = pkg.Classes;
            for (auto &c : classesCopy)
            {
                if (c.CppNameOnly == "UObject")
                {
                    std::string injection =
                        "\t// Plan B / UECore-style global object table. Wire via\n"
                        "\t// SDK::UObject::GObjects.InitManually(<GUObjectArray addr>)\n"
                        "\t// once at startup before calling FindObject* / FWeakObjectPtr.\n"
                        "\tstatic inline class TUObjectArrayWrapper GObjects;\n\n";
                    c.ExtraDecls = injection + c.ExtraDecls;
                    break;
                }
            }
            UE_UPackage::AppendStructsToBuffer(classesCopy, &buf);
        }

        // Helper bodies — skip StaticClassImpl/GetDefaultObjImpl templates
        // (Basic.h provides those, with its own BasicFilesImpleUtils chain).
        EmitAIOCoreHelpersBlock(buf, _processEventIndex, /*emitTemplates=*/false);

        buf.append("\n}} // namespace SDK\n");
    }

    // ---- 4. CoreUObject_parameters.hpp (stub) --------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_parameters.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("// CoreUObject ProcessEvent parameter structs.\n");
        buf.append("// Currently empty; future passes will emit one struct per UFunction.\n\n");
        buf.append("#include \"CoreUObject_structs.hpp\"\n");
    }

    // ---- 5. CoreUObject_functions.cpp (stub) ---------------------------
    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_functions.cpp"];
        buf.append("// CoreUObject function bodies (ProcessEvent dispatch).\n");
        buf.append("// Currently empty; future passes will emit one body per UFunction\n");
        buf.append("// using AIOCore::kProcessEventIndex from CoreUObject_classes.hpp.\n\n");
        buf.append("#include \"CoreUObject_classes.hpp\"\n");
        buf.append("#include \"CoreUObject_parameters.hpp\"\n");
    }

    // ---- 6. SDK.hpp (single-include entry) -----------------------------
    {
        auto &buf = outBuffersMap[prefix + "SDK.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("// Single-include entry. Pulls CoreUObject_classes.hpp which\n");
        buf.append("// transitively pulls CoreUObject_structs.hpp -> Basic.h +\n");
        buf.append("// UnrealContainers.h. Link Basic.cpp into your TU(s).\n\n");
        buf.append("#include \"CoreUObject_classes.hpp\"\n");
    }

    logsBufferFmt.append("SDK Plan B: emitted Basic.h/Basic.cpp/UnrealContainers.h + CoreUObject (4 files) + SDK.hpp under {}\n", prefix);
    logsBufferFmt.append("==========================\n");
}
