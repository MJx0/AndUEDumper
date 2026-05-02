#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Utils/BufferFmt.hpp"
#include "Utils/ProgressUtils.hpp"

#include "UE/UEWrappers.hpp"

class UE_UPackage
{
public:
    struct Member
    {
        std::string Type;
        std::string Name;
        std::string extra;  // extra comment
        uint32_t Offset = 0;
        uint32_t Size = 0;
    };
    struct Function
    {
        std::string Name;
        std::string FullName;
        std::string CppName;
        std::string Params;
        uint32_t EFlags = 0;
        std::string Flags;
        int8_t NumParams = 0;
        int16_t ParamSize = 0;
        uintptr_t Func = 0;
    };
    struct Struct
    {
        std::string Name;
        std::string FullName;
        std::string CppName;        // "struct FFoo : FBar" or "struct UBaz"
        std::string CppNameOnly;    // just "FFoo" / "UBaz"
        std::string SuperCppName;   // just "FBar" or empty
        uint32_t Inherited = 0;
        uint32_t Size = 0;
        std::vector<Member> Members;
        std::vector<Function> Functions;
        // Names of other dumped types this struct needs as a complete type
        // (inheritance, value-type members). Populated during Process().
        std::set<std::string> FullDeps;
        // Names of other dumped types referenced via pointer/template
        // wrappers — only forward declarations are required to compile.
        std::set<std::string> ForwardDeps;
        // Optional extra C++ source appended verbatim inside the struct body
        // (after Members and Functions). Used by the AIOCore augmenter to
        // inject inline method declarations on core reflection types.
        std::string ExtraDecls;
    };
    struct Enum
    {
        std::string FullName;
        std::string CppName;       // "enum class EFoo : uint8_t"
        std::string CppNameOnly;   // just "EFoo"
        std::string UnderlyingType;// "uint8_t" / "uint16_t" ...
        std::vector<std::pair<std::string, uint64_t>> Members;
    };

private:
    std::pair<uint8_t *const, std::vector<UE_UObject>> *Package;

public:
    std::string PackageName;                      // package object name
    std::vector<Struct> Classes;
    std::vector<Struct> Structures;
    std::vector<Enum> Enums;

private:
    static void GenerateFunction(const UE_UFunction &fn, Function *out);
    static void GenerateStruct(const UE_UStruct &object, std::vector<Struct> &arr);
    static void GenerateEnum(const UE_UEnum &object, std::vector<Enum> &arr);

    static void GenerateBitPadding(std::vector<Member> &members, uint32_t offset, uint8_t bitOffset, uint8_t size);
    static void GeneratePadding(std::vector<Member> &members, uint32_t offset, uint32_t size);
    static void FillPadding(const UE_UStruct &object, std::vector<Member> &members, uint32_t &offset, uint8_t &bitOffset, uint32_t end);

public:
    UE_UPackage(std::pair<uint8_t *const, std::vector<UE_UObject>> &package) : Package(&package) {};
    inline UE_UObject GetObject() const { return UE_UObject(Package->first); }
    void Process();

    // Parse a C++ type string (as produced by the dumper) and split the
    // referenced dumped-type names into "full" deps (need a complete type)
    // and "forward" deps (a forward declaration is enough). Builtins,
    // primitives and predefined wrapper templates are filtered out.
    static void ExtractTypeDeps(const std::string &typeStr,
                                std::set<std::string> &fullDeps,
                                std::set<std::string> &fwdDeps);

    static void AppendStructsToBuffer(std::vector<Struct> &arr, class BufferFmt *bufFmt);
    static void AppendEnumsToBuffer(std::vector<Enum> &arr, class BufferFmt *bufFmt);
};
