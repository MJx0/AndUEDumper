#pragma once

#include "../UEGameProfile.hpp"
#include <cstdio>

using namespace UEMemory;

class SFG3Profile : public IGameProfile
{
public:
    SFG3Profile() = default;

    bool ArchSupprted() const override
    {
        auto e_machine = GetUnrealELF().header().e_machine;
        return e_machine == EM_AARCH64;
    }

    std::string GetAppName() const override
    {
        return "SFG3";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.ForgeGames.sfg3"};
    }

    bool isUsingCasePreservingName() const override
    {
        return false;
    }

    bool IsUsingFNamePool() const override
    {
        return true;
    }

    bool isUsingOutlineNumberName() const override
    {
        return false;
    }

    uintptr_t GetGUObjectArrayPtr() const override
    {
        return vm_rpm_ptr<uintptr_t>((void*)Arm64::DecodeADRL(findIdaPattern(PATTERN_MAP_TYPE::ANY_X, "? ? ? F9 ? ? ? 94 ? ? ? D1 ? ? ? 94 ? ? ? D1", -4)));
    }

    uintptr_t GetNamesPtr() const override
    {
        return Arm64::DecodeADRL(GetUnrealELF().findSymbol("_Zeq12FNameEntryId5EName"));
    }

    UE_Offsets* GetOffsets() const override
    {
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_25_27(isUsingCasePreservingName());

        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FUObjectArray.ObjObjects = 0;
            offsets.TUObjectArray.Objects = 0x10;
            offsets.TUObjectArray.NumElements = 0x24;

            // Custom SFG3 Property Layout
            offsets.FProperty.ArrayInner = 0x58;
        }

        return &offsets;
    }

    uint8_t* GetNameEntry(int32_t id) const override
    {
        uintptr_t namePool = _UEVars.GetNamesPtr();
        if (!namePool) return nullptr;

        auto get = [&](uint32_t idx)
        {
            auto& f = GetOffsets()->FNamePool;
            uintptr_t chunk = vm_rpm_ptr<uintptr_t>((void*)(namePool + f.BlocksOff + ((idx >> 16) * 8)));
            return chunk ? (uint8_t*)(chunk + (idx & 0xFFFF) * f.Stride) : nullptr;
        };
        uint8_t* entry = get(id);
        if (entry && (vm_rpm_ptr<uint16_t>(entry) >> 6) > 0)
            return entry;

        uint32_t sIdx = vm_rpm_ptr<uint32_t>((void*)(namePool + 0x18040 + (id * 4)));
        return (sIdx == 0 && id != 0) ? nullptr : get(sIdx);
    }
};
