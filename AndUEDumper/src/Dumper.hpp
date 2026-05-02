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

class UEDumper
{
    IGameProfile const *_profile;
    std::string _lastError;
    std::function<void(bool)> _dumpExeInfoNotify;
    std::function<void(bool)> _dumpNamesInfoNotify;
    std::function<void(bool)> _dumpObjectsInfoNotify;
    std::function<void(bool)> _dumpOffsetsInfoNotify;
    ProgressCallback _objectsProgressCallback;
    ProgressCallback _dumpProgressCallback;

    // Discovered by DumpOffsetsInfo and consumed by DumpAIOHeader to bake
    // the per-game ProcessEvent vtable slot into the AIOCore helper block.
    int _processEventIndex = 0;

public:
    UEDumper() : _profile(nullptr), _dumpExeInfoNotify(nullptr), _dumpNamesInfoNotify(nullptr), _dumpObjectsInfoNotify(nullptr), _objectsProgressCallback(nullptr), _dumpProgressCallback(nullptr) {}

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

    void DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback);
};
