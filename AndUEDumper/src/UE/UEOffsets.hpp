#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#define kMAX_UENAME_BUFFER 0xff

struct UE_Offsets
{
    UE_Offsets()
    {
        memset(this, 0, sizeof(UE_Offsets));
    }
    struct
    {
        bool isUsingCasePreservingName = false;
        bool IsUsingFNamePool = false;
        bool isUsingOutlineNumberName = false;
    } Config;
    struct
    {
        uintptr_t ComparisonIndex = 0;
        uintptr_t DisplayIndex = 0;
        uintptr_t Number = 0;
        uintptr_t Size = 0;
    } FName;
    struct
    {
        uintptr_t Index = 0;
        uintptr_t Name = 0;
        std::function<bool(int32_t)> GetIsWide = nullptr;
    } FNameEntry;
    struct
    {
        uintptr_t Stride = 0;
        uintptr_t BlocksBit = 0;
        uintptr_t BlocksOff = 0;
    } FNamePool;
    struct
    {
        uintptr_t Header = 0;
        std::function<bool(uint16_t)> GetIsWide = nullptr;
        std::function<size_t(uint16_t)> GetLength = nullptr;
    } FNamePoolEntry;
    struct
    {
        uintptr_t ObjObjects = 0;
    } FUObjectArray;
    struct
    {
        uintptr_t Objects = 0;
        uintptr_t NumElements = 0;
        uintptr_t NumElementsPerChunk = 0;
    } TUObjectArray;
    struct
    {
        uintptr_t Object = 0;
        uintptr_t Size = 0;
    } FUObjectItem;
    struct
    {
        uintptr_t ObjectFlags = 0;
        uintptr_t InternalIndex = 0;
        uintptr_t ClassPrivate = 0;
        uintptr_t NamePrivate = 0;
        uintptr_t OuterPrivate = 0;
    } UObject;
    struct
    {
        uintptr_t Next = 0;
    } UField;
    struct
    {
        uintptr_t Names = 0;
    } UEnum;
    struct
    {
        uintptr_t SuperStruct = 0;
        uintptr_t Children = 0;
        uintptr_t ChildProperties = 0;
        uintptr_t PropertiesSize = 0;
    } UStruct;
    struct
    {
        uintptr_t EFunctionFlags = 0;
        uintptr_t NumParams = 0;
        uintptr_t ParamSize = 0;
        uintptr_t Func = 0;
    } UFunction;
    struct
    {
        uintptr_t ArrayDim = 0;
        uintptr_t ElementSize = 0;
        uintptr_t PropertyFlags = 0;
        uintptr_t Offset_Internal = 0;
        uintptr_t Size = 0;
        uintptr_t ArrayInner = 0;
    } UProperty;
    struct
    {
        uintptr_t ClassPrivate = 0;
        uintptr_t Next = 0;
        uintptr_t NamePrivate = 0;
        uintptr_t FlagsPrivate = 0;
    } FField;
    struct
    {
        uintptr_t ArrayDim = 0;
        uintptr_t ElementSize = 0;
        uintptr_t PropertyFlags = 0;
        uintptr_t Offset_Internal = 0;
        uintptr_t Size = 0;
        uintptr_t ArrayInner = 0;
    } FProperty;

    std::string ToString() const;
};

struct UE_Pointers
{
    UE_Pointers()
    {
        memset(this, 0, sizeof(UE_Pointers));
    }

    uintptr_t Names = 0;
    uintptr_t UObjectArray = 0;
    uintptr_t ObjObjects = 0;
    uintptr_t Engine = 0;
    uintptr_t World = 0;
    uintptr_t ProcessEvent = 0;
    uintptr_t ProcessEventIndex = 0;

    std::string ToString() const;
};

namespace UE_DefaultOffsets
{
    inline static uintptr_t kGetFNameSize(bool bWITH_CASE_PRESERVING_NAME, bool bFNAME_OUTLINE_NUMBER)
    {
        int32_t fNameSize = bFNAME_OUTLINE_NUMBER ? sizeof(int32_t) : (sizeof(int32_t) * 2);
        if (bWITH_CASE_PRESERVING_NAME)
        {
            fNameSize += sizeof(int32_t);
        }
        return fNameSize;
    }

    // UE 4.0 up to 4.17
    UE_Offsets UE4_00_17(bool bWITH_CASE_PRESERVING_NAME);

    // UE 4.18 up to 4.19
    UE_Offsets UE4_18_19(bool bWITH_CASE_PRESERVING_NAME);

    UE_Offsets UE4_20(bool bWITH_CASE_PRESERVING_NAME);

    UE_Offsets UE4_21(bool bWITH_CASE_PRESERVING_NAME);

    // UE 4.22
    UE_Offsets UE4_22(bool bWITH_CASE_PRESERVING_NAME);

    // UE 4.23 ~ 4.24
    UE_Offsets UE4_23_24(bool bWITH_CASE_PRESERVING_NAME);

    // UE 4.25 ~ 4.27
    UE_Offsets UE4_25_27(bool bWITH_CASE_PRESERVING_NAME);

    // UE 5.00 ~ 5.02
    UE_Offsets UE5_00_02(bool bWITH_CASE_PRESERVING_NAME, bool bFNAME_OUTLINE_NUMBER);

    // UE 5.03 and above
    UE_Offsets UE5_03(bool bWITH_CASE_PRESERVING_NAME, bool bFNAME_OUTLINE_NUMBER);
}  // namespace UE_DefaultOffsets

enum class UEVarsInitStatus : uint8_t
{
    NONE = 0,
    SUCCESS,
    ERROR_INVALID_ELF,
    ARCH_NOT_SUPPORTED,
    ERROR_ARCH_MISMATCH,
    ERROR_LIB_INVALID_BASE,
    ERROR_LIB_NOT_FOUND,
    ERROR_IO_OPERATION,
    ERROR_INIT_GNAMES,
    ERROR_INIT_NAMEPOOL,
    ERROR_INIT_GUOBJECTARRAY,
    ERROR_INIT_OBJOBJECTS,
    ERROR_INIT_OFFSETS,
    ERROR_INIT_PTR_VALIDATOR,
};

struct UEVars
{
    friend class IGameProfile;

protected:
    uintptr_t BaseAddress;
    uintptr_t NamesPtr;
    uintptr_t GUObjectsArrayPtr;
    uintptr_t ObjObjectsPtr;
    uintptr_t ObjObjects_Objects;

    UE_Offsets* Offsets;

    std::function<std::string(int32_t)> pGetNameByID;

public:
    UEVars() : BaseAddress(0), NamesPtr(0), GUObjectsArrayPtr(0), ObjObjectsPtr(0), ObjObjects_Objects(0), Offsets(nullptr), pGetNameByID(nullptr)
    {
    }

    UEVars(uintptr_t base, uintptr_t names, uintptr_t objectArray, uintptr_t objObjects, uintptr_t objects, UE_Offsets* offsets, const std::function<std::string(int32_t)>& pGetNameByID) : BaseAddress(base), NamesPtr(names), GUObjectsArrayPtr(objectArray), ObjObjectsPtr(objObjects), ObjObjects_Objects(objects), Offsets(offsets), pGetNameByID(pGetNameByID)
    {
    }

    uintptr_t GetBaseAddress() const { return BaseAddress; };
    uintptr_t GetNamesPtr() const { return NamesPtr; };
    uintptr_t GetGUObjectsArrayPtr() const { return GUObjectsArrayPtr; };
    uintptr_t GetObjObjectsPtr() const { return ObjObjectsPtr; };
    uintptr_t GetObjObjects_Objects() const { return ObjObjects_Objects; };

    UE_Offsets* GetOffsets() const { return Offsets; };

    std::string GetNameByID(int32_t id) const;

    static std::string InitStatusToStr(UEVarsInitStatus s);
};
