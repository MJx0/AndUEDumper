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

// Templates whose stored data is a pointer/handle to T — therefore a forward
// declaration of T is sufficient for the surrounding struct to be sized.
static bool IsForwardDeclWrapper(const std::string &ident)
{
    static const std::unordered_set<std::string> wrappers = {
        "TArray", "TMap", "TSet", "TPair",
        "TWeakObjectPtr", "TLazyObjectPtr",
        "TSoftObjectPtr", "TSoftClassPtr",
        "TSubclassOf", "TScriptInterface", "TFieldPath",
    };
    return wrappers.count(ident) != 0;
}

void UE_UPackage::ExtractTypeDeps(const std::string &typeStr,
                                  std::set<std::string> &fullDeps,
                                  std::set<std::string> &fwdDeps)
{
    const auto &builtins = kBuiltinIdents();

    int templateDepth = 0;
    size_t i = 0;
    const size_t n = typeStr.size();

    while (i < n)
    {
        char c = typeStr[i];
        if (c == '<') { templateDepth++; i++; continue; }
        if (c == '>') { templateDepth--; i++; continue; }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(typeStr[i])) || typeStr[i] == '_'))
                i++;
            std::string ident = typeStr.substr(start, i - start);

            // Skip whitespace looking for the next significant char
            size_t j = i;
            while (j < n && std::isspace(static_cast<unsigned char>(typeStr[j])))
                j++;
            const bool isPointer = (j < n && typeStr[j] == '*');

            if (builtins.count(ident))
                continue;

            // Wrapper template names themselves don't add dependencies — we
            // walk into their template arguments via the regular loop.
            if (IsForwardDeclWrapper(ident))
                continue;

            // Inside <>: stored as pointer/handle by the wrapper, so a
            // forward decl is enough. Outside <> with explicit '*' too.
            if (templateDepth > 0 || isPointer)
                fwdDeps.insert(std::move(ident));
            else
                fullDeps.insert(std::move(ident));
        }
        else
        {
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

    auto generateParam = [&](IProperty *prop)
    {
        auto flags = prop->GetPropertyFlags();

        // if property has 'ReturnParm' flag
        if (flags & CPF_ReturnParm)
        {
            out->CppName = SanitizeForCpp(prop->GetType().second) + " " + SanitizeIdentifier(fn.GetName());
        }
        // if property has 'Parm' flag
        else if (flags & CPF_Parm)
        {
            const std::string typeStr = SanitizeForCpp(prop->GetType().second);
            const std::string nameStr = SanitizeIdentifier(prop->GetName());
            if (prop->GetArrayDim() > 1)
            {
                out->Params += fmt::format("{}* {}, ", typeStr, nameStr);
            }
            else
            {
                if (flags & CPF_OutParm)
                {
                    out->Params += fmt::format("{}& {}, ", typeStr, nameStr);
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

    if (out->CppName.size() == 0)
    {
        out->CppName = "void " + SanitizeIdentifier(fn.GetName());
    }
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
