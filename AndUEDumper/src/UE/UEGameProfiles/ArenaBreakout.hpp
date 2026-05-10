#pragma once

#include "../UEGameProfile.hpp"
using namespace UEMemory;

class ArenaBreakoutProfile : public IGameProfile
{
public:
    ArenaBreakoutProfile() = default;

    bool ArchSupprted() const override
    {
        auto e_machine = GetUnrealELF().header().e_machine;
        // only arm64
        return e_machine == EM_AARCH64;
    }

    std::string GetAppName() const override
    {
        return "Arena Breakout";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.proximabeta.mf.uamo"};
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
        std::vector<std::pair<std::string, int>> idaPatterns = {
            {"? ? ? 90 ? ? ? 52 09 01 09 0B ? ? ? 71 ? ? ? F9 ? ? ? 1A ? ? ? 13 ? ? ? 12 08 01 09 4B ? ? ? 52 ? ? ? F9 ? ? ? F8 1F 20 03 D5 08 29 29 9B ? ? ? 39 ? ? ? 37 F8 03 00 AA", 0},
            {"91 E1 03 ? AA E0 03 08 AA E2 03 1F 2A", -7},
            {"B4 21 0C 40 B9 ? ? ? ? ? ? ? 91", 5},
            {"9F E5 00 ? 00 E3 FF ? 40 E3 ? ? A0 E1", -2},
            {"96 df 02 17 ? ? ? ? 54 ? ? ? ? ? ? ? 91 e1 03 13 aa", 9},
            {"f4 03 01 2a ? 00 00 34 ? ? ? ? ? ? ? ? ? ? 00 54 ? 00 00 14 ? ? ? ? ? ? ? 91", 0x18},
            {"69 3e 40 b9 1f 01 09 6b ? ? ? 54 e1 03 13 aa ? ? ? ? f4 4f ? a9 ? ? ? ? ? ? ? 91", 0x18},
        };

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        for (const auto &it : idaPatterns)
        {
            std::string ida_pattern = it.first;
            const int step = it.second;

            uintptr_t adrl = Arm64::DecodeADRL(findIdaPattern(map_type, ida_pattern, step));
            if (adrl != 0) return adrl;
        }

        return 0;
    }

    uintptr_t GetNamesPtr() const override
    {
        std::string pattern = "38 1D 08 8B F7 03 01 AA ? ? ? F9 ? ? ? 91 ? ? ? 91 ? ? ? D3";

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        uintptr_t find = findIdaPattern(map_type, pattern, -12);

        if (find != 0)
        {
            uintptr_t result = Arm64::DecodeADRL(find, 0);
            return result;
        }

        return 0;
    }

    UE_Offsets *GetOffsets() const override
    {
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_25_27(isUsingCasePreservingName());

        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FNamePool.BlocksOff += sizeof(void *);

            offsets.UObject.ClassPrivate = 0x10;
            offsets.UObject.NamePrivate = 0x18;
            offsets.UObject.OuterPrivate = 0x20;
            offsets.UStruct.SuperStruct = 0x40;
            offsets.UStruct.Children = 0x48;
            offsets.UStruct.ChildProperties = 0x50;
            offsets.UStruct.PropertiesSize = 0x58;
            offsets.FField.Next = 0x20;
            offsets.FField.NamePrivate = 0x28;
            offsets.FField.FlagsPrivate = 0x40;
            offsets.FProperty.Offset_Internal = 0x4C;
            offsets.UFunction.Func = 0xD8;
            offsets.UEnum.Names = 0x40;
        }

        return &offsets;
    }
};
