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

    outBuffersMap->insert({"AIOHeader.hpp", BufferFmt()});
    BufferFmt &aioBufferFmt = outBuffersMap->at("AIOHeader.hpp");
    DumpAIOHeader(logsBufferFmt, aioBufferFmt, packages, _dumpProgressCallback);

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

void UEDumper::DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback)
{
    int classes_saved = 0;
    int structs_saved = 0;
    int enums_saved = 0;

    static bool processInternal_once = false;

    SimpleProgressBar dumpProgress(int(packages.size()));
    if (progressCallback)
        progressCallback(dumpProgress);

    auto excludedObjects = _profile->GetExcludedObjects();

    // ---- Phase 1: process every package and collect type metadata ------------

    std::vector<UE_UPackage> processed;
    processed.reserve(packages.size());

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

        processed.push_back(std::move(pkg));
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

        for (auto &p : processed)
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
                // Implementation lives in the AIOCore block at the end of
                // the header so it can dispatch through UFunction.
                s.ExtraDecls =
                    "\t// === AIO Core helpers (inline body at end of header) ===\n"
                    "\tvoid ProcessEvent(struct UFunction* Function, void* Parms) const;\n";
            }
        };

        for (auto &p : processed)
        {
            for (auto &c : p.Classes)    augment(c);
            for (auto &s : p.Structures) augment(s); // harmless on non-targets
        }
    }

    // Drop packages that ended up with nothing to emit
    std::string packages_unsaved;
    {
        std::vector<UE_UPackage> kept;
        kept.reserve(processed.size());
        for (auto &p : processed)
        {
            if (!p.Classes.empty() || !p.Structures.empty() || !p.Enums.empty())
            {
                kept.push_back(std::move(p));
            }
            else
            {
                packages_unsaved += "\t";
                packages_unsaved += p.PackageName + ",\n";
            }
        }
        processed = std::move(kept);
    }

    if (processed.empty())
    {
        aioBufferFmt.append("#pragma once\n\n#include <cstdint>\n\n");
        aioBufferFmt.append("{}\n", kAIOPreamble);
        logsBufferFmt.append("Saved packages: 0\nSaved classes: 0\nSaved structs: 0\nSaved enums: 0\n");
        if (!packages_unsaved.empty())
        {
            packages_unsaved.erase(packages_unsaved.size() - 2);
            logsBufferFmt.append("Unsaved packages: [\n{}\n]\n", packages_unsaved);
        }
        logsBufferFmt.append("==========================\n");
        return;
    }

    // ---- Phase 2: build name -> package map for cross-package linking --------

    std::unordered_map<std::string, size_t> nameToPkgIdx;
    nameToPkgIdx.reserve(processed.size() * 64);

    auto registerType = [&](const std::string &name, size_t pkgIdx)
    {
        if (name.empty()) return;
        nameToPkgIdx.emplace(name, pkgIdx);
    };

    for (size_t i = 0; i < processed.size(); ++i)
    {
        for (const auto &s : processed[i].Structures) registerType(s.CppNameOnly, i);
        for (const auto &c : processed[i].Classes)    registerType(c.CppNameOnly, i);
        for (const auto &e : processed[i].Enums)      registerType(e.CppNameOnly, i);
    }

    // ---- Phase 3: build inter-package dependency graph -----------------------

    // Only "full" deps drive package ordering; pure forward-decl deps are
    // satisfied by the global forward-decl block emitted up front.
    std::vector<std::unordered_set<size_t>> pkgDeps(processed.size());

    auto recordDeps = [&](size_t pkgIdx, const UE_UPackage::Struct &s)
    {
        for (const auto &dep : s.FullDeps)
        {
            auto it = nameToPkgIdx.find(dep);
            if (it == nameToPkgIdx.end()) continue;
            if (it->second == pkgIdx) continue;
            pkgDeps[pkgIdx].insert(it->second);
        }
    };

    for (size_t i = 0; i < processed.size(); ++i)
    {
        for (const auto &s : processed[i].Structures) recordDeps(i, s);
        for (const auto &c : processed[i].Classes)    recordDeps(i, c);
    }

    // ---- Phase 4: topological sort packages ----------------------------------

    std::vector<size_t> pkgOrder;
    pkgOrder.reserve(processed.size());
    {
        std::vector<size_t> indices(processed.size());
        for (size_t i = 0; i < processed.size(); ++i) indices[i] = i;

        pkgOrder = TopoSort<size_t>(indices, [&](size_t i) -> const std::unordered_set<size_t> &
        {
            return pkgDeps[i];
        });
    }

    // ---- Phase 5: emit AIOHeader ---------------------------------------------

    aioBufferFmt.append("#pragma once\n\n#include <cstdint>\n\n");
    aioBufferFmt.append("{}\n", kAIOPreamble);

    // 5a. Forward declarations of every dumped enum, struct and class. Pointer
    //     and template-wrapped references between packages are resolved via
    //     these forward decls, so cyclic class graphs compile cleanly.
    aioBufferFmt.append("// === Forward declarations ===\n\n");
    for (const auto &p : processed)
    {
        for (const auto &e : p.Enums)
            aioBufferFmt.append("enum class {} : {};\n", e.CppNameOnly, e.UnderlyingType);
    }
    aioBufferFmt.append("\n");
    for (const auto &p : processed)
    {
        for (const auto &s : p.Structures) aioBufferFmt.append("struct {};\n", s.CppNameOnly);
        for (const auto &c : p.Classes)    aioBufferFmt.append("struct {};\n", c.CppNameOnly);
    }
    aioBufferFmt.append("\n");

    // 5a'. Phantom forward declarations: any name that appears in some
    //      member's type string but isn't defined by any dumped struct/
    //      class/enum AND isn't supplied by the preamble. Comes from
    //      partially-resolved property types (e.g. `TFieldPath<FBoolProperty>`,
    //      bare `ObjectPtrProperty` returns, `EBlah` enum references where
    //      the enum object wasn't reachable). We classify by UE prefix.
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
        std::set<std::string> phantomEnums;
        std::set<std::string> phantomStructs;
        auto considerName = [&](const std::string &name)
        {
            if (name.empty()) return;
            if (kPreambleNames.count(name)) return;
            if (nameToPkgIdx.count(name)) return;
            if (name.size() < 2 || !std::isupper(static_cast<unsigned char>(name[0])))
                return;
            const char prefix = name[0];
            if (prefix == 'E')
                phantomEnums.insert(name);
            else if (prefix == 'F' || prefix == 'U' || prefix == 'A' || prefix == 'I')
                phantomStructs.insert(name);
            else
                phantomStructs.insert(name);
        };
        for (const auto &p : processed)
        {
            auto walk = [&](const UE_UPackage::Struct &s)
            {
                for (const auto &n : s.FullDeps)    considerName(n);
                for (const auto &n : s.ForwardDeps) considerName(n);
            };
            for (const auto &s : p.Structures) walk(s);
            for (const auto &c : p.Classes)    walk(c);
        }
        if (!phantomEnums.empty() || !phantomStructs.empty())
        {
            aioBufferFmt.append("// === Phantom forward declarations ===\n");
            aioBufferFmt.append("// Type names referenced in member / function signatures but not\n");
            aioBufferFmt.append("// defined as a dumped struct/enum (typically partially-resolved\n");
            aioBufferFmt.append("// property kinds). Forward decls keep pointer / template /\n");
            aioBufferFmt.append("// signature usages compilable.\n");
            for (const auto &n : phantomEnums)
                aioBufferFmt.append("enum class {} : uint8_t;\n", n);
            for (const auto &n : phantomStructs)
                aioBufferFmt.append("struct {};\n", n);
            aioBufferFmt.append("\n");
        }
    }

    // 5b. Per-package output, packages in topological order, structs/classes
    //     within each package sorted by their intra-package full-type deps.
    auto sortByDeps = [&](size_t pkgIdx, std::vector<UE_UPackage::Struct> &items)
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
                // Same-package value-type deps must be defined first
                auto it = nameToLocal.find(dep);
                if (it != nameToLocal.end()) deps[i].push_back(it->second);
            }
        }
        (void)pkgIdx;

        auto sorted = TopoSort<size_t>(indices, [&](size_t i) -> const std::vector<size_t> &
        {
            return deps[i];
        });

        std::vector<UE_UPackage::Struct> reordered;
        reordered.reserve(items.size());
        for (size_t i : sorted) reordered.push_back(std::move(items[i]));
        items = std::move(reordered);
    };

    int packages_saved = 0;
    for (size_t pkgIdx : pkgOrder)
    {
        auto &pkg = processed[pkgIdx];

        aioBufferFmt.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                            pkg.PackageName, pkg.Enums.size(), pkg.Structures.size(), pkg.Classes.size());

        if (!pkg.Enums.empty())
            UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &aioBufferFmt);

        if (!pkg.Structures.empty())
        {
            sortByDeps(pkgIdx, pkg.Structures);
            UE_UPackage::AppendStructsToBuffer(pkg.Structures, &aioBufferFmt);
        }

        if (!pkg.Classes.empty())
        {
            sortByDeps(pkgIdx, pkg.Classes);
            UE_UPackage::AppendStructsToBuffer(pkg.Classes, &aioBufferFmt);
        }

        packages_saved++;
        classes_saved += pkg.Classes.size();
        structs_saved += pkg.Structures.size();
        enums_saved += pkg.Enums.size();

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

    // ---- Phase 5c: AIOCore inline helpers ------------------------------------
    //
    // Goes at the very end so it can dispatch through UFunction (defined
    // mid-file once CoreUObject lands). The augmenter (Phase 1.6) already
    // injected matching declarations on UObject; this block supplies the
    // inline bodies that pull the per-game ProcessEvent vtable slot out of
    // the dump.
    aioBufferFmt.append("\n// === AIO Core Helpers ===\n");
    aioBufferFmt.append("// Generated runtime glue tying the dumped UE reflection layout to\n");
    aioBufferFmt.append("// per-game offsets discovered during the dump. The ProcessEvent\n");
    aioBufferFmt.append("// vtable slot baked in here lets `Obj->ProcessEvent(Func, &Parms)`\n");
    aioBufferFmt.append("// dispatch correctly without a separate runtime initialiser.\n\n");
    aioBufferFmt.append("#ifndef AIOHeader_CORE_HELPERS_DEFINED\n");
    aioBufferFmt.append("#define AIOHeader_CORE_HELPERS_DEFINED\n\n");
    aioBufferFmt.append("namespace AIOCore\n{{\n");
    aioBufferFmt.append("    // Vtable slot of UObject::ProcessEvent in the target image.\n");
    aioBufferFmt.append("    // Discovered during dumping; override by #define-ing\n");
    aioBufferFmt.append("    // AIOCORE_PROCESS_EVENT_INDEX before #include if you need to\n");
    aioBufferFmt.append("    // swap it for a custom build.\n");
    aioBufferFmt.append("    #ifdef AIOCORE_PROCESS_EVENT_INDEX\n");
    aioBufferFmt.append("    constexpr int kProcessEventIndex = AIOCORE_PROCESS_EVENT_INDEX;\n");
    aioBufferFmt.append("    #else\n");
    aioBufferFmt.append("    constexpr int kProcessEventIndex = {};\n", _processEventIndex);
    aioBufferFmt.append("    #endif\n");
    aioBufferFmt.append("}}\n\n");
    aioBufferFmt.append("inline void UObject::ProcessEvent(struct UFunction* Function, void* Parms) const\n");
    aioBufferFmt.append("{{\n");
    aioBufferFmt.append("    using FN = void(*)(const UObject*, struct UFunction*, void*);\n");
    aioBufferFmt.append("    auto vtbl = *reinterpret_cast<void* const* const*>(this);\n");
    aioBufferFmt.append("    reinterpret_cast<FN>(vtbl[AIOCore::kProcessEventIndex])(this, Function, Parms);\n");
    aioBufferFmt.append("}}\n\n");
    aioBufferFmt.append("#endif // AIOHeader_CORE_HELPERS_DEFINED\n");

    logsBufferFmt.append("Saved packages: {}\nSaved classes: {}\nSaved structs: {}\nSaved enums: {}\n", packages_saved, classes_saved, structs_saved, enums_saved);

    if (!packages_unsaved.empty())
    {
        packages_unsaved.erase(packages_unsaved.size() - 2);
        logsBufferFmt.append("Unsaved packages: [\n{}\n]\n", packages_unsaved);
    }

    logsBufferFmt.append("==========================\n");
}
