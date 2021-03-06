//===--- TargetOptions.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::TargetOptions class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TARGETOPTIONS_H
#define LLVM_CLANG_BASIC_TARGETOPTIONS_H

#include <string>
#include <vector>
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/OpenCLOptions.h"
#include "llvm/Target/TargetOptions.h"

namespace clang {

/// Options for controlling the target.
class TargetOptions {
public:
  /// The name of the target triple to compile for.
  std::string Triple;

  /// When compiling for the device side, contains the triple used to compile
  /// for the host.
  std::string HostTriple;

  /// If given, the name of the target CPU to generate code for.
  std::string CPU;

  /// If given, the unit to use for floating point math.
  std::string FPMath;

  /// If given, the name of the target ABI to use.
  std::string ABI;

  /// The EABI version to use
  llvm::EABI EABIVersion;

  /// If given, the version string of the linker in use.
  std::string LinkerVersion;

  /// The list of target specific features to enable or disable, as written on the command line.
  std::vector<std::string> FeaturesAsWritten;

  /// The list of target specific features to enable or disable -- this should
  /// be a list of strings starting with by '+' or '-'.
  std::vector<std::string> Features;

  /// Supported OpenCL extensions and optional core features.
  OpenCLOptions SupportedOpenCLOptions;

  /// The list of OpenCL extensions to enable or disable, as written on
  /// the command line.
  std::vector<std::string> OpenCLExtensionsAsWritten;

  /// If given, enables support for __int128_t and __uint128_t types.
  bool ForceEnableInt128 = false;

  /// \brief If enabled, use 32-bit pointers for accessing const/local/shared
  /// address space.
  bool NVPTXUseShortPointers = false;

  /// The default address space for pointers.
  LangAS DefaultAddrSpace = LangAS::Default;

  /// The default address space for global data.
  LangAS StorageAddrSpace = LangAS::Default;

  /// The default address space for pointers in a system header.
  LangAS SystemAddrSpace = LangAS::Default;

  /// The default prefix to use for automatically generated 64-32 interop
  /// thunks.
  std::string Ptr32ThunkPrefix;

  /// The default name to use for the variable holding the 32-bit code
  /// segment selector.
  std::string Ptr32CS32Name;

  /// The default name to use for the variable holding the 64-bit code
  /// segment selector.
  std::string Ptr32CS64Name;

  // The code model to be used as specified by the user. Corresponds to
  // CodeModel::Model enum defined in include/llvm/Support/CodeGen.h, plus
  // "default" for the case when the user has not explicitly specified a
  // code model.
  std::string CodeModel;
};

}  // end namespace clang

#endif
