//===--- PassUtils.swift - Utilities for optimzation passes ---------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import SIL
import OptimizerBridging

public typealias BridgedFunctionPassCtxt =
  OptimizerBridging.BridgedFunctionPassCtxt
public typealias BridgedInstructionPassCtxt =
  OptimizerBridging.BridgedInstructionPassCtxt

struct PassContext {

  fileprivate let passContext: BridgedPassContext
  
  var isSwift51RuntimeAvailable: Bool {
    // Temporarily disable optimizations based on deployment target.
    // rdar://87898692
    return false
    // PassContext_isSwift51RuntimeAvailable(passContext) != 0
  }
  
  var aliasAnalysis: AliasAnalysis {
    let bridgedAA = PassContext_getAliasAnalysis(passContext)
    return AliasAnalysis(bridged: bridgedAA)
  }
  
  var calleeAnalysis: CalleeAnalysis {
    let bridgeCA = PassContext_getCalleeAnalysis(passContext)
    return CalleeAnalysis(bridged: bridgeCA)
  }

  enum EraseMode {
    case onlyInstruction, includingDebugUses
  }

  func erase(instruction: Instruction, _ mode: EraseMode = .onlyInstruction) {
    switch mode {
      case .onlyInstruction:
        break
      case .includingDebugUses:
        for result in instruction.results {
          for use in result.uses {
            assert(use.instruction is DebugValueInst)
            PassContext_eraseInstruction(passContext, use.instruction.bridged)
          }
        }
    }
  
    if instruction is FullApplySite {
      PassContext_notifyChanges(passContext, callsChanged)
    }
    if instruction is TermInst {
      PassContext_notifyChanges(passContext, branchesChanged)
    }
    PassContext_notifyChanges(passContext, instructionsChanged)

    PassContext_eraseInstruction(passContext, instruction.bridged)
  }

  func replaceAllUses(of value: Value, with replacement: Value) {
    for use in value.uses {
      setOperand(of: use.instruction, at: use.index, to: replacement)
    }
  }

  func setOperand(of instruction: Instruction, at index : Int, to value: Value) {
    if instruction is FullApplySite && index == ApplyOperands.calleeOperandIndex {
      PassContext_notifyChanges(passContext, callsChanged)
    }
    PassContext_notifyChanges(passContext, instructionsChanged)

    SILInstruction_setOperand(instruction.bridged, index, value.bridged)
  }
}

struct FunctionPass {

  let name: String
  let runFunction: (Function, PassContext) -> ()

  public init(name: String,
              _ runFunction: @escaping (Function, PassContext) -> ()) {
    self.name = name
    self.runFunction = runFunction
  }

  func run(_ bridgedCtxt: BridgedFunctionPassCtxt) {
    let function = bridgedCtxt.function.function
    let context = PassContext(passContext: bridgedCtxt.passContext)
    runFunction(function, context)
  }
}

struct InstructionPass<InstType: Instruction> {

  let name: String
  let runFunction: (InstType, PassContext) -> ()

  public init(name: String,
              _ runFunction: @escaping (InstType, PassContext) -> ()) {
    self.name = name
    self.runFunction = runFunction
  }

  func run(_ bridgedCtxt: BridgedInstructionPassCtxt) {
    let inst = bridgedCtxt.instruction.getAs(InstType.self)
    let context = PassContext(passContext: bridgedCtxt.passContext)
    runFunction(inst, context)
  }
}

extension StackList {
  init(_ context: PassContext) {
    self.init(context: context.passContext)
  }
}

extension Builder {
  init(at insPnt: Instruction, location: Location,
       _ context: PassContext) {
    self.init(insertionPoint: insPnt, location: location,
              passContext: context.passContext)
  }

  init(at insPnt: Instruction, _ context: PassContext) {
    self.init(insertionPoint: insPnt, location: insPnt.location,
              passContext: context.passContext)
  }
}
