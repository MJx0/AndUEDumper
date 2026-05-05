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

    if (_sdkMode == SDKMode::Both
        || _sdkMode == SDKMode::OnlyA
        || _sdkMode == SDKMode::OnlyB) // OnlyB legacy, treated as OnlyA
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


// Topological sort (DFS); cycle nodes emit in DFS-encounter order.
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
    _sdkPackagesUnsaved.clear();

    SimpleProgressBar dumpProgress(int(packages.size()));
    if (progressCallback)
        progressCallback(dumpProgress);

    auto excludedObjects = _profile->GetExcludedObjects();

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

    // Drop preamble-provided names and collapse duplicates (UNone x N etc).
    {
        static const std::unordered_set<std::string> kPreambleProvided = {
            "FName", "FString", "FText",
            "FWeakObjectPtr", "FUniqueObjectGuid",
            // FSoftObjectPath layout depends on per-game FName size; let dump emit it.
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

    // Augment core reflection classes (UObject/UField/UStruct/UEnum/UFunction/UClass):
    // stitch typed members back in over the opaque Pad_0xN[] regions the
    // property walker produces, using offsets from UE_Offsets.
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
                // offset 0 = not discovered; skip
                if (off == 0 && !allowZeroOffset) return;
                v.push_back({off, size, std::move(type), std::move(name)});
            };

            if (cppName == "UObject")
            {
                // synthetic vtable slot; dumper has no offset, force-emit at 0
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
                add(offs.UStruct.ChildProperties, 8, "struct FField*",  "ChildProperties");
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

            // drop fields inside inherited range or past struct size
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

            // strip self-reference re-added by ExtractTypeDeps
            s.FullDeps.erase(s.CppNameOnly);
            s.ForwardDeps.erase(s.CppNameOnly);

            s.Members = std::move(rebuilt);

            if (s.CppNameOnly == "UObject")
            {
                s.PrefixDecls = "\tstatic inline class TUObjectArrayWrapper GObjects;\n";
                s.ExtraDecls =
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

        // Emit DEFINE_UE_CLASS_HELPERS on every Class (skips ScriptStructs).
        for (auto &p : _sdkProcessed)
        {
            for (auto &c : p.Classes)
            {
                c.PrefixDecls += fmt::format(
                    "\tDEFINE_UE_CLASS_HELPERS({}, \"{}\")\n",
                    c.CppNameOnly, c.Name);
            }
        }

        // Inject math operators + helpers on canonical UE math structs.
        // Member-scoped so they can't collide with user code.
        for (auto &p : _sdkProcessed)
        {
            for (auto &s : p.Structures)
            {
                const char* mathOps = nullptr;
                if (s.CppNameOnly == "FVector")
                {
                    mathOps = R"FVOPS(
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

    // only full deps drive package order; fwd-decls handled separately
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

    _sdkPkgOrder.reserve(_sdkProcessed.size());
    {
        std::vector<size_t> indices(_sdkProcessed.size());
        for (size_t i = 0; i < _sdkProcessed.size(); ++i) indices[i] = i;

        _sdkPkgOrder = TopoSort<size_t>(indices, [&](size_t i) -> const std::unordered_set<size_t> &
        {
            return pkgDeps[i];
        });
    }


    // intra-package topo sort: value-type deps first
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


static void EmitSDKFunctionsCppBodies(BufferFmt &buf, int processEventIndex)
{
    buf.append("constexpr int kProcessEventIndex = {};\n\n", processEventIndex);
    buf.append("{}", R"AIOIMPL(void UObject::ProcessEvent(struct UFunction* Function, void* Parms) const
{
    using FN = void(*)(const UObject*, struct UFunction*, void*);
    auto vtbl = *reinterpret_cast<void* const* const*>(this);
    reinterpret_cast<FN>(vtbl[kProcessEventIndex])(this, Function, Parms);
}

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
    return FindObject<UClass>(ClassFullName, EClassCastFlags::Class);
}

class UClass* UObject::FindClassFast(const std::string& ClassName)
{
    return FindObjectFast<UClass>(ClassName, EClassCastFlags::Class);
}

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
    if (HasTypeFlag(EClassCastFlags::Class))
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

static void EmitUClassGetFunctionBody(BufferFmt &buf, bool emitInline)
{
    const char *kw = emitInline ? "inline " : "";
    buf.append(
        "{}struct UFunction* UClass::GetFunction(const std::string& ClassName, "
        "const std::string& FuncName) const\n"
        "{{\n"
        "    for (const struct UStruct* Clss = this; Clss; Clss = Clss->SuperStruct)\n"
        "    {{\n"
        "        if (Clss->GetName() != ClassName) continue;\n"
        "        for (struct UField* Field = Clss->Children; Field; Field = Field->Next)\n"
        "        {{\n"
        "            if (Field->HasTypeFlag(EClassCastFlags::Function) "
        "&& Field->GetName() == FuncName)\n"
        "                return static_cast<struct UFunction*>(Field);\n"
        "        }}\n"
        "    }}\n"
        "    return nullptr;\n"
        "}}\n\n", kw);
}

static void EmitUFunctionBody(BufferFmt &buf,
                              const UE_UPackage::Function &f,
                              bool emitInline,
                              const std::unordered_map<std::string, std::string> &enumUnderlying)
{
    const char *kw = emitInline ? "inline " : "";

    // strip leading 'static ' — only valid in class-body declaration
    std::string headOnly = f.CppName;
    const std::string staticPrefix = "static ";
    if (headOnly.compare(0, staticPrefix.size(), staticPrefix) == 0)
        headOnly = headOnly.substr(staticPrefix.size());

    // inject Owner:: qualifier
    const auto spacePos = headOnly.rfind(' ');
    if (spacePos == std::string::npos)
        return; // malformed — skip
    std::string qualifiedHead = headOnly.substr(0, spacePos + 1)
                              + f.OwnerCppName + "::"
                              + headOnly.substr(spacePos + 1);

    const bool hasReturn = (f.ReturnType != "void");

    // elaborated 'enum X' (not 'enum class') — clang rejects use-site enum class
    auto qualifyType = [&](const std::string &t) -> std::string {
        return enumUnderlying.count(t) ? "enum " + t : t;
    };

    buf.append("{}{}({})\n{{\n", kw, qualifiedHead, f.Params);

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

    // Parms struct: param order matches UE reflection, ReturnValue last
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

    // fill in-params; pure out-params stay default-constructed.
    // C-cast strips ConstParm const so refs bind to non-const Parms field.
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

    if (f.IsStatic)
        buf.append("    GetDefaultObj()->ProcessEvent(Func, &Parms);\n");
    else
        buf.append("    this->ProcessEvent(Func, &Parms);\n");

    // copy out: pure out-params back to caller
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

static void EmitPackageFunctionBodies(BufferFmt &buf,
                                      const UE_UPackage &pkg,
                                      bool emitInline,
                                      const std::unordered_map<std::string, std::string> &enumUnderlying)
{
    for (const auto &c : pkg.Classes)
    {
        if (c.Functions.empty()) continue;
        buf.append("// {}\n", c.CppNameOnly);
        for (const auto &f : c.Functions)
            EmitUFunctionBody(buf, f, emitInline, enumUnderlying);
    }
}

// strip leading UTF-8 BOM (clang rejects U+FEFF); BOM may be at offset 0 or 1
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

// patch bWITH_CASE_PRESERVING_NAME from per-game profile (8B union -> 12B fields)
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

// retarget Basic.cpp's CoreUObject_classes.h include to .hpp
static std::string RetargetBasicCppIncludes(std::string content)
{
    const std::string from = "#include \"CoreUObject_classes.h\"";
    const std::string to   = "#include \"CoreUObject_classes.hpp\"";
    auto pos = content.find(from);
    if (pos != std::string::npos)
        content.replace(pos, from.size(), to);
    return content;
}

static void EmitSDKCoreFiles(
    const std::string& prefix,
    const UE_UPackage& corePkg,
    int processEventIndex,
    bool casePreserving,
    const std::unordered_map<std::string, std::string>& enumUnderlying,
    std::unordered_map<std::string, BufferFmt>& outBuffersMap)
{
    outBuffersMap[prefix + "Basic.h"].append("{}",
        StripUtf8Bom(ApplyCasePreservingDefine(kUECoreBasicH, casePreserving)));
    outBuffersMap[prefix + "Basic.cpp"].append("{}",
        RetargetBasicCppIncludes(StripUtf8Bom(kUECoreBasicCpp)));
    outBuffersMap[prefix + "UnrealContainers.h"].append("{}", StripUtf8Bom(kUECoreUnrealContainersH));

    // utfcpp shipped under <prefix>utfcpp/ so SDK is self-contained
    outBuffersMap[prefix + "utfcpp/core.h"].append("{}", StripUtf8Bom(kUtfcppCoreH));
    outBuffersMap[prefix + "utfcpp/unchecked.h"].append("{}", StripUtf8Bom(kUtfcppUncheckedH));

    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_structs.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"Basic.h\"\n");
        buf.append("#include \"UnrealContainers.h\"\n");
        buf.append("#include <cmath>\n\n"); // for injected math helpers

        // DEFINE_UE_CLASS_HELPERS macro emitted once here; transitively
        // pulled by _classes.hpp and every Packages/<pkg>.hpp.
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

    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_classes.hpp"];
        buf.append("#pragma once\n\n");
        buf.append("#include \"CoreUObject_structs.hpp\"\n\n");
        buf.append("namespace SDK\n{{\n\n");

        buf.append("// Package: CoreUObject - Classes({})\n\n", corePkg.Classes.size());

        if (!corePkg.Classes.empty())
            UE_UPackage::AppendStructsToBuffer(const_cast<std::vector<UE_UPackage::Struct>&>(corePkg.Classes), &buf);

        buf.append("\n}} // namespace SDK\n");
    }

    {
        auto &buf = outBuffersMap[prefix + "CoreUObject_functions.cpp"];
        buf.append("// Bodies for UObject helpers + UFunction dispatch.\n");
        buf.append("// Wire FName::s_NameResolver and UObject::GObjects at startup.\n\n");
        buf.append("#include \"Basic.h\"\n");
        buf.append("#include \"CoreUObject_classes.hpp\"\n");
        buf.append("#include <cstring> // memcpy for ArrayDim>1 param marshalling\n\n");
        buf.append("namespace SDK\n{{\n\n");
        EmitSDKFunctionsCppBodies(buf, processEventIndex);
        EmitUClassGetFunctionBody(buf, /*emitInline=*/false);
        EmitPackageFunctionBodies(buf, corePkg, /*emitInline=*/false, enumUnderlying);
        buf.append("}} // namespace SDK\n");
    }
}

void UEDumper::DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt)
{
    if (_sdkProcessed.empty())
    {
        aioBufferFmt.append("#pragma once\n\n");
        aioBufferFmt.append("// (empty dump — no packages processed)\n");
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
    aioBufferFmt.append("// AIOHeader.hpp - browse-only types dump (not a compilation entry point).\n\n");
    aioBufferFmt.append("namespace SDK\n{{\n\n");

    int packages_saved = 0;
    int classes_saved = 0;
    int structs_saved = 0;
    int enums_saved = 0;

    for (size_t pkgIdx : _sdkPkgOrder)
    {
        auto &pkg = _sdkProcessed[pkgIdx];
        aioBufferFmt.append("// Package: {}\n// Enums: {}\n// Structs: {}\n// Classes: {}\n\n",
                            pkg.PackageName, pkg.Enums.size(), pkg.Structures.size(), pkg.Classes.size());
        if (!pkg.Enums.empty())      UE_UPackage::AppendEnumsToBuffer(pkg.Enums, &aioBufferFmt);
        if (!pkg.Structures.empty()) UE_UPackage::AppendStructsToBuffer(pkg.Structures, &aioBufferFmt);
        if (!pkg.Classes.empty())    UE_UPackage::AppendStructsToBuffer(pkg.Classes, &aioBufferFmt);

        packages_saved++;
        classes_saved += pkg.Classes.size();
        structs_saved += pkg.Structures.size();
        enums_saved   += pkg.Enums.size();
    }

    aioBufferFmt.append("}} // namespace SDK\n");

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

void UEDumper::DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap)
{
    if (_sdkProcessed.empty())
    {
        logsBufferFmt.append("SDK Plan A: empty processed packages, skipping.\n");
        return;
    }

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

    const bool casePreserving =
        _profile && _profile->GetUEVars() && _profile->GetUEVars()->GetOffsets()
            ? _profile->GetUEVars()->GetOffsets()->Config.isUsingCasePreservingName
            : false;
    EmitSDKCoreFiles(prefix, _sdkProcessed[coreIdx], _processEventIndex, casePreserving, _sdkEnumUnderlying, outBuffersMap);

    size_t nonCorePkgCount = 0;
    for (size_t pkgIdx : _sdkPkgOrder)
    {
        if (pkgIdx == coreIdx) continue;
        auto &pkg = _sdkProcessed[pkgIdx];
        const std::string fname = pkgPrefix + pkg.PackageName + ".hpp";
        auto &buf = outBuffersMap[fname];

        buf.append("#pragma once\n\n");
        // pulls _structs.hpp + Basic.h + UnrealContainers.h transitively
        buf.append("#include \"../CoreUObject_classes.hpp\"\n");

        // Cross-pkg deps: struct FullDep -> #include; enum FullDep or any
        // ForwardDep -> fwd-decl (enums always fwd-decl to avoid cycles).
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

        // fwd-decls: same-pkg + cross-pkg ptr-only + cross-pkg enums
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

        // <pkg>_functions.cpp: out-of-line bodies. Pulls every other non-core
        // pkg referenced by a function signature, since those may be fwd-only
        // in <pkg>.hpp.
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

    {
        auto &sdkBuf = outBuffersMap[prefix + "SDK.hpp"];
        sdkBuf.append("#pragma once\n\n");
        sdkBuf.append("// SDK.hpp - declaration aggregator.\n");
        sdkBuf.append("// Compile/link Packages/*_functions.cpp + CoreUObject_functions.cpp + Basic.cpp.\n\n");
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

