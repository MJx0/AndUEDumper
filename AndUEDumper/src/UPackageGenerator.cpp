#include "UPackageGenerator.hpp"

#include <cctype>
#include <unordered_set>

#include "UE/UEMemory.hpp"
using namespace UEMemory;

void UE_UPackage::GenerateBitPadding(std::vector<Member> &members, uint32_t offset, uint8_t bitOffset, uint8_t size)
{
    Member padding;
    padding.Type = "uint8_t";
    padding.Name = fmt::format("BitPad_0x{:X}_{} : {}", offset, bitOffset, size);
    padding.Offset = offset;
    padding.Size = 1;
    members.push_back(padding);
}

void UE_UPackage::GeneratePadding(std::vector<Member> &members, uint32_t offset, uint32_t size)
{
    Member padding;
    padding.Type = "uint8_t";
    padding.Name = fmt::format("Pad_0x{:X}[0x{:X}]", offset, size);
    padding.Offset = offset;
    padding.Size = size;
    members.push_back(padding);
}

void UE_UPackage::FillPadding(const UE_UStruct &object, std::vector<Member> &members, uint32_t &offset, uint8_t &bitOffset, uint32_t end)
{
    (void)object;

    if (bitOffset && bitOffset < 8)
    {
        UE_UPackage::GenerateBitPadding(members, offset, bitOffset, 8 - bitOffset);
        bitOffset = 0;
        offset++;
    }

    if (offset != end)
    {
        GeneratePadding(members, offset, end - offset);
        offset = end;
    }
}

// Replace characters that aren't valid in C++ identifiers with '_'. Real
// dumps contain UE class names like `UFoo~Bar` and `UBaz-Qux` (auto-named
// blueprint subclasses). Structural punctuation that legitimately appears
// in our type strings — '<>', ',', '*', '&', ':', '[]', whitespace — is
// preserved so templates and bit-field markers stay parseable.
static std::string SanitizeForCpp(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        const bool keep = std::isalnum(uc) || c == '_'
                       || c == ' ' || c == '\t'
                       || c == '<' || c == '>' || c == ','
                       || c == '*' || c == '&'
                       || c == ':'
                       || c == '[' || c == ']';
        out += keep ? c : '_';
    }
    return out;
}

// Stricter sanitizer for pure-identifier positions: struct / enum / member /
// function names that come from an FName and may contain spaces or any
// other non-identifier byte. Anything outside [A-Za-z0-9_] becomes '_'.
static std::string SanitizeIdentifier(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        out += (std::isalnum(uc) || c == '_') ? c : '_';
    }
    return out;
}

// SanitizeIdentForCpp — used for positions that become C++ identifiers in
// a body/declaration where they must NOT collide with C++ keywords or with
// the local variables we generate inside ProcessEvent dispatch bodies
// (`Parms`, `Func`). Characters are filtered as in SanitizeIdentifier; if
// the result is a keyword/reserved name, an underscore is appended.
static std::string SanitizeIdentForCpp(const std::string &in)
{
    std::string out = SanitizeIdentifier(in);
    if (out.empty())
        return out;

    static const std::unordered_set<std::string> kReserved = {
        // C++ keywords (covers C++20)
        "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool",
        "break","case","catch","char","char8_t","char16_t","char32_t","class",
        "compl","concept","const","consteval","constexpr","constinit","const_cast",
        "continue","co_await","co_return","co_yield","decltype","default","delete",
        "do","double","dynamic_cast","else","enum","explicit","export","extern",
        "false","float","for","friend","goto","if","inline","int","long","mutable",
        "namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq",
        "private","protected","public","register","reinterpret_cast","requires",
        "return","short","signed","sizeof","static","static_assert","static_cast",
        "struct","switch","template","this","thread_local","throw","true","try",
        "typedef","typeid","typename","union","unsigned","using","virtual","void",
        "volatile","wchar_t","while","xor","xor_eq",
        // Locals our emitted ProcessEvent bodies use — keep param names from
        // shadowing them. UE rarely generates these but BP-named params can.
        "Parms","Func",
    };
    if (kReserved.count(out))
        out.push_back('_');
    return out;
}

// Identifiers that come from primitive C++ types or the AIOHeader preamble
// (predefined containers, smart pointers, basic UE types). Members of this
// set never become package-level dependencies.
static const std::unordered_set<std::string> &kBuiltinIdents()
{
    static const std::unordered_set<std::string> s = {
        // C++ primitives / qualifiers
        "void", "bool", "char", "short", "int", "long", "float", "double",
        "signed", "unsigned",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "size_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        // C++ keywords that may appear via "struct"/"enum class"/"const" prefixes
        "struct", "enum", "class", "const", "volatile",
        // Basic UE types provided by the AIOHeader preamble
        "FName", "FString", "FText",
        "FScriptDelegate", "FDelegate", "FMulticastDelegate",
        "FMulticastInlineDelegate", "FMulticastSparseDelegate",
        "FWeakObjectPtr", "FUniqueObjectGuid", "FFieldClass", "FProperty",
        "FScriptInterface", "FFieldPath",
        // Predefined container / wrapper templates
        "TArray", "TMap", "TSet", "TPair",
        "TWeakObjectPtr", "TLazyObjectPtr",
        "TSoftObjectPtr", "TSoftClassPtr",
        "TSubclassOf", "TScriptInterface", "TFieldPath",
        // Sentinel emitted by the dumper for malformed/unknown children —
        // forward-declared in the AIOHeader preamble so containers compile.
        "None", "FNone",
    };
    return s;
}

// Two flavors of template wrappers based on how they store their parameter T:
//  - PtrHandle:    stores T* (or an opaque handle); fwd decl of T suffices.
//  - ValueHolding: stores T by value (TPair has `T First; U Second;`; TSet
//                  embeds TSparseArray<TPair<...>>; TMap embeds a TSet of
//                  TPair). Instantiating these requires T to be complete.
enum class WrapperKind { PtrHandle, ValueHolding };

static bool ClassifyWrapper(const std::string &ident, WrapperKind *out)
{
    static const std::unordered_set<std::string> kPtr = {
        "TArray",
        "TWeakObjectPtr", "TLazyObjectPtr",
        "TSoftObjectPtr", "TSoftClassPtr",
        "TSubclassOf", "TScriptInterface", "TFieldPath",
    };
    static const std::unordered_set<std::string> kValue = {
        "TPair", "TSet", "TMap",
    };
    if (kPtr.count(ident))   { *out = WrapperKind::PtrHandle;    return true; }
    if (kValue.count(ident)) { *out = WrapperKind::ValueHolding; return true; }
    return false;
}

void UE_UPackage::ExtractTypeDeps(const std::string &typeStr,
                                  std::set<std::string> &fullDeps,
                                  std::set<std::string> &fwdDeps)
{
    const auto &builtins = kBuiltinIdents();

    // Each open '<' pushes a wrapper kind. An ident inside the stack must be
    // a fullDep if ANY enclosing wrapper is value-holding, because that
    // wrapper instantiation can't be sized without T being complete. A
    // pointer argument (`T*`) is always fine as fwdDep regardless of wrapper.
    // Unknown templates fall back to ValueHolding — over-including is
    // harmless, missing an include breaks the build.
    std::vector<WrapperKind> wstack;
    bool                     pendingHasKind = false;
    WrapperKind              pendingKind    = WrapperKind::ValueHolding;

    size_t i = 0;
    const size_t n = typeStr.size();

    while (i < n)
    {
        char c = typeStr[i];

        if (c == '<')
        {
            wstack.push_back(pendingHasKind ? pendingKind : WrapperKind::ValueHolding);
            pendingHasKind = false;
            i++; continue;
        }
        if (c == '>')
        {
            if (!wstack.empty()) wstack.pop_back();
            pendingHasKind = false;
            i++; continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(typeStr[i])) || typeStr[i] == '_'))
                i++;
            std::string ident = typeStr.substr(start, i - start);

            // Consume trailing whitespace as part of the ident so the
            // wrapper-then-'<' link survives `TMap <K, V>`-style spacing.
            while (i < n && std::isspace(static_cast<unsigned char>(typeStr[i])))
                i++;

            const bool isPointer    = (i < n && typeStr[i] == '*');
            const bool followedByLT = (i < n && typeStr[i] == '<');

            if (builtins.count(ident))
            {
                WrapperKind k;
                if (followedByLT && ClassifyWrapper(ident, &k))
                {
                    pendingKind    = k;
                    pendingHasKind = true;
                }
                else
                {
                    pendingHasKind = false;
                }
                continue;
            }

            if (wstack.empty())
            {
                if (isPointer) fwdDeps.insert(std::move(ident));
                else           fullDeps.insert(std::move(ident));
            }
            else if (isPointer)
            {
                fwdDeps.insert(std::move(ident));
            }
            else
            {
                bool anyValue = false;
                for (WrapperKind w : wstack)
                {
                    if (w == WrapperKind::ValueHolding) { anyValue = true; break; }
                }
                if (anyValue) fullDeps.insert(std::move(ident));
                else          fwdDeps.insert(std::move(ident));
            }
            pendingHasKind = false;
        }
        else
        {
            pendingHasKind = false;
            i++;
        }
    }
}

void UE_UPackage::GenerateFunction(const UE_UFunction &fn, Function *out)
{
    out->Name = fn.GetName();
    out->FullName = fn.GetFullName();
    out->EFlags = fn.GetFunctionEFlags();
    out->Flags = fn.GetFunctionFlags();
    out->NumParams = fn.GetNumParams();
    out->ParamSize = fn.GetParamSize();
    out->Func = fn.GetFunc();
    out->IsStatic = (out->EFlags & FUNC_Static) != 0;
    out->ReturnType = "void"; // overwritten if a CPF_ReturnParm is found

    const std::string sanitizedFuncName = SanitizeIdentForCpp(fn.GetName());

    auto generateParam = [&](IProperty *prop)
    {
        auto flags = prop->GetPropertyFlags();

        // if property has 'ReturnParm' flag
        if (flags & CPF_ReturnParm)
        {
            out->ReturnType = SanitizeForCpp(prop->GetType().second);
        }
        // if property has 'Parm' flag
        else if (flags & CPF_Parm)
        {
            const std::string typeStr = SanitizeForCpp(prop->GetType().second);
            const std::string nameStr = SanitizeIdentForCpp(prop->GetName());
            const int32_t arrDim = prop->GetArrayDim();

            Param p;
            p.Type = typeStr;
            p.Name = nameStr;
            p.Flags = flags;
            p.ArrayDim = arrDim;
            out->ParamsList.push_back(std::move(p));

            if (arrDim > 1)
            {
                out->Params += fmt::format("{}* {}, ", typeStr, nameStr);
            }
            else
            {
                if ((flags & CPF_OutParm) && !(flags & CPF_ConstParm))
                {
                    out->Params += fmt::format("{}& {}, ", typeStr, nameStr);
                }
                else if ((flags & CPF_OutParm) && (flags & CPF_ConstParm))
                {
                    out->Params += fmt::format("const {}& {}, ", typeStr, nameStr);
                }
                else
                {
                    out->Params += fmt::format("{} {}, ", typeStr, nameStr);
                }
            }
        }
    };

    for (auto prop = fn.GetChildProperties().Cast<UE_FProperty>(); prop; prop = prop.GetNext().Cast<UE_FProperty>())
    {
        auto propInterface = prop.GetInterface();
        generateParam(&propInterface);
    }
    for (auto prop = fn.GetChildren().Cast<UE_UProperty>(); prop; prop = prop.GetNext().Cast<UE_UProperty>())
    {
        auto propInterface = prop.GetInterface();
        generateParam(&propInterface);
    }
    if (out->Params.size())
    {
        out->Params.erase(out->Params.size() - 2);
    }

    // CppName composes the C++ declaration head emitted in the class body:
    //   [static ]<ReturnType> <FuncName>
    // …and AppendStructsToBuffer appends `(<Params>);` to it.
    out->CppName.clear();
    if (out->IsStatic)
        out->CppName += "static ";
    out->CppName += out->ReturnType;
    out->CppName += ' ';
    out->CppName += sanitizedFuncName;
}

void UE_UPackage::GenerateStruct(const UE_UStruct &object, std::vector<Struct> &arr)
{
    Struct s;
    s.Name = object.GetName();
    s.FullName = object.GetFullName();

    s.CppNameOnly = SanitizeIdentifier(object.GetCppName());
    s.CppName = "struct ";
    s.CppName += s.CppNameOnly;

    s.Inherited = 0;
    s.Size = object.GetSize();

    if (s.Size == 0)
    {
        arr.push_back(s);
        return;
    }

    auto super = object.GetSuper();
    if (super)
    {
        s.SuperCppName = SanitizeIdentifier(super.GetCppName());
        s.CppName += " : ";
        s.CppName += s.SuperCppName;
        s.Inherited = super.GetSize();
        // Inheriting requires the complete super type
        s.FullDeps.insert(s.SuperCppName);
    }

    uint32_t offset = s.Inherited;
    uint8_t bitOffset = 0;

    auto generateMember = [&](IProperty *prop, Member *m)
    {
        auto arrDim = prop->GetArrayDim();
        m->Size = prop->GetSize() * arrDim;
        if (m->Size == 0)
        {
            return;
        }  // this shouldn't be zero

        auto type = prop->GetType();
        m->Type = SanitizeForCpp(type.second);
        m->Name = SanitizeIdentifier(prop->GetName());
        m->Offset = prop->GetOffset();

        // Unknown property types come back with a placeholder name (often
        // "None"/"ENone") whose definition would never compile. Replace
        // the type with an opaque byte buffer matching the dumped size —
        // readers still get correct offsets, just no field-typed access.
        if (type.first == UEPropertyType::Unknown
            || m->Type.empty()
            || m->Type == "None"
            || m->Type == "ENone")
        {
            if (m->Size <= 1)
            {
                m->Type = "uint8_t";
            }
            else
            {
                m->Type = "uint8_t";
                m->Name += fmt::format("[0x{:X}]", m->Size);
            }
        }

        // Track which other dumped types this member references
        ExtractTypeDeps(m->Type, s.FullDeps, s.ForwardDeps);

        if (m->Offset > offset)
        {
            UE_UPackage::FillPadding(object, s.Members, offset, bitOffset, m->Offset);
        }
        if (type.first == UEPropertyType::BoolProperty && *(uint32_t *)type.second.data() != 'loob')
        {
            auto boolProp = prop;
            auto mask = boolProp->GetFieldMask();
            uint8_t zeros = 0, ones = 0;
            while (mask & ~1)
            {
                mask >>= 1;
                zeros++;
            }
            while (mask & 1)
            {
                mask >>= 1;
                ones++;
            }
            if (ones == 0)
            {
                // Mask was zero (or unrecognised). C++ rejects a named
                // zero-width bit-field, so fall back to a full byte.
                offset += m->Size;
                m->extra = fmt::format("Mask(0x{:X})", boolProp->GetFieldMask());
            }
            else
            {
                if (zeros > bitOffset)
                {
                    UE_UPackage::GenerateBitPadding(s.Members, offset, bitOffset, zeros - bitOffset);
                    bitOffset = zeros;
                }
                m->Name += fmt::format(" : {}", ones);
                bitOffset += ones;

                if (bitOffset == 8)
                {
                    offset++;
                    bitOffset = 0;
                }

                m->extra = fmt::format("Mask(0x{:X})", boolProp->GetFieldMask());
            }
        }
        else
        {
            if (arrDim > 1)
            {
                m->Name += fmt::format("[0x{:X}]", arrDim);
            }

            offset += m->Size;
        }
    };

    for (auto prop = object.GetChildProperties().Cast<UE_FProperty>(); prop; prop = prop.GetNext().Cast<UE_FProperty>())
    {
        Member m;
        auto propInterface = prop.GetInterface();
        generateMember(&propInterface, &m);
        s.Members.push_back(m);
    }

    for (auto child = object.GetChildren(); child; child = child.GetNext())
    {
        if (child.IsA<UE_UFunction>())
        {
            auto fn = child.Cast<UE_UFunction>();
            Function f;
            GenerateFunction(fn, &f);
            f.OwnerCppName = s.CppNameOnly;
            f.OwnerUEName = s.Name;
            // Function param/return types reference other types but only
            // through value/reference parameters in declarations — these
            // can use forward declarations because the function body lives
            // outside this header.
            ExtractTypeDeps(f.CppName, s.ForwardDeps, s.ForwardDeps);
            ExtractTypeDeps(f.Params, s.ForwardDeps, s.ForwardDeps);
            s.Functions.push_back(f);
        }
        else if (child.IsA<UE_UProperty>())
        {
            auto prop = child.Cast<UE_UProperty>();
            Member m;
            auto propInterface = prop.GetInterface();
            generateMember(&propInterface, &m);
            s.Members.push_back(m);
        }
    }

    if (s.Size > offset)
    {
        UE_UPackage::FillPadding(object, s.Members, offset, bitOffset, s.Size);
    }

    // Self-references in templated members (e.g., TArray<FFoo>) must not
    // make the struct depend on itself.
    s.FullDeps.erase(s.CppNameOnly);
    s.ForwardDeps.erase(s.CppNameOnly);

    arr.push_back(s);
}

void UE_UPackage::GenerateEnum(const UE_UEnum &object, std::vector<Enum> &arr)
{
    Enum e;
    e.FullName = object.GetFullName();

    uint64_t nameSize = GetPtrAlignedOf(UEWrappers::GetUEVars()->GetOffsets()->FName.Size);
    uint64_t pairSize = nameSize + sizeof(int64_t);

    auto names = object.GetNames();
    uint64_t max = 0;

    std::unordered_set<std::string> seenEnumNames;
    for (int32_t i = 0; i < names.Num(); i++)
    {
        auto pair = names.GetData() + i * pairSize;
        auto name = UE_FName(pair);
        auto str = name.GetName();
        auto pos = str.find_last_of(':');
        if (pos != std::string::npos)
            str = str.substr(pos + 1);

        auto value = vm_rpm_ptr<uint64_t>(pair + nameSize);
        if (value > max)
            max = value;

        std::string sanitized = SanitizeIdentifier(str);
        // UE reflection occasionally exposes duplicate enumerator names within
        // the same enum (different underlying values). C++ would reject the
        // redefinition, so we drop later occurrences.
        if (!seenEnumNames.insert(sanitized).second)
            continue;

        e.Members.emplace_back(std::move(sanitized), value);
    }

    // enum values should be in ascending order
    auto isUninitializedEnum = [](Enum &e) -> bool
    {
        if (e.Members.size() > 1)
        {
            for (size_t i = 1; i < e.Members.size(); ++i)
            {
                if (e.Members[i].second <= e.Members[i - 1].second)
                    return true;
            }
        }
        return false;
    };

    if (isUninitializedEnum(e))
    {
        max = e.Members.size();
        for (size_t i = 0; i < e.Members.size(); ++i)
        {
            e.Members[i].second = i;
        }
    }

    if (max > GetMaxOfType<uint32_t>())
        e.UnderlyingType = "uint64_t";
    else if (max > GetMaxOfType<uint16_t>())
        e.UnderlyingType = "uint32_t";
    else if (max > GetMaxOfType<uint8_t>())
        e.UnderlyingType = "uint16_t";
    else
        e.UnderlyingType = "uint8_t";

    e.CppNameOnly = SanitizeIdentifier(object.GetName());
    e.CppName = "enum class " + e.CppNameOnly + " : " + e.UnderlyingType;

    if (e.Members.size())
    {
        arr.push_back(e);
    }
}

void UE_UPackage::AppendStructsToBuffer(std::vector<Struct> &arr, BufferFmt *pBufFmt)
{
    for (auto &s : arr)
    {
        pBufFmt->append("// Object: {}\n// Size: 0x{:X} (Inherited: 0x{:X})\n{}\n{{",
                        s.FullName, s.Size, s.Inherited, s.CppName);

        if (s.Members.size())
        {
            for (auto &m : s.Members)
            {
                pBufFmt->append("\n\t{} {}; // 0x{:X}(0x{:X})", m.Type, m.Name, m.Offset, m.Size);
                if (!m.extra.empty())
                {
                    pBufFmt->append(", {}", m.extra);
                }
            }
        }
        if (s.Functions.size())
        {
            if (s.Members.size())
                pBufFmt->append("\n");

            for (auto &f : s.Functions)
            {
                void *funcOffset = f.Func ? (void *)(f.Func - UEWrappers::GetUEVars()->GetBaseAddress()) : nullptr;
                pBufFmt->append("\n\n\t// Object: {}\n\t// Flags: [{}]\n\t// Offset: {}\n\t// Params: [ Num({}) Size(0x{:X}) ]\n\t{}({});", f.FullName, f.Flags, funcOffset, f.NumParams, f.ParamSize, f.CppName, f.Params);
            }
        }
        if (!s.ExtraDecls.empty())
        {
            if (s.Members.size() || s.Functions.size())
                pBufFmt->append("\n");
            pBufFmt->append("{}", s.ExtraDecls);
        }
        pBufFmt->append("\n}};\n\n");
    }
}

void UE_UPackage::AppendEnumsToBuffer(std::vector<Enum> &arr, BufferFmt *pBufFmt)
{
    for (auto &e : arr)
    {
        pBufFmt->append("// Object: {}\n{}\n{{", e.FullName, e.CppName);

        size_t lastIdx = e.Members.size() - 1;
        for (size_t i = 0; i < lastIdx; i++)
        {
            auto &m = e.Members.at(i);
            pBufFmt->append("\n\t{} = {},", m.first, m.second);
        }

        auto &m = e.Members.at(lastIdx);
        pBufFmt->append("\n\t{} = {}", m.first, m.second);

        pBufFmt->append("\n}};\n\n");
    }
}

void UE_UPackage::Process()
{
    PackageName = GetObject().GetName();
    auto &objects = Package->second;
    for (auto &object : objects)
    {
        if (object.IsA<UE_UClass>())
        {
            GenerateStruct(object.Cast<UE_UStruct>(), Classes);
        }
        else if (object.IsA<UE_UScriptStruct>())
        {
            GenerateStruct(object.Cast<UE_UStruct>(), Structures);
        }
        else if (object.IsA<UE_UEnum>())
        {
            GenerateEnum(object.Cast<UE_UEnum>(), Enums);
        }
    }
}
