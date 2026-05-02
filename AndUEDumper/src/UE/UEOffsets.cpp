#include "UEOffsets.hpp"

#include <ostream>
#include <sstream>
#include <unordered_map>

#include "UEMemory.hpp"
using namespace UEMemory;

#define kOUT_NEWLINE() oss << std::endl

#define kOUT_NS_BEGIN(n) oss << "namespace " #n "\n{"
#define kOUT_NS_END() oss << "\n}"

#define kOUT_NS_MEMBER_B(n, m) oss << "\n    constexpr bool " #m << " = " << ((n.m) ? "true" : "false") << ";"
#define kOUT_NS_MEMBER_I(n, m) oss << "\n    constexpr int32_t " #m << " = " << n.m << ";"
#define kOUT_NS_MEMBER_P(n, m) oss << "\n    constexpr uintptr_t " #m << " = " << (void *)(uintptr_t(n.m)) << ";"

std::string UE_Offsets::ToString() const
{
    std::ostringstream oss;

    kOUT_NS_BEGIN(UEOffsets);
    {
        kOUT_NEWLINE();
        kOUT_NEWLINE();

        kOUT_NS_BEGIN(Config);
        {
            kOUT_NS_MEMBER_B(Config, isUsingCasePreservingName);
            kOUT_NS_MEMBER_B(Config, IsUsingFNamePool);
            kOUT_NS_MEMBER_B(Config, isUsingOutlineNumberName);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FName);
        {
            kOUT_NS_MEMBER_P(FName, ComparisonIndex);
            kOUT_NS_MEMBER_P(FName, Number);
            kOUT_NS_MEMBER_P(FName, DisplayIndex);
            kOUT_NS_MEMBER_P(FName, Size);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FNameEntry);
        {
            kOUT_NS_MEMBER_P(FNameEntry, Index);
            kOUT_NS_MEMBER_P(FNameEntry, Name);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FNamePool);
        {
            kOUT_NS_MEMBER_I(FNamePool, Stride);
            kOUT_NS_MEMBER_I(FNamePool, BlocksBit);
            kOUT_NS_MEMBER_P(FNamePool, BlocksOff);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FNamePoolEntry);
        {
            kOUT_NS_MEMBER_P(FNamePoolEntry, Header);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FUObjectArray);
        {
            kOUT_NS_MEMBER_P(FUObjectArray, ObjObjects);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(TUObjectArray);
        {
            kOUT_NS_MEMBER_P(TUObjectArray, Objects);
            kOUT_NS_MEMBER_P(TUObjectArray, NumElements);
            kOUT_NS_MEMBER_P(TUObjectArray, NumElementsPerChunk);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FUObjectItem);
        {
            kOUT_NS_MEMBER_P(FUObjectItem, Object);
            kOUT_NS_MEMBER_P(FUObjectItem, Size);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UObject);
        {
            kOUT_NS_MEMBER_P(UObject, ObjectFlags);
            kOUT_NS_MEMBER_P(UObject, InternalIndex);
            kOUT_NS_MEMBER_P(UObject, ClassPrivate);
            kOUT_NS_MEMBER_P(UObject, NamePrivate);
            kOUT_NS_MEMBER_P(UObject, OuterPrivate);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UField);
        {
            kOUT_NS_MEMBER_P(UField, Next);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UEnum);
        {
            kOUT_NS_MEMBER_P(UEnum, Names);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UStruct);
        {
            kOUT_NS_MEMBER_P(UStruct, SuperStruct);
            kOUT_NS_MEMBER_P(UStruct, Children);
            kOUT_NS_MEMBER_P(UStruct, ChildProperties);
            kOUT_NS_MEMBER_P(UStruct, PropertiesSize);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UClass);
        {
            kOUT_NS_MEMBER_P(UClass, CastFlags);
            kOUT_NS_MEMBER_P(UClass, DefaultObject);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UFunction);
        {
            kOUT_NS_MEMBER_P(UFunction, EFunctionFlags);
            kOUT_NS_MEMBER_P(UFunction, NumParams);
            kOUT_NS_MEMBER_P(UFunction, ParamSize);
            kOUT_NS_MEMBER_P(UFunction, Func);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(UProperty);
        {
            kOUT_NS_MEMBER_P(UProperty, ArrayDim);
            kOUT_NS_MEMBER_P(UProperty, ElementSize);
            kOUT_NS_MEMBER_P(UProperty, PropertyFlags);
            kOUT_NS_MEMBER_P(UProperty, Offset_Internal);
            kOUT_NS_MEMBER_P(UProperty, Size);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FField);
        {
            kOUT_NS_MEMBER_P(FField, ClassPrivate);
            kOUT_NS_MEMBER_P(FField, Next);
            kOUT_NS_MEMBER_P(FField, NamePrivate);
            kOUT_NS_MEMBER_P(FField, FlagsPrivate);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_BEGIN(FProperty);
        {
            kOUT_NS_MEMBER_P(FProperty, ArrayDim);
            kOUT_NS_MEMBER_P(FProperty, ElementSize);
            kOUT_NS_MEMBER_P(FProperty, PropertyFlags);
            kOUT_NS_MEMBER_P(FProperty, Offset_Internal);
            kOUT_NS_MEMBER_P(FProperty, Size);
            kOUT_NS_END();
            kOUT_NEWLINE();
            kOUT_NEWLINE();
        }

        kOUT_NS_END();
    }

    return oss.str();
}

std::string UE_Pointers::ToString() const
{
    std::ostringstream oss;

    kOUT_NS_BEGIN(UEPointers);
    {
        kOUT_NS_MEMBER_P((*this), Names);
        kOUT_NS_MEMBER_P((*this), UObjectArray);
        kOUT_NS_MEMBER_P((*this), ObjObjects);
        kOUT_NS_MEMBER_P((*this), Engine);
        kOUT_NS_MEMBER_P((*this), World);
        kOUT_NS_MEMBER_P((*this), ProcessEvent);
        kOUT_NS_MEMBER_I((*this), ProcessEventIndex);

        kOUT_NS_END();
    }

    return oss.str();
}

namespace UE_DefaultOffsets
{
    UE_Offsets UE4_00_17(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets{};
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.Config.isUsingCasePreservingName = bWITH_CASE_PRESERVING_NAME;
            offsets.Config.IsUsingFNamePool = false;
            offsets.Config.isUsingOutlineNumberName = false;

            offsets.FName.ComparisonIndex = 0;
            offsets.FName.DisplayIndex = bWITH_CASE_PRESERVING_NAME ? 4 : 0;
            // WITH_CASE_PRESERVING_NAME adds DisplayIndex in FName
            offsets.FName.Number = offsets.FName.DisplayIndex + sizeof(int32_t);
            offsets.FName.Size = kGetFNameSize(bWITH_CASE_PRESERVING_NAME, false);

            offsets.FNameEntry.Index = 0;
            offsets.FNameEntry.Name = GetPtrAlignedOf(sizeof(void *) + sizeof(int32_t));
            offsets.FNameEntry.GetIsWide = [](int32_t index)
            { return (index & 1) != 0; };

            offsets.FUObjectArray.ObjObjects = sizeof(int32_t) * 4;

            offsets.TUObjectArray.Objects = 0;
            offsets.TUObjectArray.NumElements = sizeof(void *) + sizeof(int32_t);
            offsets.TUObjectArray.NumElementsPerChunk = 0;

            offsets.FUObjectItem.Object = 0;
            offsets.FUObjectItem.Size = GetPtrAlignedOf(sizeof(void *) + (sizeof(int32_t) * 3));

            offsets.UObject.ObjectFlags = sizeof(void *);
            offsets.UObject.InternalIndex = offsets.UObject.ObjectFlags + sizeof(int32_t);
            offsets.UObject.ClassPrivate = offsets.UObject.InternalIndex + sizeof(int32_t);
            offsets.UObject.NamePrivate = offsets.UObject.ClassPrivate + sizeof(void *);
            offsets.UObject.OuterPrivate = GetPtrAlignedOf(offsets.UObject.NamePrivate + offsets.FName.Size);

            offsets.UField.Next = offsets.UObject.OuterPrivate + sizeof(void *);  // sizeof(UObject)

            offsets.UEnum.Names = offsets.UField.Next + (sizeof(void *) * 2) + (sizeof(int32_t) * 2);  // usually at sizeof(UField) + sizeof(FString)

            offsets.UStruct.SuperStruct = offsets.UField.Next + sizeof(void *);       // sizeof(UField)
            offsets.UStruct.Children = offsets.UStruct.SuperStruct + sizeof(void *);  // UField*
            offsets.UStruct.PropertiesSize = offsets.UStruct.Children + sizeof(void *);

            // UFunction.EFunctionFlags = sizeof(UStruct)
            offsets.UFunction.EFunctionFlags = offsets.UStruct.PropertiesSize + (sizeof(int32_t) * 2) + ((sizeof(void *) + sizeof(int32_t) * 2) * 2) + (sizeof(void *) * 4);
            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t) + sizeof(int16_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);
            offsets.UFunction.Func = offsets.UFunction.EFunctionFlags + (sizeof(int32_t) * 4) + (sizeof(void *) * 3);

            offsets.UProperty.ArrayDim = offsets.UField.Next + sizeof(void *);  // sizeof(UField)
            offsets.UProperty.ElementSize = offsets.UProperty.ArrayDim + sizeof(int32_t);
            offsets.UProperty.PropertyFlags = GetPtrAlignedOf(offsets.UProperty.ElementSize + sizeof(int32_t));
            offsets.UProperty.Offset_Internal = offsets.UProperty.PropertyFlags + sizeof(int64_t) + (sizeof(int32_t) * 2) + offsets.FName.Size;
            offsets.UProperty.Size = GetPtrAlignedOf(offsets.UProperty.Offset_Internal + sizeof(int32_t)) + (sizeof(void *) * 4);  // sizeof(UProperty)
        }
        return offsets;
    }

    UE_Offsets UE4_18_19(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets = UE4_00_17(bWITH_CASE_PRESERVING_NAME);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);

            offsets.UProperty.Offset_Internal = offsets.UProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.UProperty.Size = GetPtrAlignedOf(offsets.UProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(UProperty)
        }
        return offsets;
    }

    UE_Offsets UE4_20(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets = UE4_18_19(bWITH_CASE_PRESERVING_NAME);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.TUObjectArray.NumElements = (sizeof(void *) * 2) + sizeof(int32_t);
            offsets.TUObjectArray.NumElementsPerChunk = 65 * 1024;
        }
        return offsets;
    }

    UE_Offsets UE4_21(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets = UE4_20(bWITH_CASE_PRESERVING_NAME);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.TUObjectArray.NumElementsPerChunk = 64 * 1024;
        }
        return offsets;
    }

    UE_Offsets UE4_22(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets = UE4_21(bWITH_CASE_PRESERVING_NAME);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.FNameEntry.Index = sizeof(void *);
            offsets.FNameEntry.Name = sizeof(void *) + sizeof(int32_t);

            offsets.UStruct.SuperStruct = offsets.UField.Next + (sizeof(void *) * 3);  // sizeof(UField) + sizeof(FStructBaseChain)
            offsets.UStruct.Children = offsets.UStruct.SuperStruct + sizeof(void *);   // UField*
            offsets.UStruct.PropertiesSize = offsets.UStruct.Children + sizeof(void *);

            offsets.UFunction.EFunctionFlags = offsets.UStruct.PropertiesSize + (sizeof(int32_t) * 2) + ((sizeof(void *) + sizeof(int32_t) * 2) * 2) + (sizeof(void *) * 4);
            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);
            offsets.UFunction.Func = offsets.UFunction.EFunctionFlags + (sizeof(int32_t) * 4) + (sizeof(void *) * 3);

            offsets.UProperty.ArrayDim = offsets.UField.Next + sizeof(void *);  // sizeof(UField)
            offsets.UProperty.ElementSize = offsets.UProperty.ArrayDim + sizeof(int32_t);
            offsets.UProperty.PropertyFlags = GetPtrAlignedOf(offsets.UProperty.ElementSize + sizeof(int32_t));
            offsets.UProperty.Offset_Internal = offsets.UProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.UProperty.Size = GetPtrAlignedOf(offsets.UProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(UProperty)
        }
        return offsets;
    }

    UE_Offsets UE4_23_24(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets{};
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.Config.isUsingCasePreservingName = bWITH_CASE_PRESERVING_NAME;
            offsets.Config.IsUsingFNamePool = true;
            offsets.Config.isUsingOutlineNumberName = false;

            offsets.FName.ComparisonIndex = 0;
            offsets.FName.DisplayIndex = bWITH_CASE_PRESERVING_NAME ? 4 : 0;
            // WITH_CASE_PRESERVING_NAME adds DisplayIndex in FName
            offsets.FName.Number = offsets.FName.DisplayIndex + sizeof(int32_t);
            offsets.FName.Size = kGetFNameSize(bWITH_CASE_PRESERVING_NAME, false);

            offsets.FNamePool.Stride = bWITH_CASE_PRESERVING_NAME ? 4 : 2;  // alignof(FNameEntry)

            // Blocks bit is 16 for most games
            // ((id >> 13) & 0x7FFF8) = 16
            // ((id >> 15) & 0x1FFF8) = 18
            offsets.FNamePool.BlocksBit = 16;

            // offset to blocks, usually ios at 0xD0 and android at 0x40
#ifdef __APPLE__
            offsets.FNamePool.BlocksOff = 0xD0;
#else
#ifdef __LP64__
            offsets.FNamePool.BlocksOff = 0x40;
#else
            offsets.FNamePool.BlocksOff = 0x30;
#endif
#endif

            offsets.FNamePoolEntry.Header = bWITH_CASE_PRESERVING_NAME ? 4 : 0;  // Offset to name entry header
            offsets.FNamePoolEntry.GetIsWide = [](uint16_t header)
            { return (header & 1) != 0; };
            // usually if stride is 2 then header >> 6 and if 4 then haeder >> 1
            offsets.FNamePoolEntry.GetLength = [bWITH_CASE_PRESERVING_NAME](uint16_t header) -> size_t
            {
                return bWITH_CASE_PRESERVING_NAME ? header >> 1 : header >> 6;
            };

            offsets.FUObjectArray.ObjObjects = sizeof(int32_t) * 4;

            offsets.TUObjectArray.Objects = 0;
            offsets.TUObjectArray.NumElements = (sizeof(void *) * 2) + sizeof(int32_t);
            offsets.TUObjectArray.NumElementsPerChunk = 64 * 1024;

            offsets.FUObjectItem.Object = 0;
            offsets.FUObjectItem.Size = GetPtrAlignedOf(sizeof(void *) + (sizeof(int32_t) * 3));

            offsets.UObject.ObjectFlags = sizeof(void *);
            offsets.UObject.InternalIndex = offsets.UObject.ObjectFlags + sizeof(int32_t);
            offsets.UObject.ClassPrivate = offsets.UObject.InternalIndex + sizeof(int32_t);
            offsets.UObject.NamePrivate = offsets.UObject.ClassPrivate + sizeof(void *);
            offsets.UObject.OuterPrivate = GetPtrAlignedOf(offsets.UObject.NamePrivate + offsets.FName.Size);

            offsets.UField.Next = offsets.UObject.OuterPrivate + sizeof(void *);  // sizeof(UObject)

            offsets.UEnum.Names = offsets.UField.Next + (sizeof(void *) * 2) + (sizeof(int32_t) * 2);  // usually at sizeof(UField) + sizeof(FString)

            offsets.UStruct.SuperStruct = offsets.UField.Next + (sizeof(void *) * 3);  // sizeof(UField) + sizeof(FStructBaseChain)
            offsets.UStruct.Children = offsets.UStruct.SuperStruct + sizeof(void *);   // UField*
            offsets.UStruct.PropertiesSize = offsets.UStruct.Children + sizeof(void *);

            offsets.UFunction.EFunctionFlags = offsets.UStruct.PropertiesSize + (sizeof(int32_t) * 2) + ((sizeof(void *) + sizeof(int32_t) * 2) * 2) + (sizeof(void *) * 4);
            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);
            offsets.UFunction.Func = offsets.UFunction.EFunctionFlags + (sizeof(int32_t) * 4) + (sizeof(void *) * 3);

            offsets.UProperty.ArrayDim = offsets.UField.Next + sizeof(void *);  // sizeof(UField)
            offsets.UProperty.ElementSize = offsets.UProperty.ArrayDim + sizeof(int32_t);
            offsets.UProperty.PropertyFlags = GetPtrAlignedOf(offsets.UProperty.ElementSize + sizeof(int32_t));
            offsets.UProperty.Offset_Internal = offsets.UProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.UProperty.Size = GetPtrAlignedOf(offsets.UProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(UProperty)
        }
        return offsets;
    }

    UE_Offsets UE4_25_27(bool bWITH_CASE_PRESERVING_NAME)
    {
        static UE_Offsets offsets = UE4_23_24(bWITH_CASE_PRESERVING_NAME);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.UStruct.ChildProperties = offsets.UStruct.Children + sizeof(void *);  // FField*
            offsets.UStruct.PropertiesSize = offsets.UStruct.ChildProperties + sizeof(void *);

            // UE 4.25-5.x default UClass layout (Dumper-7 reference): UStruct
            // (~0xB0) + 0x28 pad → CastFlags @ 0xD8, then 0x58 pad →
            // DefaultObject @ 0x138. Per-game profile may override.
            offsets.UClass.CastFlags = 0xD8;
            offsets.UClass.DefaultObject = 0x138;

            offsets.UFunction.EFunctionFlags = offsets.UStruct.PropertiesSize + (sizeof(int32_t) * 2) + ((sizeof(void *) + sizeof(int32_t) * 2) * 2) + (sizeof(void *) * 6);
            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);
            offsets.UFunction.Func = offsets.UFunction.EFunctionFlags + (sizeof(int32_t) * 4) + (sizeof(void *) * 3);

            offsets.FField.ClassPrivate = sizeof(void *);
            offsets.FField.Next = offsets.FField.ClassPrivate + (sizeof(void *) * 3);  // + sizeof(FFieldVariant);
            offsets.FField.NamePrivate = offsets.FField.Next + sizeof(void *);
            offsets.FField.FlagsPrivate = offsets.FField.NamePrivate + offsets.FName.Size;

            offsets.FProperty.ArrayDim = offsets.FField.FlagsPrivate + sizeof(int32_t);  // sizeof(UFField)
            offsets.FProperty.ElementSize = offsets.FProperty.ArrayDim + sizeof(int32_t);
            offsets.FProperty.PropertyFlags = GetPtrAlignedOf(offsets.FProperty.ElementSize + sizeof(int32_t));
            offsets.FProperty.Offset_Internal = offsets.FProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.FProperty.Size = GetPtrAlignedOf(offsets.FProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(FProperty)

            offsets.UProperty.ArrayDim = 0;
            offsets.UProperty.ElementSize = 0;
            offsets.UProperty.PropertyFlags = 0;
            offsets.UProperty.Offset_Internal = 0;
            offsets.UProperty.Size = 0;
        }
        return offsets;
    }

    UE_Offsets UE5_00_02(bool bWITH_CASE_PRESERVING_NAME, bool bFNAME_OUTLINE_NUMBER)
    {
        static UE_Offsets offsets{};
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.Config.isUsingCasePreservingName = bWITH_CASE_PRESERVING_NAME;
            offsets.Config.IsUsingFNamePool = true;
            offsets.Config.isUsingOutlineNumberName = bFNAME_OUTLINE_NUMBER;

            offsets.FName.ComparisonIndex = 0;
            offsets.FName.Number = bFNAME_OUTLINE_NUMBER ? 0 : 4;
            offsets.FName.DisplayIndex = bWITH_CASE_PRESERVING_NAME ? (offsets.FName.Number + sizeof(int32_t)) : 0;
            offsets.FName.Size = kGetFNameSize(bWITH_CASE_PRESERVING_NAME, bFNAME_OUTLINE_NUMBER);

            offsets.FNamePool.Stride = bWITH_CASE_PRESERVING_NAME ? 4 : 2;  // alignof(FNameEntry)
            // Blocks bit is 16 for most games
            // ((id >> 13) & 0x7FFF8) = 16
            // ((id >> 15) & 0x1FFF8) = 18
            offsets.FNamePool.BlocksBit = 16;

            // offset to blocks, usually ios at 0xD0 and android at 0x40
#ifdef __APPLE__
            offsets.FNamePool.BlocksOff = 0xD0;
#else
#ifdef __LP64__
            offsets.FNamePool.BlocksOff = 0x40;
#else
            offsets.FNamePool.BlocksOff = 0x30;
#endif
#endif

            offsets.FNamePoolEntry.Header = bWITH_CASE_PRESERVING_NAME ? 4 : 0;  // Offset to name entry header
            offsets.FNamePoolEntry.GetIsWide = [](uint16_t header)
            { return (header & 1) != 0; };
            // usually if stride is 2 then header >> 6 and if 4 then haeder >> 1
            offsets.FNamePoolEntry.GetLength = [bWITH_CASE_PRESERVING_NAME](uint16_t header) -> size_t
            {
                return bWITH_CASE_PRESERVING_NAME ? header >> 1 : header >> 6;
            };

            offsets.FUObjectArray.ObjObjects = sizeof(int32_t) * 4;

            offsets.TUObjectArray.Objects = 0;
            offsets.TUObjectArray.NumElements = (sizeof(void *) * 2) + sizeof(int32_t);
            offsets.TUObjectArray.NumElementsPerChunk = 64 * 1024;

            offsets.FUObjectItem.Object = 0;
            offsets.FUObjectItem.Size = GetPtrAlignedOf(sizeof(void *) + (sizeof(int32_t) * 3));

            offsets.UObject.ObjectFlags = sizeof(void *);
            offsets.UObject.InternalIndex = offsets.UObject.ObjectFlags + sizeof(int32_t);
            offsets.UObject.ClassPrivate = offsets.UObject.InternalIndex + sizeof(int32_t);
            offsets.UObject.NamePrivate = offsets.UObject.ClassPrivate + sizeof(void *);
            offsets.UObject.OuterPrivate = GetPtrAlignedOf(offsets.UObject.NamePrivate + offsets.FName.Size);

            offsets.UField.Next = offsets.UObject.OuterPrivate + sizeof(void *);  // sizeof(UObject)

            offsets.UEnum.Names = offsets.UField.Next + (sizeof(void *) * 2) + (sizeof(int32_t) * 2);  // usually at sizeof(UField) + sizeof(FString)

            offsets.UStruct.SuperStruct = offsets.UField.Next + (sizeof(void *) * 3);     // sizeof(UField) + sizeof(FStructBaseChain)
            offsets.UStruct.Children = offsets.UStruct.SuperStruct + sizeof(void *);      // UField*
            offsets.UStruct.ChildProperties = offsets.UStruct.Children + sizeof(void *);  // FField*
            offsets.UStruct.PropertiesSize = offsets.UStruct.ChildProperties + sizeof(void *);

            // UE5 default UClass layout (matches UE 4.25-5.x Dumper-7 reference).
            offsets.UClass.CastFlags = 0xD8;
            offsets.UClass.DefaultObject = 0x138;

            offsets.UFunction.EFunctionFlags = offsets.UStruct.PropertiesSize + (sizeof(int32_t) * 2) + ((sizeof(void *) + sizeof(int32_t) * 2) * 2) + (sizeof(void *) * 6);
            offsets.UFunction.NumParams = offsets.UFunction.EFunctionFlags + sizeof(int32_t);
            offsets.UFunction.ParamSize = offsets.UFunction.NumParams + sizeof(int16_t);
            offsets.UFunction.Func = offsets.UFunction.EFunctionFlags + (sizeof(int32_t) * 4) + (sizeof(void *) * 3);

            offsets.FField.ClassPrivate = sizeof(void *);
            offsets.FField.Next = offsets.FField.ClassPrivate + sizeof(void *) + GetPtrAlignedOf(sizeof(void *) + sizeof(bool));  // + sizeof(FFieldVariant);
            offsets.FField.NamePrivate = offsets.FField.Next + sizeof(void *);
            offsets.FField.FlagsPrivate = offsets.FField.NamePrivate + offsets.FName.Size;

            offsets.FProperty.ArrayDim = offsets.FField.FlagsPrivate + sizeof(int32_t);  // sizeof(UFField)
            offsets.FProperty.ElementSize = offsets.FProperty.ArrayDim + sizeof(int32_t);
            offsets.FProperty.PropertyFlags = GetPtrAlignedOf(offsets.FProperty.ElementSize + sizeof(int32_t));
            offsets.FProperty.Offset_Internal = offsets.FProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.FProperty.Size = GetPtrAlignedOf(offsets.FProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(FProperty)
        }
        return offsets;
    }

    UE_Offsets UE5_03(bool bWITH_CASE_PRESERVING_NAME, bool bFNAME_OUTLINE_NUMBER)
    {
        static UE_Offsets offsets = UE5_00_02(bWITH_CASE_PRESERVING_NAME, bFNAME_OUTLINE_NUMBER);
        static bool once = false;
        if (!once)
        {
            once = true;

            offsets.FField.Next = offsets.FField.ClassPrivate + (sizeof(void *) * 2);  // + sizeof(FFieldVariant);
            offsets.FField.NamePrivate = offsets.FField.Next + sizeof(void *);
            offsets.FField.FlagsPrivate = offsets.FField.NamePrivate + offsets.FName.Size;

            offsets.FProperty.ArrayDim = offsets.FField.FlagsPrivate + sizeof(int32_t);  // sizeof(UFField)
            offsets.FProperty.ElementSize = offsets.FProperty.ArrayDim + sizeof(int32_t);
            offsets.FProperty.PropertyFlags = GetPtrAlignedOf(offsets.FProperty.ElementSize + sizeof(int32_t));
            offsets.FProperty.Offset_Internal = offsets.FProperty.PropertyFlags + sizeof(int64_t) + sizeof(int32_t);
            offsets.FProperty.Size = GetPtrAlignedOf(offsets.FProperty.Offset_Internal + sizeof(int32_t) + offsets.FName.Size) + (sizeof(void *) * 4);  // sizeof(FProperty)
        }
        return offsets;
    }
}  // namespace UE_DefaultOffsets

std::string UEVars::GetNameByID(int32_t id) const
{
    static std::unordered_map<int32_t, std::string> namesCachedMap;
    if (namesCachedMap.count(id) > 0)
        return namesCachedMap[id];

    std::string name = pGetNameByID ? pGetNameByID(id) : "pGetNameByID_IS_NULL";
    if (!name.empty())
    {
        namesCachedMap[id] = name;
    }
    return name;
}

std::string UEVars::InitStatusToStr(UEVarsInitStatus s)
{
    switch (s)
    {
    case UEVarsInitStatus::NONE:
        return "NONE";
    case UEVarsInitStatus::SUCCESS:
        return "SUCCESS";
    case UEVarsInitStatus::ERROR_INVALID_ELF:
        return "ERROR_INVALID_ELF";
    case UEVarsInitStatus::ARCH_NOT_SUPPORTED:
        return "ERROR_ARCH_NOT_SUPPORTED";
    case UEVarsInitStatus::ERROR_ARCH_MISMATCH:
        return "ERROR_ARCH_MISMATCH";
    case UEVarsInitStatus::ERROR_LIB_INVALID_BASE:
        return "ERROR_LIB_INVALID_BASE";
    case UEVarsInitStatus::ERROR_LIB_NOT_FOUND:
        return "ERROR_LIB_NOT_FOUND";
    case UEVarsInitStatus::ERROR_IO_OPERATION:
        return "ERROR_IO_OPERATION";
    case UEVarsInitStatus::ERROR_INIT_GNAMES:
        return "ERROR_INIT_GNAMES";
    case UEVarsInitStatus::ERROR_INIT_NAMEPOOL:
        return "ERROR_INIT_NAMEPOOL";
    case UEVarsInitStatus::ERROR_INIT_GUOBJECTARRAY:
        return "ERROR_INIT_GUOBJECTARRAY";
    case UEVarsInitStatus::ERROR_INIT_OBJOBJECTS:
        return "ERROR_INIT_OBJOBJECTS";
    case UEVarsInitStatus::ERROR_INIT_OFFSETS:
        return "ERROR_INIT_OFFSETS";
    case UEVarsInitStatus::ERROR_INIT_PTR_VALIDATOR:
        return "ERROR_INIT_PTR_VALIDATOR";
    default:
        break;
    }
    return "UNKNOWN";
}
