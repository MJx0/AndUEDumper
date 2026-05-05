#pragma once

static const char* kUECoreBasicH = R"UECoreBasicH(
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <type_traits>

#include "UnrealContainers.h"

namespace SDK {

using namespace UC;

namespace InSDKUtils
{
	inline uintptr_t s_ImageBase = 0;

	inline uintptr_t GetImageBase()
	{
		return s_ImageBase;
	}

	template<typename FuncType>
	inline FuncType GetVirtualFunction(const void* ObjectInstance, int32 Index)
	{
		void** VTable = *reinterpret_cast<void***>(const_cast<void*>(ObjectInstance));

		return reinterpret_cast<FuncType>(VTable[Index]);
	}

	template<typename FuncType, typename... ParamTypes>
	requires std::invocable<FuncType, ParamTypes...>
	inline auto CallGameFunction(FuncType Function, ParamTypes&&... Args)
	{
		return Function(std::forward<ParamTypes>(Args)...);
	}
}


template<int32 Len>
struct StringLiteral
{
	char Chars[Len];

	consteval StringLiteral(const char(&String)[Len])
	{
		std::copy_n(String, Len, Chars);
	}

	operator std::string() const
	{
		return static_cast<const char*>(Chars);
	}
};

// Forward declarations because in-line forward declarations make the compiler think 'StaticClassImpl()' is a class template
class UClass;
class UObject;
class UFunction;

class FName;

namespace BasicFilesImpleUtils
{
	// Helper functions for StaticClassImpl and StaticBPGeneratedClassImpl
	UClass* FindClassByName(const std::string& Name);
	UClass* FindClassByFullName(const std::string& Name);

	std::string GetObjectName(class UClass* Class);
	int32 GetObjectIndex(class UClass* Class);

	/* FName represented as a uint64. */
	uint64 GetObjFNameAsUInt64(class UClass* Class);

	UObject* GetObjectByIndex(int32 Index);

	UFunction* FindFunctionByFName(const FName* Name);
}

template<StringLiteral Name, bool bIsFullName = false>
class UClass* StaticClassImpl()
{
	static class UClass* Clss = nullptr;

	if (Clss == nullptr)
	{
		if constexpr (bIsFullName) {
			Clss = BasicFilesImpleUtils::FindClassByFullName(Name);
		}
		else /* default */ {
			Clss = BasicFilesImpleUtils::FindClassByName(Name);
		}
	}

	return Clss;
}

template<StringLiteral Name, bool bIsFullName = false, StringLiteral NonFullName = "">
class UClass* StaticBPGeneratedClassImpl()
{
	/* Could be external function, not really unique to this StaticClass functon */
	static auto SetClassIndex = [](UClass* Class, int32& Index, uint64& ClassName) -> UClass*
	{
		if (Class)
		{
			Index = BasicFilesImpleUtils::GetObjectIndex(Class);
			ClassName = BasicFilesImpleUtils::GetObjFNameAsUInt64(Class);
		}

		return Class;
	};

	static int32 ClassIdx = 0x0;
	static uint64 ClassName = 0x0;

	/* Use the full name to find an object */
	if constexpr (bIsFullName)
	{
		if (ClassIdx == 0x0) [[unlikely]]
			return SetClassIndex(BasicFilesImpleUtils::FindClassByFullName(Name), ClassIdx, ClassName);

		// UClass* ClassObj = static_cast<UClass*>(BasicFilesImpleUtils::GetObjectByIndex(ClassIdx));
		UClass* ClassObj = (UClass*)(BasicFilesImpleUtils::GetObjectByIndex(ClassIdx));

		/* Could use cast flags too to save some string comparisons */
		if (!ClassObj || BasicFilesImpleUtils::GetObjFNameAsUInt64(ClassObj) != ClassName)
			return SetClassIndex(BasicFilesImpleUtils::FindClassByFullName(Name), ClassIdx, ClassName);

		return ClassObj;
	}
	else /* Default, use just the name to find an object*/
	{
		if (ClassIdx == 0x0) [[unlikely]]
			return SetClassIndex(BasicFilesImpleUtils::FindClassByName(Name), ClassIdx, ClassName);

		// UClass* ClassObj = static_cast<UClass*>(BasicFilesImpleUtils::GetObjectByIndex(ClassIdx));
		UClass* ClassObj = (UClass*)(BasicFilesImpleUtils::GetObjectByIndex(ClassIdx));

		/* Could use cast flags too to save some string comparisons */
		if (!ClassObj || BasicFilesImpleUtils::GetObjFNameAsUInt64(ClassObj) != ClassName)
			return SetClassIndex(BasicFilesImpleUtils::FindClassByName(Name), ClassIdx, ClassName);

		return ClassObj;
	}
}

template<class ClassType>
ClassType* GetDefaultObjImpl()
{
	return reinterpret_cast<ClassType*>(ClassType::StaticClass()->DefaultObject);
}

template <typename To, typename From>
inline To* Cast(From* Src)
{
	if (!Src)
		return nullptr;

	if (Src->IsA(To::StaticClass()))
		return static_cast<To*>(Src);

	return nullptr;
}

struct FUObjectItem final
{
public:
	class UObject*                                Object;                                            // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_8[0x10];                                       // 0x0008(0x0010)(Fixing Struct Size After Last Property [ Dumper-7 ])
};

class TUObjectArray final
{
public:
	static constexpr auto DecryptPtr = [](void* ObjPtr) -> uint8*
	{
		return reinterpret_cast<uint8*>(ObjPtr);
	};

	int32                                         NumElementsPerChunk = 0x10000;

	struct FUObjectItem**                         Objects;                                           // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_8[0x8];                                        // 0x0008(0x0008)(Fixing Size After Last Property [ Dumper-7 ])
	int32                                         MaxElements;                                       // 0x0010(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32                                         NumElements;                                       // 0x0014(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32                                         MaxChunks;                                         // 0x0018(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32                                         NumChunks;                                         // 0x001C(0x0004)(NOT AUTO-GENERATED PROPERTY)

public:
	inline int32 Num() const
	{
		return NumElements;
	}

	FUObjectItem** GetDecryptedObjPtr() const
	{
		return reinterpret_cast<FUObjectItem**>(DecryptPtr(Objects));
	}

	inline class UObject* GetByIndex(const int32 Index) const
	{
		if (Index < 0 || Index >= NumElements || !Objects)
			return nullptr;

		if (NumElementsPerChunk <= 0)
			return *reinterpret_cast<UObject**>((uintptr_t)Objects + Index * sizeof(FUObjectItem) + offsetof(FUObjectItem, Object));

		const int32_t ChunkIndex = Index / NumElementsPerChunk;
		const int32_t WithinChunkIndex = Index % NumElementsPerChunk;

		// if (ChunkIndex >= NumChunks) return nullptr;

		uint64_t chunk = *reinterpret_cast<uint64_t*>(Objects + ChunkIndex);
		if (!chunk)
			return nullptr;

		return *reinterpret_cast<UObject**>(chunk + (WithinChunkIndex * sizeof(FUObjectItem)) + offsetof(FUObjectItem, Object));
	}

	void ForEachObject(const std::function<bool(UObject*)> &callback) const
	{
		if (!callback) return;

		for (int32_t i = 0; i < NumElements; i++)
		{
			UObject* object = GetByIndex(i);
			if (!object) continue;

			if (callback(object)) return;
		}
	}
};

struct TUObjectArrayWrapper
{
private:
	friend class UObject;

private:
	void* GObjectsAddress = nullptr;

private:
	TUObjectArrayWrapper() = default;

public:
	TUObjectArrayWrapper(TUObjectArrayWrapper&&) = delete;
	TUObjectArrayWrapper(const TUObjectArrayWrapper&) = delete;

	TUObjectArrayWrapper& operator=(TUObjectArrayWrapper&&) = delete;
	TUObjectArrayWrapper& operator=(const TUObjectArrayWrapper&) = delete;

public:
	inline void InitManually(void* GObjectsAddressParameter)
	{
		GObjectsAddress = GObjectsAddressParameter;
	}

	inline class TUObjectArray* operator->()
	{
		return reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}

	inline TUObjectArray& operator*() const
	{
		return *reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}

	inline operator const void* ()
	{
		return GObjectsAddress;
	}

	inline class TUObjectArray* GetTypedPtr()
	{
		return reinterpret_cast<class TUObjectArray*>(GObjectsAddress);
	}
};

class FName final
{
public:
	static inline std::function<std::string(int32_t)> s_NameResolver;

#define bWITH_CASE_PRESERVING_NAME false
#if !bWITH_CASE_PRESERVING_NAME
	union {
#endif
	int32                                         ComparisonIndex;                                   // 0x0000(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32                                         DisplayIndex;                                      // 0x0004(0x0004)(NOT AUTO-GENERATED PROPERTY)
#if !bWITH_CASE_PRESERVING_NAME
	};
#endif
	uint32                                        Number;                                            // 0x0008(0x0004)(NOT AUTO-GENERATED PROPERTY)

public:
	int32 GetDisplayIndex() const
	{
		return DisplayIndex;
	}

	static std::string GetPlainANSIString(const FName* Name)
	{
		if (s_NameResolver)
			return s_NameResolver(Name->ComparisonIndex);
		return {};
	}

	std::string GetRawString() const
	{
		return GetPlainANSIString(this);
	}
	
	std::string ToString() const
	{
		std::string OutputString = GetRawString();
	
		size_t pos = OutputString.rfind('/');
	
		if (pos == std::string::npos)
			return OutputString;
	
		return OutputString.substr(pos + 1);
	}

	const char* ToCString() const
	{
		return ToString().c_str();
	}
	
	bool operator==(const FName& Other) const
	{
		return ComparisonIndex == Other.ComparisonIndex && Number == Other.Number;
	}
	bool operator!=(const FName& Other) const
	{
		return ComparisonIndex != Other.ComparisonIndex || Number != Other.Number;
	}
};

template<typename ClassType>
class TSubclassOf
{
	class UClass* ClassPtr;

public:
	TSubclassOf() = default;

	inline TSubclassOf(UClass* Class)
		: ClassPtr(Class)
	{
	}

	inline UClass* Get()
	{
		return ClassPtr;
	}

	inline operator UClass*() const
	{
		return ClassPtr;
	}

	template<typename Target, typename = std::enable_if<std::is_base_of_v<Target, ClassType>, bool>::type>
	inline operator TSubclassOf<Target>() const
	{
		return ClassPtr;
	}

	inline UClass* operator->()
	{
		return ClassPtr;
	}

	inline TSubclassOf& operator=(UClass* Class)
	{
		ClassPtr = Class;

		return *this;
	}

	inline bool operator==(const TSubclassOf& Other) const
	{
		return ClassPtr == Other.ClassPtr;
	}

	inline bool operator!=(const TSubclassOf& Other) const
	{
		return ClassPtr != Other.ClassPtr;
	}

	inline bool operator==(UClass* Other) const
	{
		return ClassPtr == Other;
	}

	inline bool operator!=(UClass* Other) const
	{
		return ClassPtr != Other;
	}
};

namespace FTextImpl
{
// Predefined struct FTextData
// 0x0038 (0x0038 - 0x0000)
class FTextData final
{
public:
	uint8                                         Pad_0[0x28];                                       // 0x0000(0x0028)(Fixing Size After Last Property [ Dumper-7 ])
	class FString                                 TextSource;                                        // 0x0028(0x0010)(NOT AUTO-GENERATED PROPERTY)
};
}

// Predefined struct FText
// 0x0018 (0x0018 - 0x0000)
class FText final
{
public:
	class FTextImpl::FTextData*                   TextData;                                          // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_8[0x10];                                       // 0x0008(0x0010)(Fixing Struct Size After Last Property [ Dumper-7 ])

public:
	const class FString& GetStringRef() const
	{
		if (!TextData)
			return FString();
		return TextData->TextSource;
	}
	std::string ToString() const
	{
		if (!TextData)
			return "";
		return TextData->TextSource.ToString();
	}
	bool IsValid() const
	{
		return TextData != nullptr;
	}
};

class FWeakObjectPtr
{
public:
	int32_t                                       ObjectIndex;                                       // 0x0000(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32_t                                       ObjectSerialNumber;                                // 0x0004(0x0004)(NOT AUTO-GENERATED PROPERTY)

public:
	class UObject* Get() const;
	class UObject* operator->() const;
	bool operator==(const FWeakObjectPtr& Other) const;
	bool operator!=(const FWeakObjectPtr& Other) const;
	bool operator==(const class UObject* Other) const;
	bool operator!=(const class UObject* Other) const;
};

template<typename UEType>
class TWeakObjectPtr : public FWeakObjectPtr
{
public:
	UEType* Get() const {
		return static_cast<UEType*>(FWeakObjectPtr::Get());
	}

    bool IsValid() const {
        return Get() != nullptr;
    }

	UEType* operator->() const {
		return static_cast<UEType*>(FWeakObjectPtr::Get());
	}
};

// Predefined struct FUniqueObjectGuid
// 0x0010 (0x0010 - 0x0000)
class FUniqueObjectGuid final
{
public:
	uint32                                        A;                                                 // 0x0000(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint32                                        B;                                                 // 0x0004(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint32                                        C;                                                 // 0x0008(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint32                                        D;                                                 // 0x000C(0x0004)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FUniqueObjectGuid) == 0x000004, "Wrong alignment on FUniqueObjectGuid");
static_assert(sizeof(FUniqueObjectGuid) == 0x000010, "Wrong size on FUniqueObjectGuid");
static_assert(offsetof(FUniqueObjectGuid, A) == 0x000000, "Member 'FUniqueObjectGuid::A' has a wrong offset!");
static_assert(offsetof(FUniqueObjectGuid, B) == 0x000004, "Member 'FUniqueObjectGuid::B' has a wrong offset!");
static_assert(offsetof(FUniqueObjectGuid, C) == 0x000008, "Member 'FUniqueObjectGuid::C' has a wrong offset!");
static_assert(offsetof(FUniqueObjectGuid, D) == 0x00000C, "Member 'FUniqueObjectGuid::D' has a wrong offset!");

// Predefined struct TPersistentObjectPtr
// 0x0000 (0x0000 - 0x0000)
template<typename TObjectID>
class TPersistentObjectPtr
{
public:
	FWeakObjectPtr                                WeakPtr;                                           // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	int32                                         TagAtLastTest;                                     // 0x0008(0x0004)(NOT AUTO-GENERATED PROPERTY)
	TObjectID                                     ObjectID;                                          // 0x000C(0x0000)(NOT AUTO-GENERATED PROPERTY)

public:
	class UObject* Get() const
	{
		return WeakPtr.Get();
	}
	class UObject* operator->() const
	{
		return WeakPtr.Get();
	}
};

template<typename UEType>
class TLazyObjectPtr : public TPersistentObjectPtr<FUniqueObjectGuid>
{
public:
	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};

namespace FakeSoftObjectPtr
{

// ScriptStruct CoreUObject.SoftObjectPath
// 0x0020 (0x0020 - 0x0000)
struct FSoftObjectPath
{
public:
	class FName                                   AssetPathName;                                     // 0x0000(0x000C)(ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	uint8                                         Pad_C[0x4];                                        // 0x000C(0x0004)(Fixing Size After Last Property [ Dumper-7 ])
	class FString                                 SubPathString;                                     // 0x0010(0x0010)(ZeroConstructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
};
static_assert(alignof(FSoftObjectPath) == 0x000008, "Wrong alignment on FSoftObjectPath");
static_assert(sizeof(FSoftObjectPath) == 0x000020, "Wrong size on FSoftObjectPath");
static_assert(offsetof(FSoftObjectPath, AssetPathName) == 0x000000, "Member 'FSoftObjectPath::AssetPathName' has a wrong offset!");
static_assert(offsetof(FSoftObjectPath, SubPathString) == 0x000010, "Member 'FSoftObjectPath::SubPathString' has a wrong offset!");

}

class FSoftObjectPtr : public TPersistentObjectPtr<FakeSoftObjectPtr::FSoftObjectPath>
{
};

template<typename UEType>
class TSoftObjectPtr : public FSoftObjectPtr
{
public:
	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};

template<typename UEType>
class TSoftClassPtr : public FSoftObjectPtr
{
public:
	UEType* Get() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
	UEType* operator->() const
	{
		return static_cast<UEType*>(TPersistentObjectPtr::Get());
	}
};

// Predefined struct FScriptInterface
// 0x0010 (0x0010 - 0x0000)
class FScriptInterface
{
public:
	UObject*                                      ObjectPointer;                                     // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	void*                                         InterfacePointer;                                  // 0x0008(0x0008)(NOT AUTO-GENERATED PROPERTY)

public:
	class UObject* GetObjectRef() const
	{
		return ObjectPointer;
	}
	
	void* GetInterfaceRef() const
	{
		return InterfacePointer;
	}
	
};
static_assert(alignof(FScriptInterface) == 0x000008, "Wrong alignment on FScriptInterface");
static_assert(sizeof(FScriptInterface) == 0x000010, "Wrong size on FScriptInterface");
static_assert(offsetof(FScriptInterface, ObjectPointer) == 0x000000, "Member 'FScriptInterface::ObjectPointer' has a wrong offset!");
static_assert(offsetof(FScriptInterface, InterfacePointer) == 0x000008, "Member 'FScriptInterface::InterfacePointer' has a wrong offset!");

// Predefined struct TScriptInterface
// 0x0000 (0x0010 - 0x0010)
template<class InterfaceType>
class TScriptInterface final : public FScriptInterface
{
};

// Predefined struct FFieldPath
// 0x0020 (0x0020 - 0x0000)
class FFieldPath
{
public:
	class FField*                                 ResolvedField;                                     // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	TWeakObjectPtr<class UStruct>                 ResolvedOwner;                                     // 0x0008(0x0008)(NOT AUTO-GENERATED PROPERTY)
	TArray<FName>                                 Path;                                              // 0x0010(0x0010)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FFieldPath) == 0x000008, "Wrong alignment on FFieldPath");
static_assert(sizeof(FFieldPath) == 0x000020, "Wrong size on FFieldPath");
static_assert(offsetof(FFieldPath, ResolvedField) == 0x000000, "Member 'FFieldPath::ResolvedField' has a wrong offset!");
static_assert(offsetof(FFieldPath, ResolvedOwner) == 0x000008, "Member 'FFieldPath::ResolvedOwner' has a wrong offset!");
static_assert(offsetof(FFieldPath, Path) == 0x000010, "Member 'FFieldPath::Path' has a wrong offset!");

// Predefined struct TFieldPath
// 0x0000 (0x0020 - 0x0020)
template<class PropertyType>
class TFieldPath final : public FFieldPath
{
};


template<typename OptionalType, bool bIsIntrusiveUnsetCheck = false>
class TOptional
{
private:
	template<int32 TypeSize>
	struct OptionalWithBool
	{
		static_assert(TypeSize > 0x0, "TOptional can not store an empty type!");

		uint8 Value[TypeSize];
		bool bIsSet;
	};

private:
	using ValueType = std::conditional_t<bIsIntrusiveUnsetCheck, uint8[sizeof(OptionalType)], OptionalWithBool<sizeof(OptionalType)>>;

private:
	alignas(OptionalType) ValueType StoredValue;

private:
	inline uint8* GetValueBytes()
	{
		if constexpr (!bIsIntrusiveUnsetCheck)
			return StoredValue.Value;

		return StoredValue;
	}

	inline const uint8* GetValueBytes() const
	{
		if constexpr (!bIsIntrusiveUnsetCheck)
			return StoredValue.Value;

		return StoredValue;
	}
public:

	inline OptionalType& GetValueRef()
	{
		return *reinterpret_cast<OptionalType*>(GetValueBytes());
	}

	inline const OptionalType& GetValueRef() const
	{
		return *reinterpret_cast<const OptionalType*>(GetValueBytes());
	}

	inline bool IsSet() const
	{
		if constexpr (!bIsIntrusiveUnsetCheck)
			return StoredValue.bIsSet;

		constexpr char ZeroBytes[sizeof(OptionalType)];

		return memcmp(GetValueBytes(), &ZeroBytes, sizeof(OptionalType)) == 0;
	}

	inline explicit operator bool() const
	{
		return IsSet();
	}
};


// Predefined struct FScriptDelegate
// 0x0014 (0x0014 - 0x0000)
struct FScriptDelegate
{
public:
	FWeakObjectPtr                                Object;                                            // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	FName                                         FunctionName;                                      // 0x0008(0x000C)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FScriptDelegate) == 0x000004, "Wrong alignment on FScriptDelegate");
static_assert(sizeof(FScriptDelegate) == 0x000010, "Wrong size on FScriptDelegate");
static_assert(offsetof(FScriptDelegate, Object) == 0x000000, "Member 'FScriptDelegate::Object' has a wrong offset!");
static_assert(offsetof(FScriptDelegate, FunctionName) == 0x000008, "Member 'FScriptDelegate::FunctionName' has a wrong offset!");

// Predefined delegate placeholder structs. The dumper synthesizes these
// names from FProperty kinds in per-package SDK output (e.g. dumped fields
// like `struct FMulticastInlineDelegate OnFoo;`); UE itself does not expose
// them as publicly-named types. Sizes mirror the FProperty ElementSize the
// dumper emits for each kind so the surrounding struct layout stays
// byte-correct. Listed in kBuiltinIdents in UPackageGenerator.cpp so they
// don't appear in dep tracking.
struct FDelegate                 { uint8 _opaque[0x10]; };
struct FMulticastDelegate        { uint8 _opaque[0x10]; };
struct FMulticastInlineDelegate  { uint8 _opaque[0x10]; };
struct FMulticastSparseDelegate  { uint8 _opaque[0x01]; };
static_assert(sizeof(FDelegate)                 == 0x10, "Wrong size on FDelegate");
static_assert(sizeof(FMulticastDelegate)        == 0x10, "Wrong size on FMulticastDelegate");
static_assert(sizeof(FMulticastInlineDelegate)  == 0x10, "Wrong size on FMulticastInlineDelegate");
static_assert(sizeof(FMulticastSparseDelegate)  == 0x01, "Wrong size on FMulticastSparseDelegate");

// Predefined struct TDelegate
// 0x0028 (0x0028 - 0x0000)
template<typename FunctionSignature>
class TDelegate
{
public:
	// struct InvalidUseOfTDelegate                  TemplateParamIsNotAFunctionSignature;              // 0x0000(0x0000)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_0[0x28];                                       // 0x0000(0x0028)(Fixing Struct Size After Last Property [ Dumper-7 ])
};

// Predefined struct TDelegate<Ret(Args...)>
// 0x0028 (0x0028 - 0x0000)
template<typename Ret, typename... Args>
class TDelegate<Ret(Args...)>
{
public:
	FScriptDelegate                               BoundFunction;                                     // 0x0000(0x0014)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_14[0x14];                                      // 0x0014(0x0014)(Fixing Struct Size After Last Property [ Dumper-7 ])
};

// Predefined struct TMulticastInlineDelegate
// 0x0010 (0x0010 - 0x0000)
template<typename FunctionSignature>
class TMulticastInlineDelegate
{
public:
	// struct InvalidUseOfTMulticastInlineDelegate   TemplateParamIsNotAFunctionSignature;              // 0x0000(0x0014)(NOT AUTO-GENERATED PROPERTY)
};

// Predefined struct TMulticastInlineDelegate<Ret(Args...)>
// 0x0000 (0x0000 - 0x0000)
template<typename Ret, typename... Args>
class TMulticastInlineDelegate<Ret(Args...)>
{
public:
	TArray<FScriptDelegate>                       InvocationList;                                    // 0x0000(0x0010)(NOT AUTO-GENERATED PROPERTY)
};

#define UE_ENUM_OPERATORS(EEnumClass)																																	\
																																										\
inline constexpr EEnumClass operator|(EEnumClass Left, EEnumClass Right)																								\
{																																										\
	return (EEnumClass)((std::underlying_type<EEnumClass>::type)(Left) | (std::underlying_type<EEnumClass>::type)(Right));												\
}																																										\
																																										\
inline constexpr EEnumClass& operator|=(EEnumClass& Left, EEnumClass Right)																								\
{																																										\
	return (EEnumClass&)((std::underlying_type<EEnumClass>::type&)(Left) |= (std::underlying_type<EEnumClass>::type)(Right));											\
}																																										\
																																										\
inline bool operator&(EEnumClass Left, EEnumClass Right)																												\
{																																										\
	return (((std::underlying_type<EEnumClass>::type)(Left) & (std::underlying_type<EEnumClass>::type)(Right)) == (std::underlying_type<EEnumClass>::type)(Right));		\
}

enum class EObjectFlags : int32
{
	NoFlags							= 0x00000000,

	Public							= 0x00000001,
	Standalone						= 0x00000002,
	MarkAsNative					= 0x00000004,
	Transactional					= 0x00000008,
	ClassDefaultObject				= 0x00000010,
	ArchetypeObject					= 0x00000020,
	Transient						= 0x00000040,

	MarkAsRootSet					= 0x00000080,
	TagGarbageTemp					= 0x00000100,

	NeedInitialization				= 0x00000200,
	NeedLoad						= 0x00000400,
	KeepForCooker					= 0x00000800,
	NeedPostLoad					= 0x00001000,
	NeedPostLoadSubobjects			= 0x00002000,
	NewerVersionExists				= 0x00004000,
	BeginDestroyed					= 0x00008000,
	FinishDestroyed					= 0x00010000,

	BeingRegenerated				= 0x00020000,
	DefaultSubObject				= 0x00040000,
	WasLoaded						= 0x00080000,
	TextExportTransient				= 0x00100000,
	LoadCompleted					= 0x00200000,
	InheritableComponentTemplate	= 0x00400000,
	DuplicateTransient				= 0x00800000,
	StrongRefOnFrame				= 0x01000000,
	NonPIEDuplicateTransient		= 0x02000000,
	Dynamic							= 0x04000000,
	WillBeLoaded					= 0x08000000,
	HasExternalPackage				= 0x10000000,

	MirroredGarbage					= 0x40000000,
	// AllocatedInSharedPage			= 0x80000000,
};

enum class EFunctionFlags : uint32
{
	FUNC_None				        = 0x00000000,

	FUNC_Final				        = 0x00000001,	// Function is final (prebindable, non-overridable function).
	FUNC_RequiredAPI			    = 0x00000002,	// Indicates this function is DLL exported/imported.
	FUNC_BlueprintAuthorityOnly     = 0x00000004,   // Function will only run if the object has network authority
	FUNC_BlueprintCosmetic	        = 0x00000008,   // Function is cosmetic in nature and should not be invoked on dedicated servers
	// FUNC_				        = 0x00000010,   // unused.
	// FUNC_				        = 0x00000020,   // unused.
	FUNC_Net				        = 0x00000040,   // Function is network-replicated.
	FUNC_NetReliable		        = 0x00000080,   // Function should be sent reliably on the network.
	FUNC_NetRequest			        = 0x00000100,	// Function is sent to a net service
	FUNC_Exec				        = 0x00000200,	// Executable from command line.
	FUNC_Native				        = 0x00000400,	// Native function.
	FUNC_Event				        = 0x00000800,   // Event function.
	FUNC_NetResponse		        = 0x00001000,   // Function response from a net service
	FUNC_Static				        = 0x00002000,   // Static function.
	FUNC_NetMulticast		        = 0x00004000,	// Function is networked multicast Server -> All Clients
	FUNC_UbergraphFunction	        = 0x00008000,   // Function is used as the merge 'ubergraph' for a blueprint, only assigned when using the persistent 'ubergraph' frame
	FUNC_MulticastDelegate	        = 0x00010000,	// Function is a multi-cast delegate signature (also requires FUNC_Delegate to be set!)
	FUNC_Public				        = 0x00020000,	// Function is accessible in all classes (if overridden, parameters must remain unchanged).
	FUNC_Private			        = 0x00040000,	// Function is accessible only in the class it is defined in (cannot be overridden, but function name may be reused in subclasses.  IOW: if overridden, parameters don't need to match, and Super.Func() cannot be accessed since it's private.)
	FUNC_Protected			        = 0x00080000,	// Function is accessible only in the class it is defined in and subclasses (if overridden, parameters much remain unchanged).
	FUNC_Delegate			        = 0x00100000,	// Function is delegate signature (either single-cast or multi-cast, depending on whether FUNC_MulticastDelegate is set.)
	FUNC_NetServer			        = 0x00200000,	// Function is executed on servers (set by replication code if passes check)
	FUNC_HasOutParms		        = 0x00400000,	// function has out (pass by reference) parameters
	FUNC_HasDefaults		        = 0x00800000,	// function has structs that contain defaults
	FUNC_NetClient			        = 0x01000000,	// function is executed on clients
	FUNC_DLLImport			        = 0x02000000,	// function is imported from a DLL
	FUNC_BlueprintCallable	        = 0x04000000,	// function can be called from blueprint code
	FUNC_BlueprintEvent		        = 0x08000000,	// function can be overridden/implemented from a blueprint
	FUNC_BlueprintPure		        = 0x10000000,	// function can be called from blueprint code, and is also pure (produces no side effects). If you set this, you should set FUNC_BlueprintCallable as well.
	FUNC_EditorOnly			        = 0x20000000,	// function can only be called from an editor script.
	FUNC_Const				        = 0x40000000,	// function can be called from blueprint code, and only reads state (never writes state)
	FUNC_NetValidate		        = 0x80000000,	// function must supply a _Validate implementation

	FUNC_AllFlags		            = 0xFFFFFFFF,
};

enum class EClassFlags : uint32
{
	CLASS_None						= 0x00000000u,
	Abstract						= 0x00000001u,
	DefaultConfig					= 0x00000002u,
	Config							= 0x00000004u,
	Transient						= 0x00000008u,
	Parsed							= 0x00000010u,
	MatchedSerializers				= 0x00000020u,
	ProjectUserConfig				= 0x00000040u,
	Native							= 0x00000080u,
	NoExport						= 0x00000100u,
	NotPlaceable					= 0x00000200u,
	PerObjectConfig					= 0x00000400u,
	ReplicationDataIsSetUp			= 0x00000800u,
	EditInlineNew					= 0x00001000u,
	CollapseCategories				= 0x00002000u,
	Interface						= 0x00004000u,
	CustomConstructor				= 0x00008000u,
	Const							= 0x00010000u,
	LayoutChanging					= 0x00020000u,
	CompiledFromBlueprint			= 0x00040000u,
	MinimalAPI						= 0x00080000u,
	RequiredAPI						= 0x00100000u,
	DefaultToInstanced				= 0x00200000u,
	TokenStreamAssembled			= 0x00400000u,
	HasInstancedReference			= 0x00800000u,
	Hidden							= 0x01000000u,
	Deprecated						= 0x02000000u,
	HideDropDown					= 0x04000000u,
	GlobalUserConfig				= 0x08000000u,
	Intrinsic						= 0x10000000u,
	Constructed						= 0x20000000u,
	ConfigDoNotCheckDefaults		= 0x40000000u,
	NewerVersionExists				= 0x80000000u,
};

enum class EClassCastFlags : uint64
{
	None = 0x0000000000000000,

	Field								= 0x0000000000000001,
	Int8Property						= 0x0000000000000002,
	Enum								= 0x0000000000000004,
	Struct								= 0x0000000000000008,
	ScriptStruct						= 0x0000000000000010,
	Class								= 0x0000000000000020,
	ByteProperty						= 0x0000000000000040,
	IntProperty							= 0x0000000000000080,
	FloatProperty						= 0x0000000000000100,
	UInt64Property						= 0x0000000000000200,
	ClassProperty						= 0x0000000000000400,
	UInt32Property						= 0x0000000000000800,
	InterfaceProperty					= 0x0000000000001000,
	NameProperty						= 0x0000000000002000,
	StrProperty							= 0x0000000000004000,
	Property							= 0x0000000000008000,
	ObjectProperty						= 0x0000000000010000,
	BoolProperty						= 0x0000000000020000,
	UInt16Property						= 0x0000000000040000,
	Function							= 0x0000000000080000,
	StructProperty						= 0x0000000000100000,
	ArrayProperty						= 0x0000000000200000,
	Int64Property						= 0x0000000000400000,
	DelegateProperty					= 0x0000000000800000,
	NumericProperty						= 0x0000000001000000,
	MulticastDelegateProperty			= 0x0000000002000000,
	ObjectPropertyBase					= 0x0000000004000000,
	WeakObjectProperty					= 0x0000000008000000,
	LazyObjectProperty					= 0x0000000010000000,
	SoftObjectProperty					= 0x0000000020000000,
	TextProperty						= 0x0000000040000000,
	Int16Property						= 0x0000000080000000,
	DoubleProperty						= 0x0000000100000000,
	SoftClassProperty					= 0x0000000200000000,
	Package								= 0x0000000400000000,
	Level								= 0x0000000800000000,
	Actor								= 0x0000001000000000,
	PlayerController					= 0x0000002000000000,
	Pawn								= 0x0000004000000000,
	SceneComponent						= 0x0000008000000000,
	PrimitiveComponent					= 0x0000010000000000,
	SkinnedMeshComponent				= 0x0000020000000000,
	SkeletalMeshComponent				= 0x0000040000000000,
	Blueprint							= 0x0000080000000000,
	DelegateFunction					= 0x0000100000000000,
	StaticMeshComponent					= 0x0000200000000000,
	MapProperty							= 0x0000400000000000,
	SetProperty							= 0x0000800000000000,
	EnumProperty						= 0x0001000000000000,
	USparseDelegateFunction				= 0x0002000000000000,
	MulticastInlineDelegateProperty	    = 0x0004000000000000,
	MulticastSparseDelegateProperty	    = 0x0008000000000000,
	FieldPathProperty					= 0x0010000000000000,
	LargeWorldCoordinatesRealProperty	= 0x0080000000000000,
	OptionalProperty					= 0x0100000000000000,
	VValueProperty						= 0x0200000000000000,
	UVerseVMClass						= 0x0400000000000000,
	VRestValueProperty					= 0x0800000000000000,
};

enum class EPropertyFlags : uint64
{
	None								= 0x0000000000000000,

	Edit								= 0x0000000000000001,
	ConstParm							= 0x0000000000000002,
	BlueprintVisible					= 0x0000000000000004,
	ExportObject						= 0x0000000000000008,
	BlueprintReadOnly					= 0x0000000000000010,
	Net									= 0x0000000000000020,
	EditFixedSize						= 0x0000000000000040,
	Parm								= 0x0000000000000080,
	OutParm								= 0x0000000000000100,
	ZeroConstructor						= 0x0000000000000200,
	ReturnParm							= 0x0000000000000400,
	DisableEditOnTemplate 				= 0x0000000000000800,

	Transient							= 0x0000000000002000,
	Config								= 0x0000000000004000,

	DisableEditOnInstance				= 0x0000000000010000,
	EditConst							= 0x0000000000020000,
	GlobalConfig						= 0x0000000000040000,
	InstancedReference					= 0x0000000000080000,	

	DuplicateTransient					= 0x0000000000200000,	
	SubobjectReference					= 0x0000000000400000,	

	SaveGame							= 0x0000000001000000,
	NoClear								= 0x0000000002000000,

	ReferenceParm						= 0x0000000008000000,
	BlueprintAssignable					= 0x0000000010000000,
	Deprecated							= 0x0000000020000000,
	IsPlainOldData						= 0x0000000040000000,
	RepSkip								= 0x0000000080000000,
	RepNotify							= 0x0000000100000000, 
	Interp								= 0x0000000200000000,
	NonTransactional					= 0x0000000400000000,
	EditorOnly							= 0x0000000800000000,
	NoDestructor						= 0x0000001000000000,

	AutoWeak							= 0x0000004000000000,
	ContainsInstancedReference			= 0x0000008000000000,	
	AssetRegistrySearchable				= 0x0000010000000000,
	SimpleDisplay						= 0x0000020000000000,
	AdvancedDisplay						= 0x0000040000000000,
	Protected							= 0x0000080000000000,
	BlueprintCallable					= 0x0000100000000000,
	BlueprintAuthorityOnly				= 0x0000200000000000,
	TextExportTransient					= 0x0000400000000000,
	NonPIEDuplicateTransient			= 0x0000800000000000,
	ExposeOnSpawn						= 0x0001000000000000,
	PersistentInstance					= 0x0002000000000000,
	UObjectWrapper						= 0x0004000000000000, 
	HasGetValueTypeHash					= 0x0008000000000000, 
	NativeAccessSpecifierPublic			= 0x0010000000000000,	
	NativeAccessSpecifierProtected		= 0x0020000000000000,
	NativeAccessSpecifierPrivate		= 0x0040000000000000,	
	SkipSerialization					= 0x0080000000000000, 
};

class FFieldClass
{
public:
	FName                                         Name;                                              // 0x0000(0x000C)(NOT AUTO-GENERATED PROPERTY)
	uint64                                        Id;                                                // 0x0010(0x0008)(NOT AUTO-GENERATED PROPERTY)
	uint64                                        CastFlags;                                         // 0x0018(0x0008)(NOT AUTO-GENERATED PROPERTY)
	EClassFlags                                   ClassFlags;                                        // 0x0020(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_24[0x4];                                       // 0x0024(0x0004)(Fixing Size After Last Property [ Dumper-7 ])
	class FFieldClass*                            SuperClass;                                        // 0x0028(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FFieldClass) == 0x000008, "Wrong alignment on FFieldClass");
static_assert(sizeof(FFieldClass) == 0x000028, "Wrong size on FFieldClass");
static_assert(offsetof(FFieldClass, Name) == 0x000000, "Member 'FFieldClass::Name' has a wrong offset!");
static_assert(offsetof(FFieldClass, Id) == 0x000008, "Member 'FFieldClass::Id' has a wrong offset!");
static_assert(offsetof(FFieldClass, CastFlags) == 0x000010, "Member 'FFieldClass::CastFlags' has a wrong offset!");
static_assert(offsetof(FFieldClass, ClassFlags) == 0x000018, "Member 'FFieldClass::ClassFlags' has a wrong offset!");
static_assert(offsetof(FFieldClass, SuperClass) == 0x000020, "Member 'FFieldClass::SuperClass' has a wrong offset!");

class FFieldVariant
{
public:
	using ContainerType = union { class FField* Field; class UObject* Object; };

	ContainerType                                 Container;                                         // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	bool                                          bIsUObject;                                        // 0x0008(0x0001)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FFieldVariant) == 0x000008, "Wrong alignment on FFieldVariant");
static_assert(sizeof(FFieldVariant) == 0x000010, "Wrong size on FFieldVariant");
static_assert(offsetof(FFieldVariant, Container) == 0x000000, "Member 'FFieldVariant::Container' has a wrong offset!");
static_assert(offsetof(FFieldVariant, bIsUObject) == 0x000008, "Member 'FFieldVariant::bIsUObject' has a wrong offset!");

class FField
{
public:
	void*                                         VTable;                                            // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
	FFieldVariant                                 Owner;                                             // 0x0010(0x0010)(NOT AUTO-GENERATED PROPERTY)
	class FField*                                 Next;                                              // 0x0020(0x0008)(NOT AUTO-GENERATED PROPERTY)
	class FFieldClass*                            ClassPrivate;                                      // 0x0008(0x0008)(NOT AUTO-GENERATED PROPERTY)
	FName                                         NamePrivate;                                       // 0x0028(0x0008)(NOT AUTO-GENERATED PROPERTY)
	int32                                         FlagsPrivate;                                      // 0x0030(0x0004)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FField) == 0x000008, "Wrong alignment on FField");
static_assert(sizeof(FField) == 0x000038, "Wrong size on FField");
static_assert(offsetof(FField, VTable) == 0x000000, "Member 'FField::VTable' has a wrong offset!");
static_assert(offsetof(FField, Owner) == 0x000008, "Member 'FField::Owner' has a wrong offset!");
static_assert(offsetof(FField, Next) == 0x000018, "Member 'FField::Next' has a wrong offset!");
static_assert(offsetof(FField, ClassPrivate) == 0x000020, "Member 'FField::ClassPrivate' has a wrong offset!");
static_assert(offsetof(FField, NamePrivate) == 0x000028, "Member 'FField::NamePrivate' has a wrong offset!");
static_assert(offsetof(FField, FlagsPrivate) == 0x000030, "Member 'FField::FlagsPrivate' has a wrong offset!");

// Predefined struct FProperty
// 0x0048 (0x0080 - 0x0038)
class FProperty : public FField
{
public:
	int32                                         ArrayDim;                                          // 0x0038(0x0004)(NOT AUTO-GENERATED PROPERTY)
	int32                                         ElementSize;                                       // 0x003C(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint64                                        PropertyFlags;                                     // 0x0040(0x0008)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_48[0x4];                                       // 0x0048(0x0004)(Fixing Size After Last Property [ Dumper-7 ])
	int32                                         Offset_Internal;                                   // 0x004C(0x0004)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         Pad_50[0x30];                                      // 0x0050(0x0030)(Fixing Struct Size After Last Property [ Dumper-7 ])
};
static_assert(alignof(FProperty) == 0x000008, "Wrong alignment on FProperty");
static_assert(sizeof(FProperty) == 0x000080, "Wrong size on FProperty");
static_assert(offsetof(FProperty, ArrayDim) == 0x000038, "Member 'FProperty::ArrayDim' has a wrong offset!");
static_assert(offsetof(FProperty, ElementSize) == 0x00003C, "Member 'FProperty::ElementSize' has a wrong offset!");
static_assert(offsetof(FProperty, PropertyFlags) == 0x000040, "Member 'FProperty::PropertyFlags' has a wrong offset!");
static_assert(offsetof(FProperty, Offset_Internal) == 0x00004C, "Member 'FProperty::Offset_Internal' has a wrong offset!");


// Predefined struct FByteProperty
// 0x0008 (0x0088 - 0x0080)
class FByteProperty final : public FProperty
{
public:
	class UEnum*                                  Enum;                                              // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FByteProperty) == 0x000008, "Wrong alignment on FByteProperty");
static_assert(sizeof(FByteProperty) == 0x000088, "Wrong size on FByteProperty");
static_assert(offsetof(FByteProperty, Enum) == 0x000080, "Member 'FByteProperty::Enum' has a wrong offset!");

// Predefined struct FBoolProperty
// 0x0008 (0x0088 - 0x0080)
class FBoolProperty final : public FProperty
{
public:
	uint8                                         FieldSize;                                         // 0x0080(0x0001)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         ByteOffset;                                        // 0x0081(0x0001)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         ByteMask;                                          // 0x0082(0x0001)(NOT AUTO-GENERATED PROPERTY)
	uint8                                         FieldMask;                                         // 0x0083(0x0001)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FBoolProperty) == 0x000008, "Wrong alignment on FBoolProperty");
static_assert(sizeof(FBoolProperty) == 0x000088, "Wrong size on FBoolProperty");
static_assert(offsetof(FBoolProperty, FieldSize) == 0x000080, "Member 'FBoolProperty::FieldSize' has a wrong offset!");
static_assert(offsetof(FBoolProperty, ByteOffset) == 0x000081, "Member 'FBoolProperty::ByteOffset' has a wrong offset!");
static_assert(offsetof(FBoolProperty, ByteMask) == 0x000082, "Member 'FBoolProperty::ByteMask' has a wrong offset!");
static_assert(offsetof(FBoolProperty, FieldMask) == 0x000083, "Member 'FBoolProperty::FieldMask' has a wrong offset!");

// Predefined struct FObjectPropertyBase
// 0x0008 (0x0088 - 0x0080)
class FObjectPropertyBase : public FProperty
{
public:
	class UClass*                                 PropertyClass;                                     // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FObjectPropertyBase) == 0x000008, "Wrong alignment on FObjectPropertyBase");
static_assert(sizeof(FObjectPropertyBase) == 0x000088, "Wrong size on FObjectPropertyBase");
static_assert(offsetof(FObjectPropertyBase, PropertyClass) == 0x000080, "Member 'FObjectPropertyBase::PropertyClass' has a wrong offset!");

// Predefined struct FClassProperty
// 0x0008 (0x0090 - 0x0088)
class FClassProperty final : public FObjectPropertyBase
{
public:
	class UClass*                                 MetaClass;                                         // 0x0088(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FClassProperty) == 0x000008, "Wrong alignment on FClassProperty");
static_assert(sizeof(FClassProperty) == 0x000090, "Wrong size on FClassProperty");
static_assert(offsetof(FClassProperty, MetaClass) == 0x000088, "Member 'FClassProperty::MetaClass' has a wrong offset!");

// Predefined struct FStructProperty
// 0x0008 (0x0088 - 0x0080)
class FStructProperty final : public FProperty
{
public:
	class UStruct*                                Struct;                                            // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FStructProperty) == 0x000008, "Wrong alignment on FStructProperty");
static_assert(sizeof(FStructProperty) == 0x000088, "Wrong size on FStructProperty");
static_assert(offsetof(FStructProperty, Struct) == 0x000080, "Member 'FStructProperty::Struct' has a wrong offset!");

// Predefined struct FArrayProperty
// 0x0008 (0x0088 - 0x0080)
class FArrayProperty final : public FProperty
{
public:
	class FProperty*                              InnerProperty;                                     // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FArrayProperty) == 0x000008, "Wrong alignment on FArrayProperty");
static_assert(sizeof(FArrayProperty) == 0x000088, "Wrong size on FArrayProperty");
static_assert(offsetof(FArrayProperty, InnerProperty) == 0x000080, "Member 'FArrayProperty::InnerProperty' has a wrong offset!");

// Predefined struct FDelegateProperty
// 0x0008 (0x0088 - 0x0080)
class FDelegateProperty final : public FProperty
{
public:
	class UFunction*                              SignatureFunction;                                 // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FDelegateProperty) == 0x000008, "Wrong alignment on FDelegateProperty");
static_assert(sizeof(FDelegateProperty) == 0x000088, "Wrong size on FDelegateProperty");
static_assert(offsetof(FDelegateProperty, SignatureFunction) == 0x000080, "Member 'FDelegateProperty::SignatureFunction' has a wrong offset!");

// Predefined struct FMapProperty
// 0x0010 (0x0090 - 0x0080)
class FMapProperty final : public FProperty
{
public:
	class FProperty*                              KeyProperty;                                       // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
	class FProperty*                              ValueProperty;                                     // 0x0088(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FMapProperty) == 0x000008, "Wrong alignment on FMapProperty");
static_assert(sizeof(FMapProperty) == 0x000090, "Wrong size on FMapProperty");
static_assert(offsetof(FMapProperty, KeyProperty) == 0x000080, "Member 'FMapProperty::KeyProperty' has a wrong offset!");
static_assert(offsetof(FMapProperty, ValueProperty) == 0x000088, "Member 'FMapProperty::ValueProperty' has a wrong offset!");

// Predefined struct FSetProperty
// 0x0008 (0x0088 - 0x0080)
class FSetProperty final : public FProperty
{
public:
	class FProperty*                              ElementProperty;                                   // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FSetProperty) == 0x000008, "Wrong alignment on FSetProperty");
static_assert(sizeof(FSetProperty) == 0x000088, "Wrong size on FSetProperty");
static_assert(offsetof(FSetProperty, ElementProperty) == 0x000080, "Member 'FSetProperty::ElementProperty' has a wrong offset!");

// Predefined struct FEnumProperty
// 0x0010 (0x0090 - 0x0080)
class FEnumProperty final : public FProperty
{
public:
	class FProperty*                              UnderlayingProperty;                               // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
	class UEnum*                                  Enum;                                              // 0x0088(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FEnumProperty) == 0x000008, "Wrong alignment on FEnumProperty");
static_assert(sizeof(FEnumProperty) == 0x000090, "Wrong size on FEnumProperty");
static_assert(offsetof(FEnumProperty, UnderlayingProperty) == 0x000080, "Member 'FEnumProperty::UnderlayingProperty' has a wrong offset!");
static_assert(offsetof(FEnumProperty, Enum) == 0x000088, "Member 'FEnumProperty::Enum' has a wrong offset!");

// Predefined struct FFieldPathProperty
// 0x0008 (0x0088 - 0x0080)
class FFieldPathProperty final : public FProperty
{
public:
	class FFieldClass*                            FieldClass;                                        // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FFieldPathProperty) == 0x000008, "Wrong alignment on FFieldPathProperty");
static_assert(sizeof(FFieldPathProperty) == 0x000088, "Wrong size on FFieldPathProperty");
static_assert(offsetof(FFieldPathProperty, FieldClass) == 0x000080, "Member 'FFieldPathProperty::FieldClass' has a wrong offset!");

// Predefined struct FOptionalProperty
// 0x0008 (0x0088 - 0x0080)
class FOptionalProperty final : public FProperty
{
public:
	class FProperty*                              ValueProperty;                                     // 0x0080(0x0008)(NOT AUTO-GENERATED PROPERTY)
};
static_assert(alignof(FOptionalProperty) == 0x000008, "Wrong alignment on FOptionalProperty");
static_assert(sizeof(FOptionalProperty) == 0x000088, "Wrong size on FOptionalProperty");
static_assert(offsetof(FOptionalProperty, ValueProperty) == 0x000080, "Member 'FOptionalProperty::ValueProperty' has a wrong offset!");

namespace CyclicDependencyFixupImpl
{

/*
* A wrapper for a Byte-Array of padding, that allows for casting to the actual underlaiyng type. Used for undefined structs in cylic headers.
*/
template<typename UnderlayingStructType, int32 Size, int32 Align>
struct alignas(Align) TCylicStructFixup
{
private:
	uint8 Pad[Size];

public:
	      UnderlayingStructType& GetTyped()       { return reinterpret_cast<      UnderlayingStructType&>(*this); }
	const UnderlayingStructType& GetTyped() const { return reinterpret_cast<const UnderlayingStructType&>(*this); }
};

/*
* A wrapper for a Byte-Array of padding, that inherited from UObject allows for casting to the actual underlaiyng type and access to basic UObject functionality. For cyclic classes.
*/
template<typename UnderlayingClassType, int32 Size, int32 Align = 0x8, class BaseClassType = class UObject>
struct alignas(Align) TCyclicClassFixup : public BaseClassType
{
private:
	uint8 Pad[Size];

public:
	UnderlayingClassType*       GetTyped()       { return reinterpret_cast<      UnderlayingClassType*>(this); }
	const UnderlayingClassType* GetTyped() const { return reinterpret_cast<const UnderlayingClassType*>(this); }
};

}

}
)UECoreBasicH";

static const char* kUECoreBasicCpp = R"UECoreBasicCpp(
#pragma once

// Basic file containing function-implementations from Basic.hpp
#include "Basic.h"

#include "CoreUObject_classes.h"

namespace SDK
{

class UClass* BasicFilesImpleUtils::FindClassByName(const std::string& Name)
{
	return UObject::FindClassFast(Name);
}

class UClass* BasicFilesImpleUtils::FindClassByFullName(const std::string& Name)
{
	return UObject::FindClass(Name);
}

std::string BasicFilesImpleUtils::GetObjectName(class UClass* Class)
{
	return Class->GetName();
}

int32 BasicFilesImpleUtils::GetObjectIndex(class UClass* Class)
{
	return Class->InternalIndex;
}

uint64 BasicFilesImpleUtils::GetObjFNameAsUInt64(class UClass* Class)
{
	return *reinterpret_cast<uint64*>(&Class->NamePrivate);
}

class UObject* BasicFilesImpleUtils::GetObjectByIndex(int32 Index)
{
	return UObject::GObjects->GetByIndex(Index);
}

UFunction* BasicFilesImpleUtils::FindFunctionByFName(const FName* Name)
{
	for (int i = 0; i < UObject::GObjects->Num(); ++i)
	{
		UObject* Object = UObject::GObjects->GetByIndex(i);

		if (!Object)
			continue;

		if (Object->NamePrivate == *Name)
			return static_cast<UFunction*>(Object);
	}

	return nullptr;
}


// Predefined Function

class UObject* FWeakObjectPtr::Get() const
{
	return UObject::GObjects->GetByIndex(ObjectIndex);
}


// Predefined Function

class UObject* FWeakObjectPtr::operator->() const
{
	return UObject::GObjects->GetByIndex(ObjectIndex);
}


// Predefined Function

bool FWeakObjectPtr::operator==(const FWeakObjectPtr& Other) const
{
	return ObjectIndex == Other.ObjectIndex;
}


// Predefined Function

bool FWeakObjectPtr::operator!=(const FWeakObjectPtr& Other) const
{
	return ObjectIndex != Other.ObjectIndex;
}


// Predefined Function

bool FWeakObjectPtr::operator==(const class UObject* Other) const
{
	return ObjectIndex == Other->InternalIndex;
}


// Predefined Function

bool FWeakObjectPtr::operator!=(const class UObject* Other) const
{
	return ObjectIndex != Other->InternalIndex;
}

}

)UECoreBasicCpp";

static const char* kUECoreUnrealContainersH = R"UECoreUC(
#pragma once

#include <string>
#include <stdexcept>
#include "utfcpp/unchecked.h"

namespace UC
{	
	typedef int8_t  int8;
	typedef int16_t int16;
	typedef int32_t int32;
	typedef int64_t int64;

	typedef uint8_t  uint8;
	typedef uint16_t uint16;
	typedef uint32_t uint32;
	typedef uint64_t uint64;

	template<typename ArrayElementType>
	class TArray;

	template<typename SparseArrayElementType>
	class TSparseArray;

	template<typename SetElementType>
	class TSet;

	template<typename KeyElementType, typename ValueElementType>
	class TMap;

	template<typename KeyElementType, typename ValueElementType>
	class TPair;

	namespace Iterators
	{
		class FSetBitIterator;

		template<typename ArrayType>
		class TArrayIterator;

		template<class ContainerType>
		class TContainerIterator;

		template<typename SparseArrayElementType>
		using TSparseArrayIterator = TContainerIterator<TSparseArray<SparseArrayElementType>>;

		template<typename SetElementType>
		using TSetIterator = TContainerIterator<TSet<SetElementType>>;

		template<typename KeyElementType, typename ValueElementType>
		using TMapIterator = TContainerIterator<TMap<KeyElementType, ValueElementType>>;
	}


	namespace ContainerImpl
	{
		namespace HelperFunctions
		{
			inline uint32 FloorLog2(uint32 Value)
			{
				uint32 pos = 0;
				if (Value >= 1 << 16) { Value >>= 16; pos += 16; }
				if (Value >= 1 << 8) { Value >>= 8; pos += 8; }
				if (Value >= 1 << 4) { Value >>= 4; pos += 4; }
				if (Value >= 1 << 2) { Value >>= 2; pos += 2; }
				if (Value >= 1 << 1) { pos += 1; }
				return pos;
			}

			inline uint32 CountLeadingZeros(uint32 Value)
			{
				if (Value == 0)
					return 32;

				return 31 - FloorLog2(Value);
			}
		}

		template<int32 Size, uint32 Alignment>
		struct TAlignedBytes
		{
			alignas(Alignment) uint8 Pad[Size];
		};

		template<uint32 NumInlineElements>
		class TInlineAllocator
		{
		public:
			template<typename ElementType>
			class ForElementType
			{
			private:
				static constexpr int32 ElementSize = sizeof(ElementType);
				static constexpr int32 ElementAlign = alignof(ElementType);

				static constexpr int32 InlineDataSizeBytes = NumInlineElements * ElementSize;

			private:
				TAlignedBytes<ElementSize, ElementAlign> InlineData[NumInlineElements];
				ElementType* SecondaryData;

			public:
				ForElementType()
					: InlineData{ 0x0 }, SecondaryData(nullptr)
				{
				}

				ForElementType(ForElementType&&) = default;
				ForElementType(const ForElementType&) = default;

			public:
				ForElementType& operator=(ForElementType&&) = default;
				ForElementType& operator=(const ForElementType&) = default;

			public:
				inline const ElementType* GetAllocation() const { return SecondaryData ? SecondaryData : reinterpret_cast<const ElementType*>(&InlineData); }

				inline uint32 GetNumInlineBytes() const { return NumInlineElements; }
			};
		};

		class FBitArray
		{
		protected:
			static constexpr int32 NumBitsPerDWORD = 32;
			static constexpr int32 NumBitsPerDWORDLogTwo = 5;

		private:
			TInlineAllocator<4>::ForElementType<int32> Data;
			int32 NumBits;
			int32 MaxBits;

		public:
			FBitArray()
				: NumBits(0), MaxBits(Data.GetNumInlineBytes() * NumBitsPerDWORD)
			{
			}

			FBitArray(const FBitArray&) = default;

			FBitArray(FBitArray&&) = default;

		public:
			FBitArray& operator=(FBitArray&&) = default;

			FBitArray& operator=(const FBitArray& Other) = default;

		private:
			inline void VerifyIndex(int32 Index) const { if (!IsValidIndex(Index)) throw std::out_of_range("Index was out of range!"); }

		public:
			inline int32 Num() const { return NumBits; }
			inline int32 Max() const { return MaxBits; }

			inline const uint32* GetData() const { return reinterpret_cast<const uint32*>(Data.GetAllocation()); }

			inline bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < NumBits; }

			inline bool IsValid() const { return GetData() && NumBits > 0; }

		public:
			inline bool operator[](int32 Index) const { VerifyIndex(Index); return GetData()[Index / NumBitsPerDWORD] & (1 << (Index & (NumBitsPerDWORD - 1))); }

			inline bool operator==(const FBitArray& Other) const { return NumBits == Other.NumBits && GetData() == Other.GetData(); }
			inline bool operator!=(const FBitArray& Other) const { return NumBits != Other.NumBits || GetData() != Other.GetData(); }

		public:
			friend Iterators::FSetBitIterator begin(const FBitArray& Array);
			friend Iterators::FSetBitIterator end  (const FBitArray& Array);
		};

		template<typename SparseArrayType>
		union TSparseArrayElementOrFreeListLink
		{
			SparseArrayType ElementData;

			struct
			{
				int32 PrevFreeIndex;
				int32 NextFreeIndex;
			};
		};

		template<typename SetType>
		class SetElement
		{
		private:
			template<typename SetDataType>
			friend class UC::TSet;

		private:
			SetType Value;
			int32 HashNextId;
			int32 HashIndex;
		};
	}


	template <typename KeyType, typename ValueType>
	class TPair
	{
	public:
		KeyType First;
		ValueType Second;

	public:
		TPair(KeyType Key, ValueType Value)
			: First(Key), Second(Value)
		{
		}

	public:
		inline       KeyType& Key()       { return First; }
		inline const KeyType& Key() const { return First; }

		inline       ValueType& Value()       { return Second; }
		inline const ValueType& Value() const { return Second; }
	};

	template<typename ArrayElementType>
	class TArray
	{
	private:
#if defined(__GNUC__) || defined(__clang__)
		template<typename IArrayElementType>
#else
		template<typename ArrayElementType>
#endif
		friend class TAllocatedArray;

		template<typename SparseArrayElementType>
		friend class TSparseArray;

	protected:
		static constexpr uint64 ElementAlign = alignof(ArrayElementType);
		static constexpr uint64 ElementSize = sizeof(ArrayElementType);

	public:
		ArrayElementType* Data;
		int32 NumElements;
		int32 MaxElements;

	public:
		TArray()
			: Data(nullptr), NumElements(0), MaxElements(0)
		{
		}

		TArray(const TArray&) = default;

		TArray(TArray&&) = default;

	public:
		TArray& operator=(TArray&&) = default;
		TArray& operator=(const TArray&) = default;

	private:
		inline int32 GetSlack() const { return MaxElements - NumElements; }

		inline void VerifyIndex(int32 Index) const { if (!IsValidIndex(Index)) throw std::out_of_range("Index was out of range!"); }

		inline       ArrayElementType& GetUnsafe(int32 Index)       { return Data[Index]; }
		inline const ArrayElementType& GetUnsafe(int32 Index) const { return Data[Index]; }

	public:
		/* Adds to the array if there is still space for one more element */
		inline bool Add(const ArrayElementType& Element)
		{
			if (GetSlack() <= 0)
				return false;

			Data[NumElements] = Element;
			NumElements++;

			return true;
		}

		inline bool Remove(int32 Index)
		{
			if (!IsValidIndex(Index))
				return false;

			NumElements--;

			for (int i = Index; i < NumElements; i++)
			{
				/* NumElements was decremented, acessing i + 1 is safe */
				Data[i] = Data[i + 1];
			}

			return true;
		}

		inline void Clear()
		{
			NumElements = 0;

			if (Data)
				memset(Data, 0, NumElements * ElementSize);
		}

	public:
		inline int32 Num() const { return NumElements; }
		inline int32 Max() const { return MaxElements; }

		inline const ArrayElementType* GetDataPtr() const { return Data; }

		inline bool IsValidIndex(int32 Index) const { return Data && Index >= 0 && Index < NumElements; }

		inline bool IsValid() const { return Data && NumElements > 0 && MaxElements >= NumElements; }

	public:
		inline       ArrayElementType& operator[](int32 Index)       { VerifyIndex(Index); return Data[Index]; }
		inline const ArrayElementType& operator[](int32 Index) const { VerifyIndex(Index); return Data[Index]; }

		inline bool operator==(const TArray<ArrayElementType>& Other) const { return Data == Other.Data; }
		inline bool operator!=(const TArray<ArrayElementType>& Other) const { return Data != Other.Data; }

		inline explicit operator bool() const { return IsValid(); };

	public:
		template<typename T> friend Iterators::TArrayIterator<T> begin(const TArray& Array);
		template<typename T> friend Iterators::TArrayIterator<T> end  (const TArray& Array);
	};

	class FString : public TArray<char16_t>
	{
	public:
		FString() = default;

		inline std::string ToString() const {
			if (!IsValid()) return "";
			std::string result;
			utf8::unchecked::utf16to8(Data, Data + NumElements - 1, std::back_inserter(result));
			return result;
		}
	};
	/*
	* Class to allow construction of a TArray, that uses c-style standard-library memory allocation.
	* 
	* Useful for calling functions that expect a buffer of a certain size and do not reallocate that buffer.
	* This avoids leaking memory, if the array would otherwise be allocated by the engine, and couldn't be freed without FMemory-functions.
	*/
	template<typename ArrayElementType>
	class TAllocatedArray : public TArray<ArrayElementType>
	{
	public:
		TAllocatedArray() = delete;

	public:
		TAllocatedArray(int32 Size)
		{
			this->Data = static_cast<ArrayElementType*>(malloc(Size * sizeof(ArrayElementType)));
			this->NumElements = 0x0;
			this->MaxElements = Size;
		}

		~TAllocatedArray()
		{
			if (this->Data)
				free(this->Data);

			this->NumElements = 0x0;
			this->MaxElements = 0x0;
		}

	public:
		inline operator       TArray<ArrayElementType>()       { return *reinterpret_cast<      TArray<ArrayElementType>*>(this); }
		inline operator const TArray<ArrayElementType>() const { return *reinterpret_cast<const TArray<ArrayElementType>*>(this); }
	};

	/*
	* Class to allow construction of an FString, that uses c-style standard-library memory allocation.
	*
	* Useful for calling functions that expect a buffer of a certain size and do not reallocate that buffer.
	* This avoids leaking memory, if the array would otherwise be allocated by the engine, and couldn't be freed without FMemory-functions.
	*/
	class FAllocatedString : public FString
	{
	public:
		FAllocatedString() = delete;

	public:
		FAllocatedString(int32 Size)
		{
			Data = static_cast<char16_t*>(malloc(Size * sizeof(char16_t)));
			NumElements = 0x0;
			MaxElements = Size;
		}

		FAllocatedString(const char* str)
		{
			const size_t Utf8Len = (str && *str) ? std::strlen(str) : 0;
			if (!Utf8Len)
			{
				Data = nullptr;
				NumElements = MaxElements = 0;
				return;
			}
			Data = static_cast<char16_t*>(malloc((Utf8Len + 1) * sizeof(char16_t)));
			char16_t* End = utf8::unchecked::utf8to16(str, str + Utf8Len, Data);
			*End = u'\0';
			NumElements = MaxElements = int32(End - Data) + 1;
		}

		~FAllocatedString()
		{
			if (Data)
				free(Data);

			NumElements = 0x0;
			MaxElements = 0x0;
		}

	public:
		inline operator       FString()       { return *reinterpret_cast<      FString*>(this); }
		inline operator const FString() const { return *reinterpret_cast<const FString*>(this); }
	};
	template<typename SparseArrayElementType>
	class TSparseArray
	{
	private:
		static constexpr uint32 ElementAlign = alignof(SparseArrayElementType);
		static constexpr uint32 ElementSize = sizeof(SparseArrayElementType);

	private:
		using FElementOrFreeListLink = ContainerImpl::TSparseArrayElementOrFreeListLink<ContainerImpl::TAlignedBytes<ElementSize, ElementAlign>>;

	private:
		TArray<FElementOrFreeListLink> Data;
		ContainerImpl::FBitArray AllocationFlags;
		int32 FirstFreeIndex;
		int32 NumFreeIndices;

	public:
		TSparseArray()
			: FirstFreeIndex(-1), NumFreeIndices(0)
		{
		}

		TSparseArray(TSparseArray&&) = default;
		TSparseArray(const TSparseArray&) = default;

	public:
		TSparseArray& operator=(TSparseArray&&) = default;
		TSparseArray& operator=(const TSparseArray&) = default;

	private:
		inline void VerifyIndex(int32 Index) const { if (!IsValidIndex(Index)) throw std::out_of_range("Index was out of range!"); }

	public:
		inline int32 NumAllocated() const { return Data.Num(); }

		inline int32 Num() const { return NumAllocated() - NumFreeIndices; }
		inline int32 Max() const { return Data.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Data.IsValidIndex(Index) && AllocationFlags[Index]; }

		inline bool IsValid() const { return Data.IsValid() && AllocationFlags.IsValid(); }

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return AllocationFlags; }

	public:
		inline       SparseArrayElementType& operator[](int32 Index)       { VerifyIndex(Index); return *reinterpret_cast<SparseArrayElementType*>(&Data.GetUnsafe(Index).ElementData); }
		inline const SparseArrayElementType& operator[](int32 Index) const { VerifyIndex(Index); return *reinterpret_cast<SparseArrayElementType*>(&Data.GetUnsafe(Index).ElementData); }

		inline bool operator==(const TSparseArray<SparseArrayElementType>& Other) const { return Data == Other.Data; }
		inline bool operator!=(const TSparseArray<SparseArrayElementType>& Other) const { return Data != Other.Data; }

	public:
		template<typename T> friend Iterators::TSparseArrayIterator<T> begin(const TSparseArray& Array);
		template<typename T> friend Iterators::TSparseArrayIterator<T> end  (const TSparseArray& Array);
	};

	template<typename SetElementType>
	class TSet
	{
	private:
		static constexpr uint32 ElementAlign = alignof(SetElementType);
		static constexpr uint32 ElementSize = sizeof(SetElementType);

	private:
		using SetDataType = ContainerImpl::SetElement<SetElementType>;
		using HashType = ContainerImpl::TInlineAllocator<1>::ForElementType<int32>;

	private:
		TSparseArray<SetDataType> Elements;
		HashType Hash;
		int32 HashSize;

	public:
		TSet()
			: HashSize(0)
		{
		}

		TSet(TSet&&) = default;
		TSet(const TSet&) = default;

	public:
		TSet& operator=(TSet&&) = default;
		TSet& operator=(const TSet&) = default;

	private:
		inline void VerifyIndex(int32 Index) const { if (!IsValidIndex(Index)) throw std::out_of_range("Index was out of range!"); }

	public:
		inline int32 NumAllocated() const { return Elements.NumAllocated(); }

		inline int32 Num() const { return Elements.Num(); }
		inline int32 Max() const { return Elements.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Elements.IsValidIndex(Index); }

		inline bool IsValid() const { return Elements.IsValid(); }

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return Elements.GetAllocationFlags(); }

	public:
		inline       SetElementType& operator[] (int32 Index)       { return Elements[Index].Value; }
		inline const SetElementType& operator[] (int32 Index) const { return Elements[Index].Value; }

		inline bool operator==(const TSet<SetElementType>& Other) const { return Elements == Other.Elements; }
		inline bool operator!=(const TSet<SetElementType>& Other) const { return Elements != Other.Elements; }

	public:
		template<typename T> friend Iterators::TSetIterator<T> begin(const TSet& Set);
		template<typename T> friend Iterators::TSetIterator<T> end  (const TSet& Set);
	};

	template<typename KeyElementType, typename ValueElementType>
	class TMap
	{
	public:
		using ElementType = TPair<KeyElementType, ValueElementType>;

	private:
		TSet<ElementType> Elements;

	private:
		inline void VerifyIndex(int32 Index) const { if (!IsValidIndex(Index)) throw std::out_of_range("Index was out of range!"); }

	public:
		inline int32 NumAllocated() const { return Elements.NumAllocated(); }

		inline int32 Num() const { return Elements.Num(); }
		inline int32 Max() const { return Elements.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Elements.IsValidIndex(Index); }

		inline bool IsValid() const { return Elements.IsValid(); }

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return Elements.GetAllocationFlags(); }

	public:
		inline decltype(auto) Find(const KeyElementType& Key, bool(*Equals)(const KeyElementType& LeftKey, const KeyElementType& RightKey))
		{
			for (auto It = begin(*this); It != end(*this); ++It)
			{
				if (Equals(It->Key(), Key))
					return It;
			}
		
			return end(*this);
		}

	public:
		inline       ElementType& operator[] (int32 Index)       { return Elements[Index]; }
		inline const ElementType& operator[] (int32 Index) const { return Elements[Index]; }

		inline bool operator==(const TMap<KeyElementType, ValueElementType>& Other) const { return Elements == Other.Elements; }
		inline bool operator!=(const TMap<KeyElementType, ValueElementType>& Other) const { return Elements != Other.Elements; }

	public:
		template<typename KeyType, typename ValueType> friend Iterators::TMapIterator<KeyType, ValueType> begin(const TMap& Map);
		template<typename KeyType, typename ValueType> friend Iterators::TMapIterator<KeyType, ValueType> end  (const TMap& Map);
	};

	namespace Iterators
	{
		class FRelativeBitReference
		{
		protected:
			static constexpr int32 NumBitsPerDWORD = 32;
			static constexpr int32 NumBitsPerDWORDLogTwo = 5;

		public:
			inline explicit FRelativeBitReference(int32 BitIndex)
				: WordIndex(BitIndex >> NumBitsPerDWORDLogTwo)
				, Mask(1 << (BitIndex & (NumBitsPerDWORD - 1)))
			{
			}

			int32  WordIndex;
			uint32 Mask;
		};

		class FSetBitIterator : public FRelativeBitReference
		{
		private:
			const ContainerImpl::FBitArray& Array;

			uint32 UnvisitedBitMask;
			int32 CurrentBitIndex;
			int32 BaseBitIndex;

		public:
			explicit FSetBitIterator(const ContainerImpl::FBitArray& InArray, int32 StartIndex = 0)
				: FRelativeBitReference(StartIndex)
				, Array(InArray)
				, UnvisitedBitMask((~0U) << (StartIndex & (NumBitsPerDWORD - 1)))
				, CurrentBitIndex(StartIndex)
				, BaseBitIndex(StartIndex & ~(NumBitsPerDWORD - 1))
			{
				if (StartIndex != Array.Num())
					FindFirstSetBit();
			}

		public:
			inline FSetBitIterator& operator++()
			{
				UnvisitedBitMask &= ~this->Mask;

				FindFirstSetBit();

				return *this;
			}

			inline explicit operator bool() const { return CurrentBitIndex < Array.Num(); }

			inline bool operator==(const FSetBitIterator& Rhs) const { return CurrentBitIndex == Rhs.CurrentBitIndex && &Array == &Rhs.Array; }
			inline bool operator!=(const FSetBitIterator& Rhs) const { return CurrentBitIndex != Rhs.CurrentBitIndex || &Array != &Rhs.Array; }

		public:
			inline int32 GetIndex() { return CurrentBitIndex; }

			void FindFirstSetBit()
			{
				const uint32* ArrayData = Array.GetData();
				const int32   ArrayNum = Array.Num();
				const int32   LastWordIndex = (ArrayNum - 1) / NumBitsPerDWORD;

				uint32 RemainingBitMask = ArrayData[this->WordIndex] & UnvisitedBitMask;
				while (!RemainingBitMask)
				{
					++this->WordIndex;
					BaseBitIndex += NumBitsPerDWORD;
					if (this->WordIndex > LastWordIndex)
					{
						CurrentBitIndex = ArrayNum;
						return;
					}

					RemainingBitMask = ArrayData[this->WordIndex];
					UnvisitedBitMask = ~0;
				}

				const uint32 NewRemainingBitMask = RemainingBitMask & (RemainingBitMask - 1);

				this->Mask = NewRemainingBitMask ^ RemainingBitMask;

				CurrentBitIndex = BaseBitIndex + NumBitsPerDWORD - 1 - ContainerImpl::HelperFunctions::CountLeadingZeros(this->Mask);

				if (CurrentBitIndex > ArrayNum)
					CurrentBitIndex = ArrayNum;
			}
		};

		template<typename ArrayType>
		class TArrayIterator
		{
		private:
			TArray<ArrayType>& IteratedArray;
			int32 Index;

		public:
			TArrayIterator(const TArray<ArrayType>& Array, int32 StartIndex = 0x0)
				: IteratedArray(const_cast<TArray<ArrayType>&>(Array)), Index(StartIndex)
			{
			}

		public:
			inline int32 GetIndex() { return Index; }

			inline int32 IsValid() { return IteratedArray.IsValidIndex(GetIndex()); }

		public:
			inline TArrayIterator& operator++() { ++Index; return *this; }
			inline TArrayIterator& operator--() { --Index; return *this; }

			inline       ArrayType& operator*()       { return IteratedArray[GetIndex()]; }
			inline const ArrayType& operator*() const { return IteratedArray[GetIndex()]; }

			inline       ArrayType* operator->()       { return &IteratedArray[GetIndex()]; }
			inline const ArrayType* operator->() const { return &IteratedArray[GetIndex()]; }

			inline bool operator==(const TArrayIterator& Other) const { return &IteratedArray == &Other.IteratedArray && Index == Other.Index; }
			inline bool operator!=(const TArrayIterator& Other) const { return &IteratedArray != &Other.IteratedArray || Index != Other.Index; }
		};

		template<class ContainerType>
		class TContainerIterator
		{
		private:
			ContainerType& IteratedContainer;
			FSetBitIterator BitIterator;

		public:
			TContainerIterator(const ContainerType& Container, const ContainerImpl::FBitArray& BitArray, int32 StartIndex = 0x0)
				: IteratedContainer(const_cast<ContainerType&>(Container)), BitIterator(BitArray, StartIndex)
			{
			}

		public:
			inline int32 GetIndex() { return BitIterator.GetIndex(); }

			inline int32 IsValid() { return IteratedContainer.IsValidIndex(GetIndex()); }

		public:
			inline TContainerIterator& operator++() { ++BitIterator; return *this; }
#if defined(__GNUC__) || defined(__clang__)
#else
			inline TContainerIterator& operator--() { --BitIterator; return *this; }
#endif

			inline       auto& operator*()       { return IteratedContainer[GetIndex()]; }
			inline const auto& operator*() const { return IteratedContainer[GetIndex()]; }

			inline       auto* operator->()       { return &IteratedContainer[GetIndex()]; }
			inline const auto* operator->() const { return &IteratedContainer[GetIndex()]; }

			inline bool operator==(const TContainerIterator& Other) const { return &IteratedContainer == &Other.IteratedContainer && BitIterator == Other.BitIterator; }
			inline bool operator!=(const TContainerIterator& Other) const { return &IteratedContainer != &Other.IteratedContainer || BitIterator != Other.BitIterator; }
		};
	}

	inline Iterators::FSetBitIterator begin(const ContainerImpl::FBitArray& Array) { return Iterators::FSetBitIterator(Array, 0); }
	inline Iterators::FSetBitIterator end  (const ContainerImpl::FBitArray& Array) { return Iterators::FSetBitIterator(Array, Array.Num()); }

	template<typename T> inline Iterators::TArrayIterator<T> begin(const TArray<T>& Array) { return Iterators::TArrayIterator<T>(Array, 0); }
	template<typename T> inline Iterators::TArrayIterator<T> end  (const TArray<T>& Array) { return Iterators::TArrayIterator<T>(Array, Array.Num()); }

	template<typename T> inline Iterators::TSparseArrayIterator<T> begin(const TSparseArray<T>& Array) { return Iterators::TSparseArrayIterator<T>(Array, Array.GetAllocationFlags(), 0); }
	template<typename T> inline Iterators::TSparseArrayIterator<T> end  (const TSparseArray<T>& Array) { return Iterators::TSparseArrayIterator<T>(Array, Array.GetAllocationFlags(), Array.NumAllocated()); }

	template<typename T> inline Iterators::TSetIterator<T> begin(const TSet<T>& Set) { return Iterators::TSetIterator<T>(Set, Set.GetAllocationFlags(), 0); }
	template<typename T> inline Iterators::TSetIterator<T> end  (const TSet<T>& Set) { return Iterators::TSetIterator<T>(Set, Set.GetAllocationFlags(), Set.NumAllocated()); }

	template<typename T0, typename T1> inline Iterators::TMapIterator<T0, T1> begin(const TMap<T0, T1>& Map) { return Iterators::TMapIterator<T0, T1>(Map, Map.GetAllocationFlags(), 0); }
	template<typename T0, typename T1> inline Iterators::TMapIterator<T0, T1> end  (const TMap<T0, T1>& Map) { return Iterators::TMapIterator<T0, T1>(Map, Map.GetAllocationFlags(), Map.NumAllocated()); }

	static_assert(sizeof(TArray<int32>) == 0x10, "TArray has a wrong size!");
	static_assert(sizeof(TSet<int32>) == 0x50, "TSet has a wrong size!");
	static_assert(sizeof(TMap<int32, int32>) == 0x50, "TMap has a wrong size!");
}
)UECoreUC";
