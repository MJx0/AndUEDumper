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

    // Emit AIOHeader.hpp under SDK_B/. Types-only monolith covering every
    // *non-CoreUObject* package — CoreUObject types live in the sibling
    // CoreUObject_classes.hpp / Basic.h that AIOHeader.hpp #includes, so
    // re-emitting them here would clash on first redefinition. coreIdx is
    // the index of the CoreUObject package in _sdkProcessed.
    void DumpAIOHeader(BufferFmt &logsBufferFmt, size_t coreIdx, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

    // Plan A: per-package single .hpp + Basic.hpp + SDK.hpp aggregator.
    // Inserts entries with prefixed paths (e.g. "SDK_A/Basic.hpp") into
    // outBuffersMap.
    void DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

    // Plan B: UECore-style root files + AIOHeader.hpp (all-types monolith)
    // + Packages/<pkg>_functions.cpp (out-of-line bodies). Shares the
    // _functions.cpp emission pipeline with Plan A via EmitPackageFunctionsCpp.
    void DumpSDK_UECoreStyle(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

    // Emit a single <pkg>_functions.cpp containing out-of-line ProcessEvent
    // dispatch bodies for every UFunction in pkg. headerInclude is the path
    // (relative to the .cpp's location) of the header that declares pkg's
    // types — for Plan A that's the sibling "<pkg>.hpp", for Plan B it's
    // "../AIOHeader.hpp". emitCrossPkgIncludes adds `#include "<dep>.hpp"`
    // for cross-pkg deps that appear in function signatures (Plan A only;
    // Plan B's AIOHeader already aggregates everything).
    void EmitPackageFunctionsCpp(
        const std::string &filePath,
        const UE_UPackage &pkg,
        size_t pkgIdx,
        size_t coreIdx,
        const std::string &headerInclude,
        bool emitCrossPkgIncludes,
        std::unordered_map<std::string, BufferFmt> &outBuffersMap);
};
