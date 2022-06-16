//===--- PrintAsClang.cpp - Emit a header file for a Swift AST ------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/PrintAsClang/PrintAsClang.h"

#include "ModuleContentsWriter.h"
#include "SwiftToClangInteropContext.h"

#include "swift/AST/ASTContext.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/Basic/Version.h"
#include "swift/ClangImporter/ClangImporter.h"

#include "clang/Basic/Module.h"

#include "llvm/Support/raw_ostream.h"

using namespace swift;

static void emitCxxConditional(raw_ostream &out,
                               llvm::function_ref<void()> cxxCase,
                               llvm::function_ref<void()> cCase = {}) {
  out << "#if defined(__cplusplus)\n";
  cxxCase();
  if (cCase) {
    out << "#else\n";
    cCase();
  }
  out << "#endif\n";
}

static void emitObjCConditional(raw_ostream &out,
                                llvm::function_ref<void()> objcCase,
                                llvm::function_ref<void()> nonObjCCase = {}) {
  out << "#if defined(__OBJC__)\n";
  objcCase();
  if (nonObjCCase) {
    out << "#else\n";
    nonObjCCase();
  }
  out << "#endif\n";
}

static void writePtrauthPrologue(raw_ostream &os) {
  emitCxxConditional(os, [&]() {
    os << "#if __has_include(<ptrauth.h>)\n";
    os << "# include <ptrauth.h>\n";
    os << "#else\n";
    os << "# ifndef __ptrauth_swift_value_witness_function_pointer\n";
    os << "#  define __ptrauth_swift_value_witness_function_pointer(x)\n";
    os << "# endif\n";
    os << "#endif\n";
  });
}

static void writePrologue(raw_ostream &out, ASTContext &ctx,
                          StringRef macroGuard) {

  out << "// Generated by "
      << version::getSwiftFullVersion(ctx.LangOpts.EffectiveLanguageVersion)
      << "\n"
      // Guard against recursive definition.
      << "#ifndef " << macroGuard << "\n"
      << "#define " << macroGuard
      << "\n"
         "#pragma clang diagnostic push\n"
         "#pragma clang diagnostic ignored \"-Wgcc-compat\"\n"
         "\n"
         "#if !defined(__has_include)\n"
         "# define __has_include(x) 0\n"
         "#endif\n"
         "#if !defined(__has_attribute)\n"
         "# define __has_attribute(x) 0\n"
         "#endif\n"
         "#if !defined(__has_feature)\n"
         "# define __has_feature(x) 0\n"
         "#endif\n"
         "#if !defined(__has_warning)\n"
         "# define __has_warning(x) 0\n"
         "#endif\n"
         "\n"
         "#if __has_include(<swift/objc-prologue.h>)\n"
         "# include <swift/objc-prologue.h>\n"
         "#endif\n"
         "\n"
         "#pragma clang diagnostic ignored \"-Wauto-import\"\n";
  emitObjCConditional(out,
                      [&] { out << "#include <Foundation/Foundation.h>\n"; });
  emitCxxConditional(
      out,
      [&] {
        out << "#include <cstdint>\n"
               "#include <cstddef>\n"
               "#include <cstdbool>\n"
               "#include <cstring>\n";
        out << "#include <cstdlib>\n";
      },
      [&] {
        out << "#include <stdint.h>\n"
               "#include <stddef.h>\n"
               "#include <stdbool.h>\n"
               "#include <string.h>\n";
      });
  writePtrauthPrologue(out);
  out << "\n"
         "#if !defined(SWIFT_TYPEDEFS)\n"
         "# define SWIFT_TYPEDEFS 1\n"
         "# if __has_include(<uchar.h>)\n"
         "#  include <uchar.h>\n"
         "# elif !defined(__cplusplus)\n"
         "typedef uint_least16_t char16_t;\n"
         "typedef uint_least32_t char32_t;\n"
         "# endif\n"
#define MAP_SIMD_TYPE(C_TYPE, SCALAR_TYPE, _) \
         "typedef " #SCALAR_TYPE " swift_" #C_TYPE "2"       \
         "  __attribute__((__ext_vector_type__(2)));\n" \
         "typedef " #SCALAR_TYPE " swift_" #C_TYPE "3"       \
         "  __attribute__((__ext_vector_type__(3)));\n" \
         "typedef " #SCALAR_TYPE " swift_" #C_TYPE "4"       \
         "  __attribute__((__ext_vector_type__(4)));\n"
#include "swift/ClangImporter/SIMDMappedTypes.def"
         "#endif\n"
         "\n"
         "#if !defined(SWIFT_PASTE)\n"
         "# define SWIFT_PASTE_HELPER(x, y) x##y\n"
         "# define SWIFT_PASTE(x, y) SWIFT_PASTE_HELPER(x, y)\n"
         "#endif"
         "\n"
         "#if !defined(SWIFT_METATYPE)\n"
         "# define SWIFT_METATYPE(X) Class\n"
         "#endif\n"
         "#if !defined(SWIFT_CLASS_PROPERTY)\n"
         "# if __has_feature(objc_class_property)\n"
         "#  define SWIFT_CLASS_PROPERTY(...) __VA_ARGS__\n"
         "# else\n"
         "#  define SWIFT_CLASS_PROPERTY(...)\n"
         "# endif\n"
         "#endif\n"
         "\n"
         "#if __has_attribute(objc_runtime_name)\n"
         "# define SWIFT_RUNTIME_NAME(X) "
         "__attribute__((objc_runtime_name(X)))\n"
         "#else\n"
         "# define SWIFT_RUNTIME_NAME(X)\n"
         "#endif\n"
         "#if __has_attribute(swift_name)\n"
         "# define SWIFT_COMPILE_NAME(X) "
         "__attribute__((swift_name(X)))\n"
         "#else\n"
         "# define SWIFT_COMPILE_NAME(X)\n"
         "#endif\n"
         "#if __has_attribute(objc_method_family)\n"
         "# define SWIFT_METHOD_FAMILY(X) "
         "__attribute__((objc_method_family(X)))\n"
         "#else\n"
         "# define SWIFT_METHOD_FAMILY(X)\n"
         "#endif\n"
         "#if __has_attribute(noescape)\n"
         "# define SWIFT_NOESCAPE __attribute__((noescape))\n"
         "#else\n"
         "# define SWIFT_NOESCAPE\n"
         "#endif\n"
         "#if __has_attribute(ns_consumed)\n"
         "# define SWIFT_RELEASES_ARGUMENT __attribute__((ns_consumed))\n"
         "#else\n"
         "# define SWIFT_RELEASES_ARGUMENT\n"
         "#endif\n"
         "#if __has_attribute(warn_unused_result)\n"
         "# define SWIFT_WARN_UNUSED_RESULT "
         "__attribute__((warn_unused_result))\n"
         "#else\n"
         "# define SWIFT_WARN_UNUSED_RESULT\n"
         "#endif\n"
         "#if __has_attribute(noreturn)\n"
         "# define SWIFT_NORETURN __attribute__((noreturn))\n"
         "#else\n"
         "# define SWIFT_NORETURN\n"
         "#endif\n"
         "#if !defined(SWIFT_CLASS_EXTRA)\n"
         "# define SWIFT_CLASS_EXTRA\n"
         "#endif\n"
         "#if !defined(SWIFT_PROTOCOL_EXTRA)\n"
         "# define SWIFT_PROTOCOL_EXTRA\n"
         "#endif\n"
         "#if !defined(SWIFT_ENUM_EXTRA)\n"
         "# define SWIFT_ENUM_EXTRA\n"
         "#endif\n"
         "#if !defined(SWIFT_CLASS)\n"
         "# if __has_attribute(objc_subclassing_restricted)\n"
         "#  define SWIFT_CLASS(SWIFT_NAME) SWIFT_RUNTIME_NAME(SWIFT_NAME) "
         "__attribute__((objc_subclassing_restricted)) "
         "SWIFT_CLASS_EXTRA\n"
         "#  define SWIFT_CLASS_NAMED(SWIFT_NAME) "
         "__attribute__((objc_subclassing_restricted)) "
         "SWIFT_COMPILE_NAME(SWIFT_NAME) "
         "SWIFT_CLASS_EXTRA\n"
         "# else\n"
         "#  define SWIFT_CLASS(SWIFT_NAME) SWIFT_RUNTIME_NAME(SWIFT_NAME) "
         "SWIFT_CLASS_EXTRA\n"
         "#  define SWIFT_CLASS_NAMED(SWIFT_NAME) "
         "SWIFT_COMPILE_NAME(SWIFT_NAME) "
         "SWIFT_CLASS_EXTRA\n"
         "# endif\n"
         "#endif\n"
         "#if !defined(SWIFT_RESILIENT_CLASS)\n"
         "# if __has_attribute(objc_class_stub)\n"
         "#  define SWIFT_RESILIENT_CLASS(SWIFT_NAME) SWIFT_CLASS(SWIFT_NAME) "
         "__attribute__((objc_class_stub))\n"
         "#  define SWIFT_RESILIENT_CLASS_NAMED(SWIFT_NAME) "
         "__attribute__((objc_class_stub)) "
         "SWIFT_CLASS_NAMED(SWIFT_NAME)\n"
         "# else\n"
         "#  define SWIFT_RESILIENT_CLASS(SWIFT_NAME) "
         "SWIFT_CLASS(SWIFT_NAME)\n"
         "#  define SWIFT_RESILIENT_CLASS_NAMED(SWIFT_NAME) "
         "SWIFT_CLASS_NAMED(SWIFT_NAME)\n"
         "# endif\n"
         "#endif\n"
         "\n"
         "#if !defined(SWIFT_PROTOCOL)\n"
         "# define SWIFT_PROTOCOL(SWIFT_NAME) SWIFT_RUNTIME_NAME(SWIFT_NAME) "
         "SWIFT_PROTOCOL_EXTRA\n"
         "# define SWIFT_PROTOCOL_NAMED(SWIFT_NAME) "
         "SWIFT_COMPILE_NAME(SWIFT_NAME) "
         "SWIFT_PROTOCOL_EXTRA\n"
         "#endif\n"
         "\n"
         "#if !defined(SWIFT_EXTENSION)\n"
         "# define SWIFT_EXTENSION(M) SWIFT_PASTE(M##_Swift_, __LINE__)\n"
         "#endif\n"
         "\n"
         "#if !defined(OBJC_DESIGNATED_INITIALIZER)\n"
         "# if __has_attribute(objc_designated_initializer)\n"
         "#  define OBJC_DESIGNATED_INITIALIZER "
         "__attribute__((objc_designated_initializer))\n"
         "# else\n"
         "#  define OBJC_DESIGNATED_INITIALIZER\n"
         "# endif\n"
         "#endif\n"
         "#if !defined(SWIFT_ENUM_ATTR)\n"
         "# if defined(__has_attribute) && "
         "__has_attribute(enum_extensibility)\n"
         "#  define SWIFT_ENUM_ATTR(_extensibility) "
         "__attribute__((enum_extensibility(_extensibility)))\n"
         "# else\n"
         "#  define SWIFT_ENUM_ATTR(_extensibility)\n"
         "# endif\n"
         "#endif\n"
         "#if !defined(SWIFT_ENUM)\n"
         "# define SWIFT_ENUM(_type, _name, _extensibility) "
         "enum _name : _type _name; "
         "enum SWIFT_ENUM_ATTR(_extensibility) SWIFT_ENUM_EXTRA "
         "_name : _type\n"
         "# if __has_feature(generalized_swift_name)\n"
         "#  define SWIFT_ENUM_NAMED(_type, _name, SWIFT_NAME, "
         "_extensibility) "
         "enum _name : _type _name SWIFT_COMPILE_NAME(SWIFT_NAME); "
         "enum SWIFT_COMPILE_NAME(SWIFT_NAME) "
         "SWIFT_ENUM_ATTR(_extensibility) SWIFT_ENUM_EXTRA _name : _type\n"
         "# else\n"
         "#  define SWIFT_ENUM_NAMED(_type, _name, SWIFT_NAME, "
         "_extensibility) SWIFT_ENUM(_type, _name, _extensibility)\n"
         "# endif\n"
         "#endif\n"
         "#if !defined(SWIFT_UNAVAILABLE)\n"
         "# define SWIFT_UNAVAILABLE __attribute__((unavailable))\n"
         "#endif\n"
         "#if !defined(SWIFT_UNAVAILABLE_MSG)\n"
         "# define SWIFT_UNAVAILABLE_MSG(msg) "
         "__attribute__((unavailable(msg)))\n"
         "#endif\n"
         "#if !defined(SWIFT_AVAILABILITY)\n"
         "# define SWIFT_AVAILABILITY(plat, ...) "
         "__attribute__((availability(plat, __VA_ARGS__)))\n"
         "#endif\n"
         "#if !defined(SWIFT_WEAK_IMPORT)\n"
         "# define SWIFT_WEAK_IMPORT __attribute__((weak_import))\n"
         "#endif\n"
         "#if !defined(SWIFT_DEPRECATED)\n"
         "# define SWIFT_DEPRECATED __attribute__((deprecated))\n"
         "#endif\n"
         "#if !defined(SWIFT_DEPRECATED_MSG)\n"
         "# define SWIFT_DEPRECATED_MSG(...) "
         "__attribute__((deprecated(__VA_ARGS__)))\n"
         "#endif\n"
         "#if __has_feature(attribute_diagnose_if_objc)\n"
         "# define SWIFT_DEPRECATED_OBJC(Msg) __attribute__((diagnose_if(1, "
         "Msg, \"warning\")))\n"
         "#else\n"
         "# define SWIFT_DEPRECATED_OBJC(Msg) SWIFT_DEPRECATED_MSG(Msg)\n"
         "#endif\n";
  emitObjCConditional(out, [&] {
    out << "#if !defined(IBSegueAction)\n"
           "# define IBSegueAction\n"
           "#endif\n";
  });
  out << "#if !defined(SWIFT_EXTERN)\n"
         "# if defined(__cplusplus)\n"
         "#  define SWIFT_EXTERN extern \"C\"\n"
         "# else\n"
         "#  define SWIFT_EXTERN extern\n"
         "# endif\n"
         "#endif\n";
  auto emitMacro = [&](StringRef name, StringRef value = "") {
    out << "#if !defined(" << name << ")\n";
    out << "# define " << name << " " << value << "\n";
    out << "#endif\n";
  };
  emitMacro("SWIFT_CALL", "__attribute__((swiftcall))");
  emitMacro("SWIFT_INDIRECT_RESULT", "__attribute__((swift_indirect_result))");
  emitMacro("SWIFT_CONTEXT", "__attribute__((swift_context))");
  // SWIFT_NOEXCEPT applies 'noexcept' in C++ mode only.
  emitCxxConditional(
      out, [&] { emitMacro("SWIFT_NOEXCEPT", "noexcept"); },
      [&] { emitMacro("SWIFT_NOEXCEPT"); });
  emitCxxConditional(out, [&] {
    out << "#if !defined(SWIFT_CXX_INT_DEFINED)\n";
    out << "#define SWIFT_CXX_INT_DEFINED\n";
    out << "namespace swift {\n";
    out << "using Int = ptrdiff_t;\n";
    out << "using UInt = size_t;\n";
    out << "}\n";
    out << "#endif\n";
  });
  static_assert(SWIFT_MAX_IMPORTED_SIMD_ELEMENTS == 4,
              "need to add SIMD typedefs here if max elements is increased");
}

static int compareImportModulesByName(const ImportModuleTy *left,
                                      const ImportModuleTy *right) {
  auto *leftSwiftModule = left->dyn_cast<ModuleDecl *>();
  auto *rightSwiftModule = right->dyn_cast<ModuleDecl *>();

  if (leftSwiftModule && !rightSwiftModule)
    return -compareImportModulesByName(right, left);

  if (leftSwiftModule && rightSwiftModule)
    return leftSwiftModule->getName().compare(rightSwiftModule->getName());

  auto *leftClangModule = left->get<const clang::Module *>();
  assert(leftClangModule->isSubModule() &&
         "top-level modules should use a normal swift::ModuleDecl");
  if (rightSwiftModule) {
    // Because the Clang module is a submodule, its full name will never be
    // equal to a Swift module's name, even if the top-level name is the same;
    // it will always come before or after.
    if (leftClangModule->getTopLevelModuleName() <
        rightSwiftModule->getName().str()) {
      return -1;
    }
    return 1;
  }

  auto *rightClangModule = right->get<const clang::Module *>();
  assert(rightClangModule->isSubModule() &&
         "top-level modules should use a normal swift::ModuleDecl");

  SmallVector<StringRef, 8> leftReversePath(
      ModuleDecl::ReverseFullNameIterator(leftClangModule), {});
  SmallVector<StringRef, 8> rightReversePath(
      ModuleDecl::ReverseFullNameIterator(rightClangModule), {});

  assert(leftReversePath != rightReversePath &&
         "distinct Clang modules should not have the same full name");
  if (std::lexicographical_compare(leftReversePath.rbegin(),
                                   leftReversePath.rend(),
                                   rightReversePath.rbegin(),
                                   rightReversePath.rend())) {
    return -1;
  }
  return 1;
}

static void writeImports(raw_ostream &out,
                         llvm::SmallPtrSetImpl<ImportModuleTy> &imports,
                         ModuleDecl &M, StringRef bridgingHeader) {
  out << "#if __has_feature(modules)\n";

  out << "#if __has_warning(\"-Watimport-in-framework-header\")\n"
      << "#pragma clang diagnostic ignored \"-Watimport-in-framework-header\"\n"
      << "#endif\n";

  // Sort alphabetically for determinism and consistency.
  SmallVector<ImportModuleTy, 8> sortedImports{imports.begin(),
                                               imports.end()};
  llvm::array_pod_sort(sortedImports.begin(), sortedImports.end(),
                       &compareImportModulesByName);

  auto isUnderlyingModule = [&M, bridgingHeader](ModuleDecl *import) -> bool {
    if (bridgingHeader.empty())
      return import != &M && import->getName() == M.getName();

    auto importer = static_cast<ClangImporter *>(
        import->getASTContext().getClangModuleLoader());
    return import == importer->getImportedHeaderModule();
  };

  // Track printed names to handle overlay modules.
  llvm::SmallPtrSet<Identifier, 8> seenImports;
  bool includeUnderlying = false;
  for (auto import : sortedImports) {
    if (auto *swiftModule = import.dyn_cast<ModuleDecl *>()) {
      auto Name = swiftModule->getName();
      if (isUnderlyingModule(swiftModule)) {
        includeUnderlying = true;
        continue;
      }
      if (seenImports.insert(Name).second)
        out << "@import " << Name.str() << ";\n";
    } else {
      const auto *clangModule = import.get<const clang::Module *>();
      assert(clangModule->isSubModule() &&
             "top-level modules should use a normal swift::ModuleDecl");
      out << "@import ";
      ModuleDecl::ReverseFullNameIterator(clangModule).printForward(out);
      out << ";\n";
    }
  }

  out << "#endif\n\n";

  if (includeUnderlying) {
    if (bridgingHeader.empty())
      out << "#import <" << M.getName().str() << '/' << M.getName().str()
          << ".h>\n\n";
    else
      out << "#import \"" << bridgingHeader << "\"\n\n";
  }
}

static void writePostImportPrologue(raw_ostream &os, ModuleDecl &M) {
  os << "#pragma clang diagnostic ignored \"-Wproperty-attribute-mismatch\"\n"
        "#pragma clang diagnostic ignored \"-Wduplicate-method-arg\"\n"
        "#if __has_warning(\"-Wpragma-clang-attribute\")\n"
        "# pragma clang diagnostic ignored \"-Wpragma-clang-attribute\"\n"
        "#endif\n"
        "#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\n"
        "#pragma clang diagnostic ignored \"-Wnullability\"\n"
        "#pragma clang diagnostic ignored "
        "\"-Wdollar-in-identifier-extension\"\n"
        "\n"
        "#if __has_attribute(external_source_symbol)\n"
        "# pragma push_macro(\"any\")\n"
        "# undef any\n"
        "# pragma clang attribute push("
        "__attribute__((external_source_symbol(language=\"Swift\", "
        "defined_in=\""
     << M.getNameStr()
     << "\",generated_declaration))), "
        "apply_to=any(function,enum,objc_interface,objc_category,"
        "objc_protocol))\n"
        "# pragma pop_macro(\"any\")\n"
        "#endif\n\n";
}

static void writeEpilogue(raw_ostream &os) {
  os <<
      "#if __has_attribute(external_source_symbol)\n"
      "# pragma clang attribute pop\n"
      "#endif\n"
      "#pragma clang diagnostic pop\n"
      // For the macro guard against recursive definition
      "#endif\n";
}

static std::string computeMacroGuard(const ModuleDecl *M) {
  return (llvm::Twine(M->getNameStr().upper()) + "_SWIFT_H").str();
}

static std::string
getModuleContentsCxxString(ModuleDecl &M,
                           SwiftToClangInteropContext &interopContext) {
  SmallPtrSet<ImportModuleTy, 8> imports;
  std::string moduleContentsBuf;
  llvm::raw_string_ostream moduleContents{moduleContentsBuf};
  printModuleContentsAsCxx(moduleContents, imports, M, interopContext);
  return moduleContents.str();
}

bool swift::printAsClangHeader(raw_ostream &os, ModuleDecl *M,
                               StringRef bridgingHeader,
                               bool ExposePublicDeclsInClangHeader,
                               const IRGenOptions &irGenOpts) {
  llvm::PrettyStackTraceString trace("While generating Clang header");

  SwiftToClangInteropContext interopContext(*M, irGenOpts);

  SmallPtrSet<ImportModuleTy, 8> imports;
  std::string objcModuleContentsBuf;
  llvm::raw_string_ostream objcModuleContents{objcModuleContentsBuf};
  printModuleContentsAsObjC(objcModuleContents, imports, *M, interopContext);
  writePrologue(os, M->getASTContext(), computeMacroGuard(M));
  emitObjCConditional(os,
                      [&] { writeImports(os, imports, *M, bridgingHeader); });
  writePostImportPrologue(os, *M);
  emitObjCConditional(os, [&] { os << objcModuleContents.str(); });
  emitCxxConditional(os, [&] {
    // FIXME: Expose Swift with @expose by default.
    if (ExposePublicDeclsInClangHeader) {
      os << getModuleContentsCxxString(*M, interopContext);
    }
  });
  writeEpilogue(os);

  return false;
}
