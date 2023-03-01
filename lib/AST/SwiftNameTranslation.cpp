//===--- SwiftNameTranslation.cpp - Swift to ObjC Name Translation APIs ---===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file contains utilities for translating Swift names to ObjC.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/SwiftNameTranslation.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Module.h"
#include "swift/AST/Decl.h"
#include "swift/AST/LazyResolver.h"
#include "swift/Basic/StringExtras.h"

#include "clang/AST/DeclObjC.h"
#include "llvm/ADT/SmallString.h"

using namespace swift;

StringRef swift::objc_translation::
getNameForObjC(const ValueDecl *VD, CustomNamesOnly_t customNamesOnly) {
  assert(isa<ClassDecl>(VD) || isa<ProtocolDecl>(VD) || isa<StructDecl>(VD) ||
         isa<EnumDecl>(VD) || isa<EnumElementDecl>(VD) ||
         isa<TypeAliasDecl>(VD));
  if (auto objc = VD->getAttrs().getAttribute<ObjCAttr>()) {
    if (auto name = objc->getName()) {
      assert(name->getNumSelectorPieces() == 1);
      return name->getSelectorPieces().front().str();
    }
  }

  if (customNamesOnly)
    return StringRef();

  if (auto clangDecl = dyn_cast_or_null<clang::NamedDecl>(VD->getClangDecl())) {
    if (const clang::IdentifierInfo *II = clangDecl->getIdentifier())
      return II->getName();
    if (auto *anonDecl = dyn_cast<clang::TagDecl>(clangDecl))
      if (auto *anonTypedef = anonDecl->getTypedefNameForAnonDecl())
        return anonTypedef->getIdentifier()->getName();
  }

  return VD->getBaseIdentifier().str();
}

std::string swift::objc_translation::
getErrorDomainStringForObjC(const EnumDecl *ED) {
  // Should have already been diagnosed as diag::objc_enum_generic.
  assert(!ED->isGenericContext() && "Trying to bridge generic enum error to Obj-C");

  // Clang decls have custom domains, but we shouldn't see them here anyway.
  assert(!ED->getClangDecl() && "clang decls shouldn't be re-exported");

  SmallVector<const NominalTypeDecl *, 4> outerTypes;
  for (const NominalTypeDecl * D = ED;
       D != nullptr;
       D = D->getDeclContext()->getSelfNominalTypeDecl()) {
    // We don't currently PrintAsClang any types whose parents are private or
    // fileprivate.
    assert(D->getFormalAccess() >= AccessLevel::Internal &&
            "We don't currently append private discriminators");
    outerTypes.push_back(D);
  }

  std::string buffer = ED->getParentModule()->getNameStr().str();
  for (auto D : llvm::reverse(outerTypes)) {
    buffer += ".";
    buffer += D->getNameStr();
  }

  return buffer;
}

bool swift::objc_translation::
printSwiftEnumElemNameInObjC(const EnumElementDecl *EL, llvm::raw_ostream &OS,
                             Identifier PreferredName) {
  StringRef ElemName = getNameForObjC(EL, CustomNamesOnly);
  if (!ElemName.empty()) {
    OS << ElemName;
    return true;
  }
  OS << getNameForObjC(EL->getDeclContext()->getSelfEnumDecl());
  if (PreferredName.empty())
    ElemName = EL->getBaseIdentifier().str();
  else
    ElemName = PreferredName.str();

  SmallString<64> Scratch;
  OS << camel_case::toSentencecase(ElemName, Scratch);
  return false;
}

std::pair<Identifier, ObjCSelector> swift::objc_translation::
getObjCNameForSwiftDecl(const ValueDecl *VD, DeclName PreferredName){
  ASTContext &Ctx = VD->getASTContext();
  Identifier BaseName;
  if (PreferredName) {
    auto BaseNameStr = PreferredName.getBaseName().userFacingName();
    BaseName = Ctx.getIdentifier(BaseNameStr);
  }
  if (auto *FD = dyn_cast<AbstractFunctionDecl>(VD)) {
    return {Identifier(), FD->getObjCSelector(PreferredName)};
  } else if (auto *VAD = dyn_cast<VarDecl>(VD)) {
    if (PreferredName)
      return {BaseName, ObjCSelector()};
    return {VAD->getObjCPropertyName(), ObjCSelector()};
  } else if (auto *SD = dyn_cast<SubscriptDecl>(VD)) {
    return getObjCNameForSwiftDecl(SD->getParsedAccessor(AccessorKind::Get),
                                   PreferredName);
  } else if (auto *EL = dyn_cast<EnumElementDecl>(VD)) {
    SmallString<64> Buffer;
    {
      llvm::raw_svector_ostream OS(Buffer);
      printSwiftEnumElemNameInObjC(EL, OS, BaseName);
    }
    return {Ctx.getIdentifier(Buffer.str()), ObjCSelector()};
  } else {
    // @objc(ExplicitName) > PreferredName > Swift name.
    StringRef Name = getNameForObjC(VD, CustomNamesOnly);
    if (!Name.empty())
      return {Ctx.getIdentifier(Name), ObjCSelector()};
    if (PreferredName)
      return {BaseName, ObjCSelector()};
    return {Ctx.getIdentifier(getNameForObjC(VD)), ObjCSelector()};
  }
}

bool swift::objc_translation::
isVisibleToObjC(const ValueDecl *VD, AccessLevel minRequiredAccess,
                bool checkParent) {
  if (!(VD->isObjC() || VD->getAttrs().hasAttribute<CDeclAttr>()))
    return false;
  if (VD->getFormalAccess() >= minRequiredAccess) {
    return true;
  } else if (checkParent) {
    if (auto ctor = dyn_cast<ConstructorDecl>(VD)) {
      // Check if we're overriding an initializer that is visible to obj-c
      if (auto parent = ctor->getOverriddenDecl())
        return isVisibleToObjC(parent, minRequiredAccess, false);
    }
  }
  return false;
}

StringRef
swift::cxx_translation::getNameForCxx(const ValueDecl *VD,
                                      CustomNamesOnly_t customNamesOnly) {
  if (const auto *Expose = VD->getAttrs().getAttribute<ExposeAttr>()) {
    if (!Expose->Name.empty())
      return Expose->Name;
  }

  if (customNamesOnly)
    return StringRef();

  // FIXME: String.Index should be exposed as String::Index, not _String_Index.
  if (VD->getModuleContext()->isStdlibModule() &&
      VD->getBaseIdentifier().str() == "Index") {
    return "String_Index";
  }

  return VD->getBaseIdentifier().str();
}

swift::cxx_translation::DeclRepresentation
swift::cxx_translation::getDeclRepresentation(const ValueDecl *VD) {
  if (getActorIsolation(const_cast<ValueDecl *>(VD)).isActorIsolated())
    return {Unsupported, UnrepresentableIsolatedInActor};
  Optional<CanGenericSignature> genericSignature;
  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(VD)) {
    if (AFD->hasAsync())
      return {Unsupported, UnrepresentableAsync};
    // Don't expose @_alwaysEmitIntoClient functions as they require their
    // bodies to be emitted into client.
    if (AFD->getAttrs().hasAttribute<AlwaysEmitIntoClientAttr>())
      return {Unsupported, UnrepresentableRequiresClientEmission};
    if (AFD->isGeneric())
      genericSignature = AFD->getGenericSignature().getCanonicalSignature();
  }
  if (const auto *typeDecl = dyn_cast<NominalTypeDecl>(VD)) {
    if (typeDecl->isGeneric()) {
      if (isa<ClassDecl>(VD))
        return {Unsupported, UnrepresentableGeneric};
      genericSignature =
          typeDecl->getGenericSignature().getCanonicalSignature();
    }
  }
  // Generic requirements are not yet supported in C++.
  if (genericSignature && !genericSignature->getRequirements().empty())
    return {Unsupported, UnrepresentableGenericRequirements};
  return {Representable, llvm::None};
}

bool swift::cxx_translation::isVisibleToCxx(const ValueDecl *VD,
                                            AccessLevel minRequiredAccess,
                                            bool checkParent) {
  if (VD->isObjC())
    return false;
  if (VD->getFormalAccess() >= minRequiredAccess) {
    return true;
  } else if (checkParent) {
    if (auto ctor = dyn_cast<ConstructorDecl>(VD)) {
      // Check if we're overriding an initializer that is visible to obj-c
      if (auto parent = ctor->getOverriddenDecl())
        return isVisibleToCxx(parent, minRequiredAccess, false);
    }
  }
  return false;
}
