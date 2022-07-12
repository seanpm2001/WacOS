//===--- Action.h - Abstract compilation steps ------------------*- C++ -*-===//
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

#ifndef SWIFT_DRIVER_ACTION_H
#define SWIFT_DRIVER_ACTION_H

#include "swift/Basic/LLVM.h"
#include "swift/Driver/Types.h"
#include "swift/Driver/Util.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Chrono.h"

namespace llvm {
namespace opt {
  class Arg;
}
}

namespace swift {
namespace driver {
  class Action;

class Action {
public:
  typedef ArrayRef<const Action *>::size_type size_type;
  typedef ArrayRef<const Action *>::iterator iterator;
  typedef ArrayRef<const Action *>::const_iterator const_iterator;

  enum ActionClass {
    Input = 0,
    CompileJob,
    InterpretJob,
    BackendJob,
    MergeModuleJob,
    ModuleWrapJob,
    AutolinkExtractJob,
    REPLJob,
    LinkJob,
    GenerateDSYMJob,
    VerifyDebugInfoJob,
    GeneratePCHJob,

    JobFirst=CompileJob,
    JobLast=GeneratePCHJob
  };

  static const char *getClassName(ActionClass AC);

private:
  unsigned Kind : 4;
  unsigned Type : 28;

  friend class Compilation;
  /// Actions must be created through Compilation::createAction.
  void *operator new(size_t size) { return ::operator new(size); };

protected:
  Action(ActionClass Kind, types::ID Type)
    : Kind(Kind), Type(Type) {
    assert(Kind == getKind() && "not enough bits");
    assert(Type == getType() && "not enough bits");
  }

public:
  virtual ~Action() = default;

  const char *getClassName() const { return Action::getClassName(getKind()); }

  ActionClass getKind() const { return static_cast<ActionClass>(Kind); }
  types::ID getType() const { return static_cast<types::ID>(Type); }
};

class InputAction : public Action {
  virtual void anchor();
  const llvm::opt::Arg &Input;

public:
  InputAction(const llvm::opt::Arg &Input, types::ID Type)
      : Action(Action::Input, Type), Input(Input) {}
  const llvm::opt::Arg &getInputArg() const { return Input; }

  static bool classof(const Action *A) {
    return A->getKind() == Action::Input;
  }
};

class JobAction : public Action {
  TinyPtrVector<const Action *> Inputs;
  virtual void anchor();
protected:
  JobAction(ActionClass Kind, ArrayRef<const Action *> Inputs, types::ID Type)
      : Action(Kind, Type), Inputs(Inputs) {}

public:
  ArrayRef<const Action *> getInputs() const { return Inputs; }
  void addInput(const Action *Input) { Inputs.push_back(Input); }

  size_type size() const { return Inputs.size(); }

  iterator begin() { return Inputs.begin(); }
  iterator end() { return Inputs.end(); }
  const_iterator begin() const { return Inputs.begin(); }
  const_iterator end() const { return Inputs.end(); }

  // Returns the index of the Input action's output file which is used as
  // (single) input to this action. Most actions produce only a single output
  // file, so we return 0 by default.
  virtual int getInputIndex() const { return 0; }

  static bool classof(const Action *A) {
    return (A->getKind() >= ActionClass::JobFirst &&
            A->getKind() <= ActionClass::JobLast);
  }
};

class CompileJobAction : public JobAction {
public:
  struct InputInfo {
    enum Status {
      UpToDate,
      NeedsCascadingBuild,
      NeedsNonCascadingBuild,
      NewlyAdded
    };
    Status status = UpToDate;
    llvm::sys::TimePoint<> previousModTime;

    InputInfo() = default;
    InputInfo(Status stat, llvm::sys::TimePoint<> time)
        : status(stat), previousModTime(time) {}

    static InputInfo makeNewlyAdded() {
      return InputInfo(Status::NewlyAdded, llvm::sys::TimePoint<>::max());
    }
  };

private:
  virtual void anchor();
  InputInfo inputInfo;

public:
  CompileJobAction(types::ID OutputType)
      : JobAction(Action::CompileJob, None, OutputType), inputInfo() {}

  CompileJobAction(Action *Input, types::ID OutputType,
                   InputInfo info)
      : JobAction(Action::CompileJob, Input, OutputType),
        inputInfo(info) {}

  InputInfo getInputInfo() const {
    return inputInfo;
  }

  static bool classof(const Action *A) {
    return A->getKind() == Action::CompileJob;
  }
};

class InterpretJobAction : public JobAction {
private:
  virtual void anchor();

public:
  explicit InterpretJobAction()
      : JobAction(Action::InterpretJob, llvm::None, types::TY_Nothing) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::InterpretJob;
  }
};

class BackendJobAction : public JobAction {
private:
  virtual void anchor();
  
  // In case of multi-threaded compilation, the compile-action produces multiple
  // output bitcode-files. For each bitcode-file a BackendJobAction is created.
  // This index specifies which of the files to select for the input.
  int InputIndex;
public:
  BackendJobAction(const Action *Input, types::ID OutputType, int InputIndex)
      : JobAction(Action::BackendJob, Input, OutputType),
        InputIndex(InputIndex) {}
  static bool classof(const Action *A) {
    return A->getKind() == Action::BackendJob;
  }
  
  virtual int getInputIndex() const { return InputIndex; }
};

class REPLJobAction : public JobAction {
public:
  enum class Mode {
    Integrated,
    PreferLLDB,
    RequireLLDB
  };
private:
  virtual void anchor();
  Mode RequestedMode;
public:
  REPLJobAction(Mode mode)
    : JobAction(Action::REPLJob, llvm::None, types::TY_Nothing),
      RequestedMode(mode) {}

  Mode getRequestedMode() const { return RequestedMode; }

  static bool classof(const Action *A) {
    return A->getKind() == Action::REPLJob;
  }
};

class MergeModuleJobAction : public JobAction {
  virtual void anchor();
public:
  MergeModuleJobAction(ArrayRef<const Action *> Inputs)
      : JobAction(Action::MergeModuleJob, Inputs, types::TY_SwiftModuleFile) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::MergeModuleJob;
  }
};

class ModuleWrapJobAction : public JobAction {
  virtual void anchor();
public:
  ModuleWrapJobAction(ArrayRef<const Action *> Inputs)
      : JobAction(Action::ModuleWrapJob, Inputs, types::TY_Object) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::ModuleWrapJob;
  }
};

class AutolinkExtractJobAction : public JobAction {
  virtual void anchor();
public:
  AutolinkExtractJobAction(ArrayRef<const Action *> Inputs)
      : JobAction(Action::AutolinkExtractJob, Inputs, types::TY_AutolinkFile) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::AutolinkExtractJob;
  }
};

class GenerateDSYMJobAction : public JobAction {
  virtual void anchor();
public:
  explicit GenerateDSYMJobAction(const Action *Input)
    : JobAction(Action::GenerateDSYMJob, Input, types::TY_dSYM) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::GenerateDSYMJob;
  }
};

class VerifyDebugInfoJobAction : public JobAction {
  virtual void anchor();
public:
  explicit VerifyDebugInfoJobAction(const Action *Input)
    : JobAction(Action::VerifyDebugInfoJob, Input, types::TY_Nothing) {}

  static bool classof(const Action *A) {
    return A->getKind() == Action::VerifyDebugInfoJob;
  }
};
  
class GeneratePCHJobAction : public JobAction {
  std::string PersistentPCHDir;

  virtual void anchor();
public:
  GeneratePCHJobAction(const Action *Input, StringRef persistentPCHDir)
    : JobAction(Action::GeneratePCHJob, Input,
                persistentPCHDir.empty() ? types::TY_PCH : types::TY_Nothing),
      PersistentPCHDir(persistentPCHDir) {}

  bool isPersistentPCH() const { return !PersistentPCHDir.empty(); }
  StringRef getPersistentPCHDir() const { return PersistentPCHDir; }

  static bool classof(const Action *A) {
    return A->getKind() == Action::GeneratePCHJob;
  }
};

class LinkJobAction : public JobAction {
  virtual void anchor();
  LinkKind Kind;

public:
  LinkJobAction(ArrayRef<const Action *> Inputs, LinkKind K)
      : JobAction(Action::LinkJob, Inputs, types::TY_Image), Kind(K) {
    assert(Kind != LinkKind::None);
  }

  LinkKind getKind() const { return Kind; }

  static bool classof(const Action *A) {
    return A->getKind() == Action::LinkJob;
  }
};

} // end namespace driver
} // end namespace swift

#endif
