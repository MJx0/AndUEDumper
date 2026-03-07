#include "UEWrappers.hpp"
using namespace UEMemory;

#include <hash/hash.h>

#include "UEGameProfile.hpp"

#include <utfcpp/unchecked.h>

namespace UEWrappers
{
    UEVars const* GUVars = nullptr;
    std::unique_ptr<UE_UObjectArray> pObjectsArray = nullptr;

    void Init(const UEVars* vars)
    {
        if (vars)
        {
            GUVars = vars;
            if (pObjectsArray.get())
            {
                pObjectsArray.reset();
            }
            pObjectsArray = std::make_unique<UE_UObjectArray>(vars->GetObjObjects_Objects());
        }
    }

    UEVars const* GetUEVars() { return GUVars; }
    uintptr_t GetBaseAddress() { return GUVars ? GUVars->GetBaseAddress() : 0; }
    UE_Offsets* GetOffsets() { return GUVars ? GUVars->GetOffsets() : nullptr; }
    std::string GetNameByID(int32_t id) { return GUVars ? GUVars->GetNameByID(id) : ""; }
    UE_UObjectArray* GetObjects() { return pObjectsArray.get(); }
}  // namespace UEWrappers

std::string FString::ToString() const
{
    if (!IsValid()) return "";

    std::wstring wstr = ToWString();
    if (wstr.empty()) return "";

    std::string result;
    utf8::unchecked::utf16to8(wstr.begin(), wstr.end(), std::back_inserter(result));
    return result;
}

int32_t UE_UObjectArray::GetNumElements() const
{
    if (UEWrappers::GUVars->GetObjObjectsPtr() == 0)
        return 0;

    return vm_rpm_ptr<int32_t>((void*)(UEWrappers::GUVars->GetObjObjectsPtr() + UEWrappers::GetOffsets()->TUObjectArray.NumElements));
}

uint8_t* UE_UObjectArray::GetObjectPtr(int32_t id) const
{
    if (id < 0 || id >= GetNumElements() || !Objects)
        return nullptr;

    if (UEWrappers::GetOffsets()->TUObjectArray.NumElementsPerChunk <= 0)
    {
        return vm_rpm_ptr<uint8_t*>((void*)((uintptr_t)Objects + (id * UEWrappers::GetOffsets()->FUObjectItem.Size) + UEWrappers::GetOffsets()->FUObjectItem.Object));
    }

    const int32_t NumElementsPerChunk = UEWrappers::GetOffsets()->TUObjectArray.NumElementsPerChunk;
    const int32_t chunkIndex = id / NumElementsPerChunk;
    const int32_t withinChunkIndex = id % NumElementsPerChunk;

    // if (chunkIndex >= NumChunks) return nullptr;

    uint8_t* chunk = vm_rpm_ptr<uint8_t*>(Objects + chunkIndex);
    if (!chunk)
        return nullptr;

    return vm_rpm_ptr<uint8_t*>(chunk + (withinChunkIndex * UEWrappers::GetOffsets()->FUObjectItem.Size) + UEWrappers::GetOffsets()->FUObjectItem.Object);
}

void UE_UObjectArray::ForEachObject(const std::function<bool(UE_UObject)>& callback) const
{
    if (!callback) return;

    for (int32_t i = 0; i < GetNumElements(); i++)
    {
        uint8_t* object = GetObjectPtr(i);
        if (!object) continue;

        if (callback(object)) return;
    }
}

void UE_UObjectArray::ForEachObjectOfClass(const UE_UClass& cmp, const std::function<bool(UE_UObject)>& callback) const
{
    if (!cmp || !callback) return;

    for (int32_t i = 0; i < GetNumElements(); i++)
    {
        UE_UObject object = GetObjectPtr(i);
        if (object && object.IsA(cmp))
        {
            if (callback(object)) return;
        }
    }
}

bool UE_UObjectArray::IsObject(const UE_UObject& address) const
{
    for (int32_t i = 0; i < GetNumElements(); i++)
    {
        UE_UObject object = GetObjectPtr(i);
        if (address == object) return true;
    }
    return false;
}

int UE_FName::GetNumber() const
{
    if (!object || UEWrappers::GetOffsets()->Config.isUsingOutlineNumberName)
        return 0;

    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->FName.Number);
}

std::string UE_FName::GetName() const
{
    if (!object) return "None";

    uintptr_t nameID_offset = UEWrappers::GetOffsets()->FName.ComparisonIndex;
    // if (UEWrappers::GetOffsets()->isUsingCasePreservingName)
    //   nameID_offset = UEWrappers::GetOffsets()->FName.DisplayIndex;

    int32_t index = 0;
    if (!vm_rpm_ptr(object + nameID_offset, &index, sizeof(int32_t)) || index < 0)
        return "None";

    std::string name = UEWrappers::GetNameByID(index);
    if (name.empty()) return "None";

    if (!UEWrappers::GetOffsets()->Config.isUsingOutlineNumberName)
    {
        int32_t number = GetNumber();
        if (number > 0)
        {
            name += '_' + std::to_string(number - 1);
        }
    }

    auto pos = name.rfind('/');
    if (pos != std::string::npos)
    {
        name = name.substr(pos + 1);
    }

    return name;
}

EObjectFlags UE_UObject::GetFlags() const
{
    if (!object) return EObjectFlags::NoFlags;

    return vm_rpm_ptr<EObjectFlags>(object + UEWrappers::GetOffsets()->UObject.ObjectFlags);
}

int32_t UE_UObject::GetIndex() const
{
    if (!object) return -1;

    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->UObject.InternalIndex);
}

UE_UClass UE_UObject::GetClass() const
{
    if (!object) return nullptr;

    return vm_rpm_ptr<UE_UClass>(object + UEWrappers::GetOffsets()->UObject.ClassPrivate);
}

UE_UObject UE_UObject::GetOuter() const
{
    if (!object) return nullptr;

    return vm_rpm_ptr<UE_UObject>(object + UEWrappers::GetOffsets()->UObject.OuterPrivate);
}

UE_UObject UE_UObject::GetPackageObject() const
{
    if (!object) return nullptr;

    UE_UObject package(nullptr);
    for (auto outer = GetOuter(); outer; outer = outer.GetOuter())
    {
        package = outer;
    }
    return package;
}

std::string UE_UObject::GetName() const
{
    if (!object) return "";

    auto fname = UE_FName(object + UEWrappers::GetOffsets()->UObject.NamePrivate);
    return fname.GetName();
}

std::string UE_UObject::GetFullName() const
{
    if (!object) return "";

    std::string temp;
    for (auto outer = GetOuter(); outer; outer = outer.GetOuter())
    {
        temp = outer.GetName() + "." + temp;
    }
    UE_UClass objectClass = GetClass();
    std::string name = objectClass.GetName() + " " + temp + GetName();
    return name;
}

std::string UE_UObject::GetCppName() const
{
    if (!object) return "";

    std::string name;
    if (IsA<UE_UClass>())
    {
        for (auto c = Cast<UE_UStruct>(); c; c = c.GetSuper())
        {
            if (c == UE_AActor::StaticClass())
            {
                name = "A";
                break;
            }
            else if (c == UE_UObject::StaticClass())
            {
                name = "U";
                break;
            }
            else if (c == UE_UInterface::StaticClass())
            {
                name = "I";
                break;
            }
        }
    }
    else
    {
        name = "F";
    }

    name += GetName();
    return name;
}

bool UE_UObject::IsA(UE_UClass cmp) const
{
    if (!object) return false;

    for (auto super = GetClass(); super; super = super.GetSuper().Cast<UE_UClass>())
    {
        if (super == cmp)
        {
            return true;
        }
    }

    return false;
}

bool UE_UObject::HasFlags(EObjectFlags flags) const
{
    return (GetFlags() & flags) == flags;
}

UE_UClass UE_UObject::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Object");
    return obj;
}

UE_UClass UE_UInterface::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Interface");
    return obj;
}

UE_UClass UE_AActor::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class Engine.Actor");
    return obj;
}

UE_UField UE_UField::GetNext() const
{
    if (!object)
        return nullptr;

    return vm_rpm_ptr<UE_UField>(object + UEWrappers::GetOffsets()->UField.Next);
}

UE_UClass UE_UField::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Field");
    return obj;
}

std::string IUProperty::GetName() const
{
    return ((UE_UProperty*)(this->prop))->GetName();
}

int32_t IUProperty::GetArrayDim() const
{
    return ((UE_UProperty*)(this->prop))->GetArrayDim();
}

int32_t IUProperty::GetSize() const
{
    return ((UE_UProperty*)(this->prop))->GetSize();
}

int32_t IUProperty::GetOffset() const
{
    return ((UE_UProperty*)(this->prop))->GetOffset();
}

uint64_t IUProperty::GetPropertyFlags() const
{
    return ((UE_UProperty*)(this->prop))->GetPropertyFlags();
}

std::pair<UEPropertyType, std::string> IUProperty::GetType() const
{
    return ((UE_UProperty*)(this->prop))->GetType();
}

uint8_t IUProperty::GetFieldMask() const
{
    return ((UE_UBoolProperty*)(this->prop))->GetFieldMask();
}

int32_t UE_UProperty::GetArrayDim() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->UProperty.ArrayDim);
}

int32_t UE_UProperty::GetSize() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->UProperty.ElementSize);
}

int32_t UE_UProperty::GetOffset() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->UProperty.Offset_Internal);
}

uint64_t UE_UProperty::GetPropertyFlags() const
{
    return vm_rpm_ptr<uint64_t>(object + UEWrappers::GetOffsets()->UProperty.PropertyFlags);
}

std::pair<UEPropertyType, std::string> UE_UProperty::GetType() const
{
    if (IsA<UE_UDoubleProperty>())
    {
        return {UEPropertyType::DoubleProperty, Cast<UE_UDoubleProperty>().GetTypeStr()};
    }
    if (IsA<UE_UFloatProperty>())
    {
        return {UEPropertyType::FloatProperty, Cast<UE_UFloatProperty>().GetTypeStr()};
    }
    if (IsA<UE_UIntProperty>())
    {
        return {UEPropertyType::IntProperty, Cast<UE_UIntProperty>().GetTypeStr()};
    }
    if (IsA<UE_UInt16Property>())
    {
        return {UEPropertyType::Int16Property, Cast<UE_UInt16Property>().GetTypeStr()};
    }
    if (IsA<UE_UInt32Property>())
    {
        return {UEPropertyType::Int32Property, Cast<UE_UInt32Property>().GetTypeStr()};
    }
    if (IsA<UE_UInt64Property>())
    {
        return {UEPropertyType::Int64Property, Cast<UE_UInt64Property>().GetTypeStr()};
    }
    if (IsA<UE_UInt8Property>())
    {
        return {UEPropertyType::Int8Property, Cast<UE_UInt8Property>().GetTypeStr()};
    }
    if (IsA<UE_UUInt16Property>())
    {
        return {UEPropertyType::UInt16Property, Cast<UE_UUInt16Property>().GetTypeStr()};
    }
    if (IsA<UE_UUInt32Property>())
    {
        return {UEPropertyType::UInt32Property, Cast<UE_UUInt32Property>().GetTypeStr()};
    }
    if (IsA<UE_UUInt64Property>())
    {
        return {UEPropertyType::UInt64Property, Cast<UE_UUInt64Property>().GetTypeStr()};
    }
    if (IsA<UE_UTextProperty>())
    {
        return {UEPropertyType::TextProperty, Cast<UE_UTextProperty>().GetTypeStr()};
    }
    if (IsA<UE_UStrProperty>())
    {
        return {UEPropertyType::TextProperty, Cast<UE_UStrProperty>().GetTypeStr()};
    }
    if (IsA<UE_UClassProperty>())
    {
        return {UEPropertyType::ClassProperty, Cast<UE_UClassProperty>().GetTypeStr()};
    }
    if (IsA<UE_UStructProperty>())
    {
        return {UEPropertyType::StructProperty, Cast<UE_UStructProperty>().GetTypeStr()};
    }
    if (IsA<UE_UNameProperty>())
    {
        return {UEPropertyType::NameProperty, Cast<UE_UNameProperty>().GetTypeStr()};
    }
    if (IsA<UE_UBoolProperty>())
    {
        return {UEPropertyType::BoolProperty, Cast<UE_UBoolProperty>().GetTypeStr()};
    }
    if (IsA<UE_UByteProperty>())
    {
        return {UEPropertyType::ByteProperty, Cast<UE_UByteProperty>().GetTypeStr()};
    }
    if (IsA<UE_UArrayProperty>())
    {
        return {UEPropertyType::ArrayProperty, Cast<UE_UArrayProperty>().GetTypeStr()};
    }
    if (IsA<UE_UEnumProperty>())
    {
        return {UEPropertyType::EnumProperty, Cast<UE_UEnumProperty>().GetTypeStr()};
    }
    if (IsA<UE_USetProperty>())
    {
        return {UEPropertyType::SetProperty, Cast<UE_USetProperty>().GetTypeStr()};
    }
    if (IsA<UE_UMapProperty>())
    {
        return {UEPropertyType::MapProperty, Cast<UE_UMapProperty>().GetTypeStr()};
    }
    if (IsA<UE_UInterfaceProperty>())
    {
        return {UEPropertyType::InterfaceProperty, Cast<UE_UInterfaceProperty>().GetTypeStr()};
    }
    if (IsA<UE_UMulticastDelegateProperty>())
    {
        return {UEPropertyType::MulticastDelegateProperty, Cast<UE_UMulticastDelegateProperty>().GetTypeStr()};
    }
    if (IsA<UE_UWeakObjectProperty>())
    {
        return {UEPropertyType::WeakObjectProperty, Cast<UE_UWeakObjectProperty>().GetTypeStr()};
    }
    if (IsA<UE_ULazyObjectProperty>())
    {
        return {UEPropertyType::LazyObjectProperty, Cast<UE_ULazyObjectProperty>().GetTypeStr()};
    }
    if (IsA<UE_UObjectProperty>())
    {
        return {UEPropertyType::ObjectProperty, Cast<UE_UObjectProperty>().GetTypeStr()};
    }
    if (IsA<UE_UObjectPropertyBase>())
    {
        return {UEPropertyType::ObjectProperty, Cast<UE_UObjectPropertyBase>().GetTypeStr()};
    }
    return {UEPropertyType::Unknown, GetClass().GetName()};
}

IUProperty UE_UProperty::GetInterface() const { return IUProperty(this); }

UE_UClass UE_UProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Property");
    return obj;
}

UE_UStruct UE_UStruct::GetSuper() const
{
    return vm_rpm_ptr<UE_UStruct>(object + UEWrappers::GetOffsets()->UStruct.SuperStruct);
}

UE_FField UE_UStruct::GetChildProperties() const
{
    if (UEWrappers::GetOffsets()->UStruct.ChildProperties > 0)
    {
        return vm_rpm_ptr<UE_FField>(object + UEWrappers::GetOffsets()->UStruct.ChildProperties);
    }
    return {};
}

UE_UField UE_UStruct::GetChildren() const
{
    if (UEWrappers::GetOffsets()->UStruct.Children > 0)
    {
        return vm_rpm_ptr<UE_UField>(object + UEWrappers::GetOffsets()->UStruct.Children);
    }
    return {};
}

int32_t UE_UStruct::GetSize() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->UStruct.PropertiesSize);
}

UE_UClass UE_UStruct::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Struct");
    return obj;
}

UE_FField UE_UStruct::FindChildProp(const std::string& name) const
{
    for (auto prop = GetChildProperties(); prop; prop = prop.GetNext())
    {
        if (prop.GetName() == name)
            return prop;
    }
    return {};
}

UE_UField UE_UStruct::FindChild(const std::string& name) const
{
    for (auto prop = GetChildren(); prop; prop = prop.GetNext())
    {
        if (prop.GetName() == name)
            return prop;
    }
    return {};
}

uintptr_t UE_UFunction::GetFunc() const
{
    return vm_rpm_ptr<uintptr_t>(object + UEWrappers::GetOffsets()->UFunction.Func);
}

int8_t UE_UFunction::GetNumParams() const
{
    return vm_rpm_ptr<int8_t>(object + UEWrappers::GetOffsets()->UFunction.NumParams);
}

int16_t UE_UFunction::GetParamSize() const
{
    return vm_rpm_ptr<int16_t>(object + UEWrappers::GetOffsets()->UFunction.ParamSize);
}

uint32_t UE_UFunction::GetFunctionEFlags() const
{
    return vm_rpm_ptr<uint32_t>(object + UEWrappers::GetOffsets()->UFunction.EFunctionFlags);
}

std::string UE_UFunction::GetFunctionFlags() const
{
    auto flags = GetFunctionEFlags();
    std::string result;
    if (flags == FUNC_None)
    {
        result = "None";
    }
    else
    {
        if (flags & FUNC_Final)
        {
            result += "Final|";
        }
        if (flags & FUNC_RequiredAPI)
        {
            result += "RequiredAPI|";
        }
        if (flags & FUNC_BlueprintAuthorityOnly)
        {
            result += "BlueprintAuthorityOnly|";
        }
        if (flags & FUNC_BlueprintCosmetic)
        {
            result += "BlueprintCosmetic|";
        }
        if (flags & FUNC_Net)
        {
            result += "Net|";
        }
        if (flags & FUNC_NetReliable)
        {
            result += "NetReliable";
        }
        if (flags & FUNC_NetRequest)
        {
            result += "NetRequest|";
        }
        if (flags & FUNC_Exec)
        {
            result += "Exec|";
        }
        if (flags & FUNC_Native)
        {
            result += "Native|";
        }
        if (flags & FUNC_Event)
        {
            result += "Event|";
        }
        if (flags & FUNC_NetResponse)
        {
            result += "NetResponse|";
        }
        if (flags & FUNC_Static)
        {
            result += "Static|";
        }
        if (flags & FUNC_NetMulticast)
        {
            result += "NetMulticast|";
        }
        if (flags & FUNC_UbergraphFunction)
        {
            result += "UbergraphFunction|";
        }
        if (flags & FUNC_MulticastDelegate)
        {
            result += "MulticastDelegate|";
        }
        if (flags & FUNC_Public)
        {
            result += "Public|";
        }
        if (flags & FUNC_Private)
        {
            result += "Private|";
        }
        if (flags & FUNC_Protected)
        {
            result += "Protected|";
        }
        if (flags & FUNC_Delegate)
        {
            result += "Delegate|";
        }
        if (flags & FUNC_NetServer)
        {
            result += "NetServer|";
        }
        if (flags & FUNC_HasOutParms)
        {
            result += "HasOutParms|";
        }
        if (flags & FUNC_HasDefaults)
        {
            result += "HasDefaults|";
        }
        if (flags & FUNC_NetClient)
        {
            result += "NetClient|";
        }
        if (flags & FUNC_DLLImport)
        {
            result += "DLLImport|";
        }
        if (flags & FUNC_BlueprintCallable)
        {
            result += "BlueprintCallable|";
        }
        if (flags & FUNC_BlueprintEvent)
        {
            result += "BlueprintEvent|";
        }
        if (flags & FUNC_BlueprintPure)
        {
            result += "BlueprintPure|";
        }
        if (flags & FUNC_EditorOnly)
        {
            result += "EditorOnly|";
        }
        if (flags & FUNC_Const)
        {
            result += "Const|";
        }
        if (flags & FUNC_NetValidate)
        {
            result += "NetValidate|";
        }
        if (result.size())
        {
            result.erase(result.size() - 1);
        }
    }
    return result;
}

UE_UClass UE_UFunction::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Function");
    return obj;
}

UE_UClass UE_UScriptStruct::StaticClass()
{
    static UE_UClass obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ScriptStruct");
    return obj;
}

UE_UClass UE_UClass::StaticClass()
{
    static UE_UClass obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Class");
    return obj;
}

TArray<uint8_t> UE_UEnum::GetNames() const
{
    return vm_rpm_ptr<TArray<uint8_t>>(object + UEWrappers::GetOffsets()->UEnum.Names);
}

std::string UE_UEnum::GetName() const
{
    std::string name = UE_UField::GetName();
    if (!name.empty() && name[0] != 'E')
        return "E" + name;
    return name;
}

UE_UClass UE_UEnum::StaticClass()
{
    static UE_UClass obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Enum");
    return obj;
}

std::string UE_UDoubleProperty::GetTypeStr() const { return "double"; }

UE_UClass UE_UDoubleProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.DoubleProperty");
    return obj;
}

UE_UStruct UE_UStructProperty::GetStruct() const
{
    return vm_rpm_ptr<UE_UStruct>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_UStructProperty::GetTypeStr() const
{
    return "struct " + GetStruct().GetCppName();
}

UE_UClass UE_UStructProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.StructProperty");
    return obj;
}

std::string UE_UNameProperty::GetTypeStr() const { return "struct FName"; }

UE_UClass UE_UNameProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.NameProperty");
    return obj;
}

UE_UClass UE_UObjectPropertyBase::GetPropertyClass() const
{
    return vm_rpm_ptr<UE_UClass>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_UObjectPropertyBase::GetTypeStr() const
{
    return "struct " + GetPropertyClass().GetCppName() + "*";
}

UE_UClass UE_UObjectPropertyBase::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ObjectPropertyBase");
    return obj;
}

UE_UClass UE_UObjectProperty::GetPropertyClass() const
{
    return vm_rpm_ptr<UE_UClass>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_UObjectProperty::GetTypeStr() const
{
    return "struct " + GetPropertyClass().GetCppName() + "*";
}

UE_UClass UE_UObjectProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ObjectProperty");
    return obj;
}

UE_UProperty UE_UArrayProperty::GetInner() const
{
    uintptr_t arrayInner = UEWrappers::GetOffsets()->UProperty.ArrayInner;
    if (arrayInner > 0)
    {
        return vm_rpm_ptr<UE_UProperty>(object + arrayInner);
    }
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_UArrayProperty::GetTypeStr() const
{
    return "struct TArray<" + GetInner().GetType().second + ">";
}

UE_UClass UE_UArrayProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ArrayProperty");
    return obj;
}

UE_UEnum UE_UByteProperty::GetEnum() const
{
    auto e = vm_rpm_ptr<UE_UEnum>(object + UEWrappers::GetOffsets()->UProperty.Size);
    return (e && e.IsA<UE_UEnum>()) ? e : nullptr;
}

std::string UE_UByteProperty::GetTypeStr() const
{
    auto e = GetEnum();
    if (e)
        return "enum class " + e.GetName();
    return "uint8_t";
}

UE_UClass UE_UByteProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ByteProperty");
    return obj;
}

uint8_t UE_UBoolProperty::GetFieldSize() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

uint8_t UE_UBoolProperty::GetByteOffset() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->UProperty.Size + 1);
}

uint8_t UE_UBoolProperty::GetByteMask() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->UProperty.Size + 2);
}

uint8_t UE_UBoolProperty::GetFieldMask() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->UProperty.Size + 3);
}

std::string UE_UBoolProperty::GetTypeStr() const
{
    if (GetFieldMask() == 0xFF)
    {
        return "bool";
    }
    return "uint8_t";
}

UE_UClass UE_UBoolProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.BoolProperty");
    return obj;
}

std::string UE_UFloatProperty::GetTypeStr() const { return "float"; }

UE_UClass UE_UFloatProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.FloatProperty");
    return obj;
}

std::string UE_UIntProperty::GetTypeStr() const { return "int"; }

UE_UClass UE_UIntProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.IntProperty");
    return obj;
}

std::string UE_UInt16Property::GetTypeStr() const { return "int16_t"; }

UE_UClass UE_UInt16Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Int16Property");
    return obj;
}

std::string UE_UInt64Property::GetTypeStr() const { return "int64_t"; }

UE_UClass UE_UInt64Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Int64Property");
    return obj;
}

std::string UE_UInt8Property::GetTypeStr() const { return "uint8_t"; }

UE_UClass UE_UInt8Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Int8Property");
    return obj;
}

std::string UE_UUInt16Property::GetTypeStr() const { return "uint16_t"; }

UE_UClass UE_UUInt16Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.UInt16Property");
    return obj;
}

std::string UE_UUInt32Property::GetTypeStr() const { return "uint32_t"; }

UE_UClass UE_UUInt32Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.UInt32Property");
    return obj;
}

std::string UE_UInt32Property::GetTypeStr() const { return "int32_t"; }

UE_UClass UE_UInt32Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.Int32Property");
    return obj;
}

std::string UE_UUInt64Property::GetTypeStr() const { return "uint64_t"; }

UE_UClass UE_UUInt64Property::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.UInt64Property");
    return obj;
}

std::string UE_UTextProperty::GetTypeStr() const { return "struct FText"; }

UE_UClass UE_UTextProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.TextProperty");
    return obj;
}

std::string UE_UStrProperty::GetTypeStr() const { return "struct FString"; }

UE_UClass UE_UStrProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.StrProperty");
    return obj;
}

UE_UProperty UE_UEnumProperty::GetUnderlayingProperty() const
{
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

UE_UEnum UE_UEnumProperty::GetEnum() const
{
    auto e = vm_rpm_ptr<UE_UEnum>(object + UEWrappers::GetOffsets()->UProperty.Size);
    return (e && e.IsA<UE_UEnum>()) ? e : nullptr;
}

std::string UE_UEnumProperty::GetTypeStr() const
{
    if (GetEnum())
        return "enum class " + GetEnum().GetName();

    return GetUnderlayingProperty().GetType().second;
}

UE_UClass UE_UEnumProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.EnumProperty");
    return obj;
}

UE_UClass UE_UClassProperty::GetMetaClass() const
{
    return vm_rpm_ptr<UE_UClass>(object + UEWrappers::GetOffsets()->UProperty.Size + sizeof(void*));
}

std::string UE_UClassProperty::GetTypeStr() const
{
    return "struct " + GetMetaClass().GetCppName() + "*";
}

UE_UClass UE_UClassProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.ClassProperty");
    return obj;
}

std::string UE_USoftClassProperty::GetTypeStr() const
{
    auto className = GetMetaClass() ? GetMetaClass().GetCppName() : GetPropertyClass().GetCppName();
    return "struct TSoftClassPtr<struct " + className + "*>";
}

UE_UProperty UE_USetProperty::GetElementProp() const
{
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_USetProperty::GetTypeStr() const
{
    return "struct TSet<" + GetElementProp().GetType().second + ">";
}

UE_UClass UE_USetProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.SetProperty");
    return obj;
}

UE_UProperty UE_UMapProperty::GetKeyProp() const
{
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

UE_UProperty UE_UMapProperty::GetValueProp() const
{
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size + sizeof(void*));
}

std::string UE_UMapProperty::GetTypeStr() const
{
    return "struct TMap<" + GetKeyProp().GetType().second + ", " + GetValueProp().GetType().second + ">";
}

UE_UClass UE_UMapProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.MapProperty");
    return obj;
}

UE_UProperty UE_UInterfaceProperty::GetInterfaceClass() const
{
    return vm_rpm_ptr<UE_UProperty>(object + UEWrappers::GetOffsets()->UProperty.Size);
}

std::string UE_UInterfaceProperty::GetTypeStr() const
{
    return "struct TScriptInterface<" + GetInterfaceClass().GetType().second + ">";
}

UE_UClass UE_UInterfaceProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.InterfaceProperty");
    return obj;
}

std::string UE_UMulticastDelegateProperty::GetTypeStr() const
{
    return "struct FScriptMulticastDelegate";
}

UE_UClass UE_UMulticastDelegateProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.MulticastDelegateProperty");
    return obj;
}

std::string UE_UWeakObjectProperty::GetTypeStr() const
{
    return "struct TWeakObjectPtr<" + this->Cast<UE_UStructProperty>().GetTypeStr() + ">";
}

UE_UClass UE_UWeakObjectProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.WeakObjectProperty");
    return obj;
}

std::string UE_ULazyObjectProperty::GetTypeStr() const
{
    return "struct TLazyObjectPtr<" + this->Cast<UE_UStructProperty>().GetTypeStr() + ">";
}

UE_UClass UE_ULazyObjectProperty::StaticClass()
{
    static auto obj = UEWrappers::GetObjects()->FindObject<UE_UClass>("Class CoreUObject.LazyObjectProperty");
    return obj;
}

std::string UE_FFieldClass::GetName() const
{
    auto name = UE_FName(object);
    return name.GetName();
}

UE_FField UE_FField::GetNext() const
{
    return vm_rpm_ptr<UE_FField>(object + UEWrappers::GetOffsets()->FField.Next);
}

std::string UE_FField::GetName() const
{
    auto name = UE_FName(object + UEWrappers::GetOffsets()->FField.NamePrivate);
    return name.GetName();
}

UE_FFieldClass UE_FField::GetClass() const
{
    return vm_rpm_ptr<UE_FFieldClass>(object + UEWrappers::GetOffsets()->FField.ClassPrivate);
}

std::string IFProperty::GetName() const
{
    return ((UE_FProperty*)prop)->GetName();
}

int32_t IFProperty::GetArrayDim() const
{
    return ((UE_FProperty*)prop)->GetArrayDim();
}

int32_t IFProperty::GetSize() const { return ((UE_FProperty*)prop)->GetSize(); }

int32_t IFProperty::GetOffset() const
{
    return ((UE_FProperty*)prop)->GetOffset();
}

uint64_t IFProperty::GetPropertyFlags() const
{
    return ((UE_FProperty*)prop)->GetPropertyFlags();
}

std::pair<UEPropertyType, std::string> IFProperty::GetType() const
{
    return ((UE_FProperty*)prop)->GetType();
}

uint8_t IFProperty::GetFieldMask() const
{
    return ((UE_FBoolProperty*)prop)->GetFieldMask();
}

int32_t UE_FProperty::GetArrayDim() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->FProperty.ArrayDim);
}

int32_t UE_FProperty::GetSize() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->FProperty.ElementSize);
}

int32_t UE_FProperty::GetOffset() const
{
    return vm_rpm_ptr<int32_t>(object + UEWrappers::GetOffsets()->FProperty.Offset_Internal);
}

uint64_t UE_FProperty::GetPropertyFlags() const
{
    return vm_rpm_ptr<uint64_t>(object + UEWrappers::GetOffsets()->FProperty.PropertyFlags);
}

UEPropTypeInfo UE_FProperty::GetType() const
{
    auto objectClass = GetClass();
    UEPropTypeInfo type = {UEPropertyType::Unknown, objectClass.GetName()};

    auto& str = type.second;
    auto hash = Hash(str.c_str(), str.size());
    switch (hash)
    {
    case HASH("StructProperty"):
    {
        auto obj = this->Cast<UE_FStructProperty>();
        type = {UEPropertyType::StructProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("ObjectProperty"):
    {
        auto obj = this->Cast<UE_FObjectPropertyBase>();
        type = {UEPropertyType::ObjectProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("SoftObjectProperty"):
    {
        auto obj = this->Cast<UE_FObjectPropertyBase>();
        type = {UEPropertyType::SoftObjectProperty, "struct TSoftObjectPtr<" + obj.GetPropertyClass().GetCppName() + ">"};
        break;
    }
    case HASH("FloatProperty"):
    {
        type = {UEPropertyType::FloatProperty, "float"};
        break;
    }
    case HASH("ByteProperty"):
    {
        auto obj = this->Cast<UE_FByteProperty>();
        type = {UEPropertyType::ByteProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("BoolProperty"):
    {
        auto obj = this->Cast<UE_FBoolProperty>();
        type = {UEPropertyType::BoolProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("IntProperty"):
    {
        type = {UEPropertyType::IntProperty, "int32_t"};
        break;
    }
    case HASH("Int8Property"):
    {
        type = {UEPropertyType::Int8Property, "int8_t"};
        break;
    }
    case HASH("Int16Property"):
    {
        type = {UEPropertyType::Int16Property, "int16_t"};
        break;
    }
    case HASH("Int64Property"):
    {
        type = {UEPropertyType::Int64Property, "int64_t"};
        break;
    }
    case HASH("UInt16Property"):
    {
        type = {UEPropertyType::UInt16Property, "uint16_t"};
        break;
    }
    case HASH("Int32Property"):
    {
        type = {UEPropertyType::Int32Property, "int32_t"};
        break;
    }
    case HASH("UInt32Property"):
    {
        type = {UEPropertyType::UInt32Property, "uint32_t"};
        break;
    }
    case HASH("UInt64Property"):
    {
        type = {UEPropertyType::UInt64Property, "uint64_t"};
        break;
    }
    case HASH("NameProperty"):
    {
        type = {UEPropertyType::NameProperty, "struct FName"};
        break;
    }
    case HASH("DelegateProperty"):
    {
        type = {UEPropertyType::DelegateProperty, "struct FDelegate"};
        break;
    }
    case HASH("SetProperty"):
    {
        auto obj = this->Cast<UE_FSetProperty>();
        type = {UEPropertyType::SetProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("ArrayProperty"):
    {
        auto obj = this->Cast<UE_FArrayProperty>();
        type = {UEPropertyType::ArrayProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("WeakObjectProperty"):
    {
        auto obj = this->Cast<UE_FStructProperty>();
        type = {UEPropertyType::WeakObjectProperty, "struct TWeakObjectPtr<" + obj.GetTypeStr() + ">"};
        break;
    }
    case HASH("LazyObjectProperty"):
    {
        auto obj = this->Cast<UE_FStructProperty>();
        type = {UEPropertyType::LazyObjectProperty, "struct TLazyObjectPtr<" + obj.GetTypeStr() + ">"};
        break;
    }
    case HASH("StrProperty"):
    {
        type = {UEPropertyType::StrProperty, "struct FString"};
        break;
    }
    case HASH("TextProperty"):
    {
        type = {UEPropertyType::TextProperty, "struct FText"};
        break;
    }
    case HASH("MulticastSparseDelegateProperty"):
    {
        type = {UEPropertyType::MulticastSparseDelegateProperty, "struct FMulticastSparseDelegate"};
        break;
    }
    case HASH("EnumProperty"):
    {
        auto obj = this->Cast<UE_FEnumProperty>();
        type = {UEPropertyType::EnumProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("DoubleProperty"):
    {
        type = {UEPropertyType::DoubleProperty, "double"};
        break;
    }
    case HASH("MulticastDelegateProperty"):
    {
        type = {UEPropertyType::MulticastDelegateProperty, "FMulticastDelegate"};
        break;
    }
    case HASH("ClassProperty"):
    {
        auto obj = this->Cast<UE_FClassProperty>();
        type = {UEPropertyType::ClassProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("MulticastInlineDelegateProperty"):
    {
        type = {UEPropertyType::MulticastDelegateProperty, "struct FMulticastInlineDelegate"};
        break;
    }
    case HASH("MapProperty"):
    {
        auto obj = this->Cast<UE_FMapProperty>();
        type = {UEPropertyType::MapProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("InterfaceProperty"):
    {
        auto obj = this->Cast<UE_FInterfaceProperty>();
        type = {UEPropertyType::InterfaceProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("FieldPathProperty"):
    {
        auto obj = this->Cast<UE_FFieldPathProperty>();
        type = {UEPropertyType::FieldPathProperty, obj.GetTypeStr()};
        break;
    }
    case HASH("SoftClassProperty"):
    {
        auto obj = this->Cast<UE_FSoftClassProperty>();
        type = {UEPropertyType::SoftClassProperty, obj.GetTypeStr()};
        break;
    }
    }

    return type;
}

IFProperty UE_FProperty::GetInterface() const { return IFProperty(this); }

uintptr_t UE_FProperty::FindSubFPropertyBaseOffset() const
{
    uintptr_t offset = 0;
    uintptr_t temp = 0;
    if (vm_rpm_ptr(object + UEWrappers::GetOffsets()->FProperty.Size, &temp, sizeof(uintptr_t)) && kPtrValidator.isPtrReadable(temp))
    {
        offset = UEWrappers::GetOffsets()->FProperty.Size;
    }
    else if (vm_rpm_ptr(object + UEWrappers::GetOffsets()->FProperty.Size + sizeof(void*), &temp, sizeof(uintptr_t)) && kPtrValidator.isPtrReadable(temp))
    {
        offset = UEWrappers::GetOffsets()->FProperty.Size + sizeof(void*);
    }
    return offset;
}

UE_UStruct UE_FStructProperty::GetStruct() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_UStruct>(object + offset) : UE_UStruct();
}

std::string UE_FStructProperty::GetTypeStr() const
{
    return "struct " + GetStruct().GetCppName();
}

UE_UClass UE_FObjectPropertyBase::GetPropertyClass() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_UClass>(object + offset) : UE_UClass();
}

std::string UE_FObjectPropertyBase::GetTypeStr() const
{
    return "struct " + GetPropertyClass().GetCppName() + "*";
}

UE_FProperty UE_FArrayProperty::GetInner() const
{
    uintptr_t arrayInner = UEWrappers::GetOffsets()->FProperty.ArrayInner;
    if (arrayInner > 0)
    {
        return vm_rpm_ptr<UE_FProperty>(object + arrayInner);
    }

    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_FProperty>(object + offset) : UE_FProperty();
}

std::string UE_FArrayProperty::GetTypeStr() const
{
    return "struct TArray<" + GetInner().GetType().second + ">";
}

UE_UEnum UE_FByteProperty::GetEnum() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    if (offset == 0) return nullptr;

    auto e = vm_rpm_ptr<UE_UEnum>(object + offset);
    return (e && e.IsA<UE_UEnum>()) ? e : nullptr;
}

std::string UE_FByteProperty::GetTypeStr() const
{
    auto e = GetEnum();
    if (e)
        return "enum class " + e.GetName();
    return "uint8_t";
}

uint8_t UE_FBoolProperty::GetFieldSize() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->FProperty.Size);
}

uint8_t UE_FBoolProperty::GetByteOffset() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->FProperty.Size + 1);
}

uint8_t UE_FBoolProperty::GetByteMask() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->FProperty.Size + 2);
}

uint8_t UE_FBoolProperty::GetFieldMask() const
{
    return vm_rpm_ptr<uint8_t>(object + UEWrappers::GetOffsets()->FProperty.Size + 3);
}

std::string UE_FBoolProperty::GetTypeStr() const
{
    if (GetFieldMask() == 0xFF)
    {
        return "bool";
    }
    return "uint8_t";
}

UE_FProperty UE_FEnumProperty::GetUnderlayingProperty() const
{
    static uintptr_t off = 0;
    if (off == 0)
    {
        auto p = vm_rpm_ptr<UE_FProperty>(object + UEWrappers::GetOffsets()->FProperty.Size);
        if (p && p.GetName() == "UnderlyingType")
        {
            off = UEWrappers::GetOffsets()->FProperty.Size;
        }
        else
        {
            p = vm_rpm_ptr<UE_FProperty>(object + UEWrappers::GetOffsets()->FProperty.Size - sizeof(void*));
            if (p && p.GetName() == "UnderlyingType")
            {
                off = UEWrappers::GetOffsets()->FProperty.Size + sizeof(void*);
            }
        }
        return off == 0 ? nullptr : p;
    }

    return vm_rpm_ptr<UE_FProperty>(object + off);
}

UE_UEnum UE_FEnumProperty::GetEnum() const
{
    static uintptr_t off = 0;
    if (off == 0)
    {
        auto e = vm_rpm_ptr<UE_UEnum>(object + UEWrappers::GetOffsets()->FProperty.Size + sizeof(void*));
        if (e && e.IsA<UE_UEnum>())
        {
            off = UEWrappers::GetOffsets()->FProperty.Size + sizeof(void*);
        }
        else
        {
            e = vm_rpm_ptr<UE_UEnum>(object + UEWrappers::GetOffsets()->FProperty.Size);
            if (e && e.IsA<UE_UEnum>())
            {
                off = UEWrappers::GetOffsets()->FProperty.Size + (sizeof(void*) * 2);
            }
        }
        return off == 0 ? nullptr : e;
    }

    return vm_rpm_ptr<UE_UEnum>(object + off);
}

std::string UE_FEnumProperty::GetTypeStr() const
{
    if (GetEnum())
        return "enum class " + GetEnum().GetName();

    return GetUnderlayingProperty().GetType().second;
}

UE_UClass UE_FClassProperty::GetMetaClass() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_UClass>(object + offset + sizeof(void*)) : UE_UClass();
}

std::string UE_FClassProperty::GetTypeStr() const
{
    return "struct " + GetMetaClass().GetCppName() + "*";
}

std::string UE_FSoftClassProperty::GetTypeStr() const
{
    auto className = GetMetaClass() ? GetMetaClass().GetCppName() : GetPropertyClass().GetCppName();
    return "struct TSoftClassPtr<struct " + className + "*>";
}

UE_FProperty UE_FSetProperty::GetElementProp() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_FProperty>(object + offset) : UE_FProperty();
}

std::string UE_FSetProperty::GetTypeStr() const
{
    return "struct TSet<" + GetElementProp().GetType().second + ">";
}

UE_FProperty UE_FMapProperty::GetKeyProp() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_FProperty>(object + offset) : UE_FProperty();
}

UE_FProperty UE_FMapProperty::GetValueProp() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_FProperty>(object + offset + sizeof(void*)) : UE_FProperty();
}

std::string UE_FMapProperty::GetTypeStr() const
{
    return "struct TMap<" + GetKeyProp().GetType().second + ", " + GetValueProp().GetType().second + ">";
}

UE_UClass UE_FInterfaceProperty::GetInterfaceClass() const
{
    static uintptr_t offset = 0;
    if (offset == 0)
    {
        offset = FindSubFPropertyBaseOffset();
    }
    return offset ? vm_rpm_ptr<UE_UClass>(object + offset) : UE_UClass();
}

std::string UE_FInterfaceProperty::GetTypeStr() const
{
    return "struct TScriptInterface<I" + GetInterfaceClass().GetName() + ">";
}

UE_FName UE_FFieldPathProperty::GetPropertyName() const
{
    return vm_rpm_ptr<UE_FName>(object + UEWrappers::GetOffsets()->FProperty.Size);
}

std::string UE_FFieldPathProperty::GetTypeStr() const
{
    return "struct TFieldPath<F" + GetPropertyName().GetName() + ">";
}
