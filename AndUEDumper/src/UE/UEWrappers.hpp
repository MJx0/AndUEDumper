#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "UEMemory.hpp"
#include "UEOffsets.hpp"

class UE_UObjectArray;

namespace UEWrappers
{
    void Init(const UEVars *vars);
    UEVars const *GetUEVars();
    UE_UObjectArray *GetObjects();
};  // namespace UEWrappers

template <class T>
class TArray
{
protected:
    T *Data;
    int32_t NumElements;
    int32_t MaxElements;

public:
    TArray(const TArray &) = default;
    TArray(TArray &&) = default;

    inline TArray() : Data(nullptr), NumElements(0), MaxElements(0) {}
    inline TArray(int size) : NumElements(0), MaxElements(size), Data(reinterpret_cast<T *>(calloc(1, sizeof(T) * size))) {}

    TArray &operator=(TArray &&) = default;
    TArray &operator=(const TArray &) = default;

    inline T &operator[](int i) { return (IsValid() && IsValidIndex(i)) ? Data[i] : T(); };
    inline const T &operator[](int i) const { (IsValid() && IsValidIndex(i)) ? Data[i] : T(); }

    inline explicit operator bool() const { return IsValid(); };

    inline bool IsValid() const { return Data != nullptr; }
    inline bool IsValidIndex(int index) const { return index >= 0 && index < NumElements; }

    inline int Slack() const { return MaxElements - NumElements; }

    inline int Num() const { return NumElements; }
    inline int Max() const { return MaxElements; }

    inline T *GetData() const { return Data; }
    inline T *GetDataAt(int index) const { return Data + index; }

    inline bool Add(const T &element)
    {
        if (Slack() <= 0) return false;

        Data[NumElements] = element;
        NumElements++;
        return true;
    }

    inline bool RemoveAt(int index)
    {
        if (!IsValidIndex(index)) return false;

        NumElements--;

        for (int i = index; i < NumElements; i++)
        {
            Data[i] = Data[i + 1];
        }

        return true;
    }

    inline void Clear()
    {
        NumElements = 0;
        if (Data) memset(Data, 0, sizeof(T) * MaxElements);
    }
};

class FString : public TArray<wchar_t>
{
public:
    FString() = default;
    inline FString(const wchar_t *wstr)
    {
        MaxElements = NumElements = (wstr && *wstr) ? int32_t(std::wcslen(wstr)) + 1 : 0;
        if (NumElements) Data = const_cast<wchar_t *>(wstr);
    }

    inline FString operator=(const wchar_t *&&other) { return FString(other); }

    inline std::wstring ToWString() const { return IsValid() ? Data : L""; }
    std::string ToString() const;
};

template <typename KeyType, typename ValueType>
class TPair
{
private:
    KeyType First;
    ValueType Second;

public:
    TPair() = default;
    inline TPair(KeyType Key, ValueType Value) : First(Key), Second(Value) {}

    inline KeyType &Key() { return First; }
    inline const KeyType &Key() const { return First; }
    inline ValueType &Value() { return Second; }
    inline const ValueType &Value() const { return Second; }
};

class UE_FName
{
protected:
    uint8_t *object;

public:
    UE_FName(uint8_t *object) : object(object) {}
    UE_FName() : object(nullptr) {}
    int GetNumber() const;
    std::string GetName() const;
};

enum class UEPropertyType
{
    Unknown,
    StructProperty,
    ObjectProperty,
    SoftObjectProperty,
    FloatProperty,
    ByteProperty,
    BoolProperty,
    IntProperty,
    Int8Property,
    Int16Property,
    Int32Property,
    Int64Property,
    UInt16Property,
    UInt32Property,
    UInt64Property,
    NameProperty,
    DelegateProperty,
    SetProperty,
    ArrayProperty,
    WeakObjectProperty,
    LazyObjectProperty,
    StrProperty,
    TextProperty,
    MulticastSparseDelegateProperty,
    EnumProperty,
    DoubleProperty,
    MulticastDelegateProperty,
    ClassProperty,
    MulticastInlineDelegateProperty,
    MapProperty,
    InterfaceProperty,
    FieldPathProperty,
    SoftClassProperty
};

enum class EInternalObjectFlags : int32_t
{
    None = 0,
    ReachableInCluster = 1 << 23,
    ClusterRoot = 1 << 24,
    Native = 1 << 25,
    Async = 1 << 26,
    AsyncLoading = 1 << 27,
    Unreachable = 1 << 28,
    PendingKill = 1 << 29,
    RootSet = 1 << 30,
    GarbageCollectionKeepFlags = Native | Async | AsyncLoading,
    AllFlags = ReachableInCluster | ClusterRoot | Native | Async | AsyncLoading | Unreachable | PendingKill | RootSet,
};
kDEFINE_ENUM_BITMASK_OPERATORS(EInternalObjectFlags);

class UE_UClass;
class UE_FField;

enum class EObjectFlags : uint32_t
{
    NoFlags = 0x00000000,

    Public = 0x00000001,
    Standalone = 0x00000002,
    MarkAsNative = 0x00000004,
    Transactional = 0x00000008,
    ClassDefaultObject = 0x00000010,
    ArchetypeObject = 0x00000020,
    Transient = 0x00000040,

    MarkAsRootSet = 0x00000080,
    TagGarbageTemp = 0x00000100,

    NeedInitialization = 0x00000200,
    NeedLoad = 0x00000400,
    KeepForCooker = 0x00000800,
    NeedPostLoad = 0x00001000,
    NeedPostLoadSubobjects = 0x00002000,
    NewerVersionExists = 0x00004000,
    BeginDestroyed = 0x00008000,
    FinishDestroyed = 0x00010000,

    BeingRegenerated = 0x00020000,
    DefaultSubObject = 0x00040000,
    WasLoaded = 0x00080000,
    TextExportTransient = 0x00100000,
    LoadCompleted = 0x00200000,
    InheritableComponentTemplate = 0x00400000,
    DuplicateTransient = 0x00800000,
    StrongRefOnFrame = 0x01000000,
    NonPIEDuplicateTransient = 0x02000000,
    Dynamic = 0x04000000,
    WillBeLoaded = 0x08000000,
};
kDEFINE_ENUM_BITMASK_OPERATORS(EObjectFlags);

class UE_UObject
{
protected:
    uint8_t *object;

public:
    UE_UObject(void *object) : object((uint8_t *)object) {}
    UE_UObject() : object(nullptr) {}
    bool operator==(const UE_UObject obj) const { return obj.object == object; };
    bool operator!=(const UE_UObject obj) const { return obj.object != object; };
    EObjectFlags GetFlags() const;
    int32_t GetIndex() const;
    UE_UClass GetClass() const;
    UE_UObject GetOuter() const;
    UE_UObject GetPackageObject() const;
    std::string GetName() const;
    std::string GetFullName() const;
    std::string GetCppName() const;
    uint8_t *GetAddress() const { return object; }
    operator uint8_t *() const { return object; };
    operator bool() const { return object != nullptr; }

    template <typename Base>
    Base Cast() const
    {
        return Base(object);
    }

    template <typename T>
    bool IsA() const;

    bool IsA(UE_UClass cmp) const;

    bool HasFlags(EObjectFlags flags) const;

    static UE_UClass StaticClass();
};

class UE_UObjectArray
{
public:
    UE_UObjectArray() : Objects(nullptr) {}
    UE_UObjectArray(void *objects) : Objects((uint8_t **)objects) {}
    UE_UObjectArray(uintptr_t objects) : Objects((uint8_t **)objects) {}

    uint8_t **Objects;

    int32_t GetNumElements() const;

    uint8_t *GetObjectPtr(int32_t id) const;

    void ForEachObject(const std::function<bool(UE_UObject)> &callback) const;
    void ForEachObjectOfClass(const class UE_UClass &cmp, const std::function<bool(UE_UObject)> &callback) const;

    bool IsObject(const UE_UObject &address) const;

    static std::string ToLower(const std::string &s)
    {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }

    template <typename T = UE_UObject>
    T FindObject(const std::string &fullName) const
    {
        std::string lowerFullName = ToLower(fullName);
        for (int32_t i = 0; i < GetNumElements(); i++)
        {
            UE_UObject object = GetObjectPtr(i);
            if (object && ToLower(object.GetFullName()) == lowerFullName)
            {
                return object.Cast<T>();
            }
        }
        return T();
    }

    template <typename T = UE_UObject>
    T FindObjectFast(const std::string &name) const
    {
        std::string lowerName = ToLower(name);
        for (int32_t i = 0; i < GetNumElements(); i++)
        {
            UE_UObject object = GetObjectPtr(i);
            if (object && ToLower(object.GetName()) == lowerName)
            {
                return object.Cast<T>();
            }
        }
        return T();
    }

    template <typename T = UE_UObject>
    T FindObjectFastInOuter(const std::string &name, const std::string &outer)
    {
        std::string lowerName = ToLower(name);
        std::string lowerOuter = ToLower(outer);
        for (int32_t i = 0; i < GetNumElements(); i++)
        {
            UE_UObject object = GetObjectPtr(i);
            if (ToLower(object.GetName()) == lowerName && ToLower(object.GetOuter().GetName()) == lowerOuter)
            {
                return object.Cast<T>();
            }
        }

        return T();
    }
};

class UE_UInterface : public UE_UObject
{
public:
    static UE_UClass StaticClass();
};

class UE_AActor : public UE_UObject
{
public:
    static UE_UClass StaticClass();
};

class UE_UField : public UE_UObject
{
public:
    using UE_UObject::UE_UObject;
    UE_UField GetNext() const;
    static UE_UClass StaticClass();
};

enum EPropertyFlags : uint64_t
{
    CPF_None = 0,

    CPF_Edit = 0x0000000000000001,                   ///< Property is user-settable in the editor.
    CPF_ConstParm = 0x0000000000000002,              ///< This is a constant function parameter
    CPF_BlueprintVisible = 0x0000000000000004,       ///< This property can be read by blueprint code
    CPF_ExportObject = 0x0000000000000008,           ///< Object can be exported with actor.
    CPF_BlueprintReadOnly = 0x0000000000000010,      ///< This property cannot be modified by blueprint code
    CPF_Net = 0x0000000000000020,                    ///< Property is relevant to network replication.
    CPF_EditFixedSize = 0x0000000000000040,          ///< Indicates that elements of an array can be modified, but its size cannot be changed.
    CPF_Parm = 0x0000000000000080,                   ///< Function/When call parameter.
    CPF_OutParm = 0x0000000000000100,                ///< Value is copied out after function call.
    CPF_ZeroConstructor = 0x0000000000000200,        ///< memset is fine for construction
    CPF_ReturnParm = 0x0000000000000400,             ///< Return value.
    CPF_DisableEditOnTemplate = 0x0000000000000800,  ///< Disable editing of this property on an archetype/sub-blueprint
    CPF_NonNullable = 0x0000000000001000,            ///< Object property can never be null
    CPF_Transient = 0x0000000000002000,              ///< Property is transient: shouldn't be saved or loaded, except for Blueprint CDOs.
    CPF_Config = 0x0000000000004000,                 ///< Property should be loaded/saved as permanent profile.
    CPF_RequiredParm = 0x0000000000008000,           ///< Parameter must be linked explicitly in blueprint. Leaving the parameter out results in a compile error.
    CPF_DisableEditOnInstance = 0x0000000000010000,  ///< Disable editing on an instance of this class
    CPF_EditConst = 0x0000000000020000,              ///< Property is uneditable in the editor.
    CPF_GlobalConfig = 0x0000000000040000,           ///< Load config from base class, not subclass.
    CPF_InstancedReference = 0x0000000000080000,     ///< Property is a component references.
    // CPF_                                = 0x0000000000100000,    ///<
    CPF_DuplicateTransient = 0x0000000000200000,  ///< Property should always be reset to the default value during any type of duplication (copy/paste, binary duplication, etc.)
    // CPF_                                = 0x0000000000400000,    ///<
    // CPF_                                = 0x0000000000800000,    ///<
    CPF_SaveGame = 0x0000000001000000,  ///< Property should be serialized for save games, this is only checked for game-specific archives with ArIsSaveGame
    CPF_NoClear = 0x0000000002000000,   ///< Hide clear (and browse) button.
    // CPF_                              = 0x0000000004000000,    ///<
    CPF_ReferenceParm = 0x0000000008000000,        ///< Value is passed by reference; CPF_OutParam and CPF_Param should also be set.
    CPF_BlueprintAssignable = 0x0000000010000000,  ///< MC Delegates only.  Property should be exposed for assigning in blueprint code
    CPF_Deprecated = 0x0000000020000000,           ///< Property is deprecated.  Read it from an archive, but don't save it.
    CPF_IsPlainOldData = 0x0000000040000000,       ///< If this is set, then the property can be memcopied instead of CopyCompleteValue / CopySingleValue
    CPF_RepSkip = 0x0000000080000000,              ///< Not replicated. For non replicated properties in replicated structs
    CPF_RepNotify = 0x0000000100000000,            ///< Notify actors when a property is replicated
    CPF_Interp = 0x0000000200000000,               ///< interpolatable property for use with cinematics
    CPF_NonTransactional = 0x0000000400000000,     ///< Property isn't transacted
    CPF_EditorOnly = 0x0000000800000000,           ///< Property should only be loaded in the editor
    CPF_NoDestructor = 0x0000001000000000,         ///< No destructor
    // CPF_                                = 0x0000002000000000,    ///<
    CPF_AutoWeak = 0x0000004000000000,                        ///< Only used for weak pointers, means the export type is autoweak
    CPF_ContainsInstancedReference = 0x0000008000000000,      ///< Property contains component references.
    CPF_AssetRegistrySearchable = 0x0000010000000000,         ///< asset instances will add properties with this flag to the asset registry automatically
    CPF_SimpleDisplay = 0x0000020000000000,                   ///< The property is visible by default in the editor details view
    CPF_AdvancedDisplay = 0x0000040000000000,                 ///< The property is advanced and not visible by default in the editor details view
    CPF_Protected = 0x0000080000000000,                       ///< property is protected from the perspective of script
    CPF_BlueprintCallable = 0x0000100000000000,               ///< MC Delegates only.  Property should be exposed for calling in blueprint code
    CPF_BlueprintAuthorityOnly = 0x0000200000000000,          ///< MC Delegates only.  This delegate accepts (only in blueprint) only events with BlueprintAuthorityOnly.
    CPF_TextExportTransient = 0x0000400000000000,             ///< Property shouldn't be exported to text format (e.g. copy/paste)
    CPF_NonPIEDuplicateTransient = 0x0000800000000000,        ///< Property should only be copied in PIE
    CPF_ExposeOnSpawn = 0x0001000000000000,                   ///< Property is exposed on spawn
    CPF_PersistentInstance = 0x0002000000000000,              ///< A object referenced by the property is duplicated like a component. (Each actor should have an own instance.)
    CPF_UObjectWrapper = 0x0004000000000000,                  ///< Property was parsed as a wrapper class like TSubclassOf<T>, FScriptInterface etc., rather than a USomething*
    CPF_HasGetValueTypeHash = 0x0008000000000000,             ///< This property can generate a meaningful hash value.
    CPF_NativeAccessSpecifierPublic = 0x0010000000000000,     ///< Public native access specifier
    CPF_NativeAccessSpecifierProtected = 0x0020000000000000,  ///< Protected native access specifier
    CPF_NativeAccessSpecifierPrivate = 0x0040000000000000,    ///< Private native access specifier
    CPF_SkipSerialization = 0x0080000000000000,               ///< Property shouldn't be serialized, can still be exported to text
};

typedef std::pair<UEPropertyType, std::string> UEPropTypeInfo;

class IProperty
{
protected:
    const void *prop;

public:
    IProperty(const void *object) : prop(object) {}
    virtual std::string GetName() const = 0;
    virtual int32_t GetArrayDim() const = 0;
    virtual int32_t GetSize() const = 0;
    virtual int32_t GetOffset() const = 0;
    virtual uint64_t GetPropertyFlags() const = 0;
    virtual UEPropTypeInfo GetType() const = 0;
    virtual uint8_t GetFieldMask() const = 0;
};

class IUProperty : public IProperty
{
public:
    using IProperty::IProperty;
    IUProperty(const class UE_UProperty *object) : IProperty(object) {}
    virtual std::string GetName() const;
    virtual int32_t GetArrayDim() const;
    virtual int32_t GetSize() const;
    virtual int32_t GetOffset() const;
    virtual uint64_t GetPropertyFlags() const;
    virtual UEPropTypeInfo GetType() const;
    virtual uint8_t GetFieldMask() const;
};

class UE_UProperty : public UE_UField
{
public:
    using UE_UField::UE_UField;
    int32_t GetArrayDim() const;
    int32_t GetSize() const;
    int32_t GetOffset() const;
    uint64_t GetPropertyFlags() const;
    UEPropTypeInfo GetType() const;

    IUProperty GetInterface() const;
    static UE_UClass StaticClass();
};

class UE_UStruct : public UE_UField
{
public:
    using UE_UField::UE_UField;
    UE_UStruct GetSuper() const;
    UE_FField GetChildProperties() const;
    UE_UField GetChildren() const;
    int32_t GetSize() const;
    static UE_UClass StaticClass();

    UE_FField FindChildProp(const std::string &name) const;
    UE_UField FindChild(const std::string &name) const;
};

enum EFunctionFlags : uint32_t
{
    // Function flags.
    FUNC_None = 0x00000000,
    FUNC_Final = 0x00000001,                   // Function is final (prebindable, non-overridable function).
    FUNC_RequiredAPI = 0x00000002,             // Indicates this function is DLL exported/imported.
    FUNC_BlueprintAuthorityOnly = 0x00000004,  // Function will only run if the object has network authority
    FUNC_BlueprintCosmetic = 0x00000008,       // Function is cosmetic in nature and should not be invoked on dedicated servers
                                               // FUNC_                = 0x00000010,   // unused.
                                               // FUNC_                = 0x00000020,   // unused.
    FUNC_Net = 0x00000040,                     // Function is network-replicated.
    FUNC_NetReliable = 0x00000080,             // Function should be sent reliably on the network.
    FUNC_NetRequest = 0x00000100,              // Function is sent to a net service
    FUNC_Exec = 0x00000200,                    // Executable from command line.
    FUNC_Native = 0x00000400,                  // Native function.
    FUNC_Event = 0x00000800,                   // Event function.
    FUNC_NetResponse = 0x00001000,             // Function response from a net service
    FUNC_Static = 0x00002000,                  // Static function.
    FUNC_NetMulticast = 0x00004000,            // Function is networked multicast Server -> All Clients
    FUNC_UbergraphFunction = 0x00008000,       // Function is used as the merge 'ubergraph' for a blueprint, only assigned when using the persistent 'ubergraph' frame
    FUNC_MulticastDelegate = 0x00010000,       // Function is a multi-cast delegate signature (also requires FUNC_Delegate to be set!)
    FUNC_Public = 0x00020000,                  // Function is accessible in all classes (if overridden, parameters must remain unchanged).
    FUNC_Private = 0x00040000,                 // Function is accessible only in the class it is defined in (cannot be overridden, but function name may be reused in subclasses.  IOW: if overridden, parameters don't need to match, and Super.Func() cannot be accessed since it's private.)
    FUNC_Protected = 0x00080000,               // Function is accessible only in the class it is defined in and subclasses (if overridden, parameters much remain unchanged).
    FUNC_Delegate = 0x00100000,                // Function is delegate signature (either single-cast or multi-cast, depending on whether FUNC_MulticastDelegate is set.)
    FUNC_NetServer = 0x00200000,               // Function is executed on servers (set by replication code if passes check)
    FUNC_HasOutParms = 0x00400000,             // function has out (pass by reference) parameters
    FUNC_HasDefaults = 0x00800000,             // function has structs that contain defaults
    FUNC_NetClient = 0x01000000,               // function is executed on clients
    FUNC_DLLImport = 0x02000000,               // function is imported from a DLL
    FUNC_BlueprintCallable = 0x04000000,       // function can be called from blueprint code
    FUNC_BlueprintEvent = 0x08000000,          // function can be overridden/implemented from a blueprint
    FUNC_BlueprintPure = 0x10000000,           // function can be called from blueprint code, and is also pure (produces no side effects). If you set this, you should set FUNC_BlueprintCallable as well.
    FUNC_EditorOnly = 0x20000000,              // function can only be called from an editor scrippt.
    FUNC_Const = 0x40000000,                   // function can be called from blueprint code, and only reads state (never writes state)
    FUNC_NetValidate = 0x80000000,             // function must supply a _Validate implementation
    FUNC_AllFlags = 0xFFFFFFFF,
};

class UE_UFunction : public UE_UStruct
{
public:
    using UE_UStruct::UE_UStruct;
    uintptr_t GetFunc() const;

    int8_t GetNumParams() const;
    int16_t GetParamSize() const;

    uint32_t GetFunctionEFlags() const;
    std::string GetFunctionFlags() const;
    static UE_UClass StaticClass();
};

class UE_UScriptStruct : public UE_UStruct
{
public:
    using UE_UStruct::UE_UStruct;
    static UE_UClass StaticClass();
};

class UE_UClass : public UE_UStruct
{
public:
    using UE_UStruct::UE_UStruct;
    static UE_UClass StaticClass();
};

class UE_UEnum : public UE_UField
{
public:
    using UE_UField::UE_UField;
    TArray<uint8_t> GetNames() const;
    std::string GetName() const;
    static UE_UClass StaticClass();
};

class UE_UDoubleProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UFloatProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UIntProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UInt16Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UInt32Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UInt64Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UInt8Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UUInt16Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UUInt32Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UUInt64Property : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UTextProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UStrProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UStructProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UStruct GetStruct() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UNameProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UObjectPropertyBase : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UClass GetPropertyClass() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UObjectProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UClass GetPropertyClass() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UArrayProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UProperty GetInner() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UByteProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UEnum GetEnum() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UBoolProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    uint8_t GetFieldSize() const;
    uint8_t GetByteOffset() const;
    uint8_t GetByteMask() const;
    uint8_t GetFieldMask() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UEnumProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UProperty GetUnderlayingProperty() const;
    UE_UEnum GetEnum() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UClassProperty : public UE_UObjectPropertyBase
{
public:
    using UE_UObjectPropertyBase::UE_UObjectPropertyBase;
    UE_UClass GetMetaClass() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_USoftClassProperty : public UE_UClassProperty
{
public:
    using UE_UClassProperty::UE_UClassProperty;
    std::string GetTypeStr() const;
};

class UE_USetProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UProperty GetElementProp() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UMapProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UProperty GetKeyProp() const;
    UE_UProperty GetValueProp() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UInterfaceProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    UE_UProperty GetInterfaceClass() const;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UMulticastDelegateProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_UWeakObjectProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_ULazyObjectProperty : public UE_UProperty
{
public:
    using UE_UProperty::UE_UProperty;
    std::string GetTypeStr() const;
    static UE_UClass StaticClass();
};

class UE_FFieldClass
{
protected:
    uint8_t *object;

public:
    UE_FFieldClass(uint8_t *object) : object(object) {};
    UE_FFieldClass() : object(nullptr) {};
    bool operator==(const UE_FFieldClass obj) const { return obj.object == object; };
    bool operator!=(const UE_FFieldClass obj) const { return obj.object != object; };
    uint8_t *GetAddress() const { return object; }
    operator uint8_t *() const { return object; };
    operator bool() const { return object != nullptr; }

    template <typename Base>
    Base Cast() const
    {
        return Base(object);
    }
    std::string GetName() const;
};

class UE_FField
{
protected:
    uint8_t *object;

public:
    UE_FField(uint8_t *object) : object(object) {}
    UE_FField() : object(nullptr) {}
    bool operator==(const UE_FField obj) const { return obj.object == object; };
    bool operator!=(const UE_FField obj) const { return obj.object != object; };
    uint8_t *GetAddress() const { return object; }
    operator uint8_t *() const { return object; };
    operator bool() const { return object != nullptr; }

    UE_FField GetNext() const;
    std::string GetName() const;
    UE_FFieldClass GetClass() const;

    template <typename Base>
    Base Cast() const
    {
        return Base(object);
    }
};

class IFProperty : public IProperty
{
public:
    IFProperty(const class UE_FProperty *object) : IProperty(object) {}
    virtual std::string GetName() const;
    virtual int32_t GetArrayDim() const;
    virtual int32_t GetSize() const;
    virtual int32_t GetOffset() const;
    virtual uint64_t GetPropertyFlags() const;
    virtual UEPropTypeInfo GetType() const;
    virtual uint8_t GetFieldMask() const;
};

class UE_FProperty : public UE_FField
{
public:
    using UE_FField::UE_FField;
    int32_t GetArrayDim() const;
    int32_t GetSize() const;
    int32_t GetOffset() const;
    uint64_t GetPropertyFlags() const;
    UEPropTypeInfo GetType() const;
    IFProperty GetInterface() const;

    uintptr_t FindSubFPropertyBaseOffset() const;
};

class UE_FStructProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_UStruct GetStruct() const;
    std::string GetTypeStr() const;
};

class UE_FObjectPropertyBase : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_UClass GetPropertyClass() const;
    std::string GetTypeStr() const;
};

class UE_FArrayProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_FProperty GetInner() const;
    std::string GetTypeStr() const;
};

class UE_FByteProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_UEnum GetEnum() const;
    std::string GetTypeStr() const;
};

class UE_FBoolProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    uint8_t GetFieldSize() const;
    uint8_t GetByteOffset() const;
    uint8_t GetByteMask() const;
    uint8_t GetFieldMask() const;
    std::string GetTypeStr() const;
};

class UE_FEnumProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_FProperty GetUnderlayingProperty() const;
    UE_UEnum GetEnum() const;
    std::string GetTypeStr() const;
};

class UE_FClassProperty : public UE_FObjectPropertyBase
{
public:
    using UE_FObjectPropertyBase::UE_FObjectPropertyBase;
    UE_UClass GetMetaClass() const;
    std::string GetTypeStr() const;
};

class UE_FSoftClassProperty : public UE_FClassProperty
{
public:
    using UE_FClassProperty::UE_FClassProperty;
    std::string GetTypeStr() const;
};

class UE_FSetProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_FProperty GetElementProp() const;
    std::string GetTypeStr() const;
};

class UE_FMapProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_FProperty GetKeyProp() const;
    UE_FProperty GetValueProp() const;
    std::string GetTypeStr() const;
};

class UE_FInterfaceProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_UClass GetInterfaceClass() const;
    std::string GetTypeStr() const;
};

class UE_FFieldPathProperty : public UE_FProperty
{
public:
    using UE_FProperty::UE_FProperty;
    UE_FName GetPropertyName() const;
    std::string GetTypeStr() const;
};

template <typename T>
bool UE_UObject::IsA() const
{
    auto cmp = T::StaticClass();
    if (!cmp)
    {
        return false;
    }

    return IsA(cmp);
}
