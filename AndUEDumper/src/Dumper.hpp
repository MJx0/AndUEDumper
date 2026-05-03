#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "UE/UEGameProfile.hpp"
#include "UE/UEWrappers.hpp"

#include "Utils/BufferFmt.hpp"
#include "Utils/ProgressUtils.hpp"

using ProgressCallback = std::function<void(const SimpleProgressBar &)>;
using UEPackagesArray = std::vector<std::pair<uint8_t *const, std::vector<UE_UObject>>>;

#include "UPackageGenerator.hpp"

class UEDumper
{
public:
    // Which split-style SDKs to emit alongside the always-present
    // monolithic AIOHeader.hpp. Both is the default — callers that don't
    // want a specific style can call SetSDKMode before Dump().
    enum class SDKMode : uint8_t {
        Both    = 0, // SDK_A/ + SDK_B/
        OnlyA   = 1, // SDK_A/ only
        OnlyB   = 2, // SDK_B/ only
        None    = 3, // skip both, AIOHeader.hpp only
    };

private:
    IGameProfile const *_profile;
    std::string _lastError;
    std::function<void(bool)> _dumpExeInfoNotify;
    std::function<void(bool)> _dumpNamesInfoNotify;
    std::function<void(bool)> _dumpObjectsInfoNotify;
    std::function<void(bool)> _dumpOffsetsInfoNotify;
    ProgressCallback _objectsProgressCallback;
    ProgressCallback _dumpProgressCallback;

    // Discovered by DumpOffsetsInfo and consumed by AIOHeader / SDK
    // emitters to bake the per-game ProcessEvent vtable slot into the
    // AIOCore helper block.
    int _processEventIndex = 0;

    SDKMode _sdkMode = SDKMode::Both;

    // Cached output of BuildProcessedPackages — produced once per Dump()
    // and consumed by DumpAIOHeader / DumpSDK_PerPackage /
    // DumpSDK_UECoreStyle. Cleared at the top of each Dump().
    std::vector<UE_UPackage> _sdkProcessed;
    std::unordered_map<std::string, size_t> _sdkNameToPkg;
    // name -> "uint8_t"/"uint16_t"/... so cross-pkg enum refs can be emitted
    // as `enum class EFoo : <ut>;` forward decls instead of #including the
    // defining package (which formed cycles between e.g. GPGameInput and
    // GPGlobalDefines, each ref'ing an enum from the other).
    std::unordered_map<std::string, std::string> _sdkEnumUnderlying;
    std::vector<size_t> _sdkPkgOrder;
    std::set<std::string> _sdkPhantomEnums;
    std::set<std::string> _sdkPhantomStructs;
    std::string _sdkPackagesUnsaved;

public:
    UEDumper() : _profile(nullptr), _dumpExeInfoNotify(nullptr), _dumpNamesInfoNotify(nullptr), _dumpObjectsInfoNotify(nullptr), _objectsProgressCallback(nullptr), _dumpProgressCallback(nullptr) {}

    void SetSDKMode(SDKMode m) { _sdkMode = m; }
    SDKMode GetSDKMode() const { return _sdkMode; }

    bool Init(IGameProfile *profile);

    bool Dump(std::unordered_map<std::string, BufferFmt> *outBuffersMap);

    const IGameProfile *GetProfile() const { return _profile; }

    std::string GetLastError() const { return _lastError; }

    inline void setDumpExeInfoNotify(const std::function<void(bool)> &f) { _dumpExeInfoNotify = f; }
    inline void setDumpNamesInfoNotify(const std::function<void(bool)> &f) { _dumpNamesInfoNotify = f; }
    inline void setDumpObjectsInfoNotify(const std::function<void(bool)> &f) { _dumpObjectsInfoNotify = f; }
    inline void setDumpOffsetsInfoNotify(const std::function<void(bool)> &f) { _dumpOffsetsInfoNotify = f; }

    inline void setObjectsProgressCallback(const ProgressCallback &f) { _objectsProgressCallback = f; }
    inline void setDumpProgressCallback(const ProgressCallback &f) { _dumpProgressCallback = f; }

private:
    void DumpExecutableInfo(BufferFmt &logsBufferFmt);

    void DumpNamesInfo(BufferFmt &logsBufferFmt);

    void DumpObjectsInfo(BufferFmt &logsBufferFmt);

    void DumpOffsetsInfo(BufferFmt &logsBufferFmt, BufferFmt &offsetsBufferFmt);

    void GatherUObjects(BufferFmt &logsBufferFmt, BufferFmt &objsBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback);

    // Phase 1-4 of the SDK pipeline: process every package, drop preamble
    // duplicates, augment core reflection classes, build name->pkg /
    // pkg->deps maps, topo-sort packages, compute phantom forward decls.
    // Caches results into the _sdk* members so the various emit functions
    // share one pass.
    void BuildProcessedPackages(UEPackagesArray &packages, const ProgressCallback &progressCallback);

    void DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt);

    // Plan A: per-package single .hpp + Basic.hpp + SDK.hpp aggregator.
    // Inserts entries with prefixed paths (e.g. "SDK_A/Basic.hpp") into
    // outBuffersMap.
    void DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

    // Plan B: UECore-style 4-file split per package
    // (<pkg>_classes.hpp / _structs.hpp / _parameters.hpp / _functions.cpp)
    // plus shared Basic.hpp + UnrealContainers.hpp + SDK.hpp aggregator.
    void DumpSDK_UECoreStyle(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);
};
