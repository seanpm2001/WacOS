//===--- Instruction.swift - Defines the Instruction classes --------------===//
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

import SILBridging

//===----------------------------------------------------------------------===//
//                       Instruction base classes
//===----------------------------------------------------------------------===//

public class Instruction : ListNode, CustomStringConvertible, Hashable {
  final public var next: Instruction? {
    SILInstruction_next(bridged).instruction
  }

  final public var previous: Instruction? {
    SILInstruction_previous(bridged).instruction
  }

  final public var block: BasicBlock {
    SILInstruction_getParent(bridged).block
  }

  final public var description: String {
    var s = SILNode_debugDescription(bridgedNode)
    return String(cString: s.c_str())
  }
  
  final public var operands: OperandArray {
    return OperandArray(opArray: SILInstruction_getOperands(bridged))
  }
  
  fileprivate var resultCount: Int { 0 }
  fileprivate func getResult(index: Int) -> Value { fatalError() }

  public struct Results : RandomAccessCollection {
    fileprivate let inst: Instruction
    fileprivate let numResults: Int

    public var startIndex: Int { 0 }
    public var endIndex: Int { numResults }
    public subscript(_ index: Int) -> Value { inst.getResult(index: index) }
  }

  final public var results: Results {
    Results(inst: self, numResults: resultCount)
  }

  final public var location: Location {
    return Location(bridgedLocation: SILInstruction_getLocation(bridged))
  }

  public var mayTrap: Bool { false }

  final public var mayHaveSideEffects: Bool {
    return mayTrap || mayWriteToMemory
  }

  final public var mayReadFromMemory: Bool {
    switch SILInstruction_getMemBehavior(bridged) {
      case MayReadBehavior, MayReadWriteBehavior, MayHaveSideEffectsBehavior:
        return true
      default:
        return false
    }
  }

  final public var mayWriteToMemory: Bool {
    switch SILInstruction_getMemBehavior(bridged) {
      case MayWriteBehavior, MayReadWriteBehavior, MayHaveSideEffectsBehavior:
        return true
      default:
        return false
    }
  }

  final public var mayReadOrWriteMemory: Bool {
    switch SILInstruction_getMemBehavior(bridged) {
      case MayReadBehavior, MayWriteBehavior, MayReadWriteBehavior,
           MayHaveSideEffectsBehavior:
        return true
      default:
        return false
    }
  }

  public static func ==(lhs: Instruction, rhs: Instruction) -> Bool {
    lhs === rhs
  }

  public func hash(into hasher: inout Hasher) {
    hasher.combine(ObjectIdentifier(self))
  }

  public var bridged: BridgedInstruction {
    BridgedInstruction(obj: SwiftObject(self))
  }
  var bridgedNode: BridgedNode { BridgedNode(obj: SwiftObject(self)) }
}

extension BridgedInstruction {
  public var instruction: Instruction { obj.getAs(Instruction.self) }
  public func getAs<T: Instruction>(_ instType: T.Type) -> T { obj.getAs(T.self) }
}

extension OptionalBridgedInstruction {
  var instruction: Instruction? { obj.getAs(Instruction.self) }
}

public class SingleValueInstruction : Instruction, Value {
  final public var definingInstruction: Instruction? { self }

  fileprivate final override var resultCount: Int { 1 }
  fileprivate final override func getResult(index: Int) -> Value { self }
}

public final class MultipleValueInstructionResult : Value {
  final public var description: String {
    var s = SILNode_debugDescription(bridgedNode)
    return String(cString: s.c_str())
  }

  public var instruction: Instruction {
    MultiValueInstResult_getParent(bridged).instruction
  }

  public var definingInstruction: Instruction? { instruction }

  var bridged: BridgedMultiValueResult {
    BridgedMultiValueResult(obj: SwiftObject(self))
  }
  var bridgedNode: BridgedNode { BridgedNode(obj: SwiftObject(self)) }
}

extension BridgedMultiValueResult {
  var result: MultipleValueInstructionResult {
    obj.getAs(MultipleValueInstructionResult.self)
  }
}

public class MultipleValueInstruction : Instruction {
  fileprivate final override var resultCount: Int {
    return MultipleValueInstruction_getNumResults(bridged)
  }
  fileprivate final override func getResult(index: Int) -> Value {
    MultipleValueInstruction_getResult(bridged, index).result
  }
}

/// Instructions, which have a single operand.
public protocol UnaryInstruction : AnyObject {
  var operands: OperandArray { get }
  var operand: Value { get }
}

extension UnaryInstruction {
  public var operand: Value { operands[0].value }
}

//===----------------------------------------------------------------------===//
//                             no-value instructions
//===----------------------------------------------------------------------===//

/// Used for all non-value instructions which are not implemented here, yet.
/// See registerBridgedClass() in SILBridgingUtils.cpp.
final public class UnimplementedInstruction : Instruction {
}

final public class StoreInst : Instruction {
  public var sourceOperand: Operand { return operands[0] }
  public var destinationOperand: Operand { return operands[1] }
  public var source: Value { return sourceOperand.value }
  public var destination: Value { return destinationOperand.value }
  
  // must match with enum class StoreOwnershipQualifier
  public enum StoreOwnership: Int {
    case unqualified = 0, initialize = 1, assign = 2, trivial = 3
  }
  public var destinationOwnership: StoreOwnership {
    StoreOwnership(rawValue: StoreInst_getStoreOwnership(bridged))!
  }
}

final public class CopyAddrInst : Instruction {
  public var sourceOperand: Operand { return operands[0] }
  public var destinationOperand: Operand { return operands[1] }
  public var source: Value { return sourceOperand.value }
  public var destination: Value { return destinationOperand.value }
}

final public class EndAccessInst : Instruction, UnaryInstruction {
  public var beginAccess: BeginAccessInst {
    return operand as! BeginAccessInst
  }
}

final public class EndBorrowInst : Instruction, UnaryInstruction {}

final public class DeallocStackInst : Instruction, UnaryInstruction {
  public var allocstack: AllocStackInst {
    return operand as! AllocStackInst
  }
}

final public class CondFailInst : Instruction, UnaryInstruction {
  public override var mayTrap: Bool { true }

  public var message: String { CondFailInst_getMessage(bridged).string }
}

final public class FixLifetimeInst : Instruction, UnaryInstruction {}

final public class DebugValueInst : Instruction, UnaryInstruction {}

final public class UnconditionalCheckedCastAddrInst : Instruction {
  public override var mayTrap: Bool { true }
}

final public class SetDeallocatingInst : Instruction, UnaryInstruction {}

final public class DeallocRefInst : Instruction, UnaryInstruction {}

public class RefCountingInst : Instruction, UnaryInstruction {}

final public class StrongRetainInst : RefCountingInst {
}

final public class RetainValueInst : RefCountingInst {
}

final public class StrongReleaseInst : RefCountingInst {
}

final public class ReleaseValueInst : RefCountingInst {
}

final public class DestroyValueInst : Instruction, UnaryInstruction {}

final public class DestroyAddrInst : Instruction, UnaryInstruction {}

final public class UnimplementedRefCountingInst : RefCountingInst {}

//===----------------------------------------------------------------------===//
//                           single-value instructions
//===----------------------------------------------------------------------===//

/// Used for all SingleValueInstructions which are not implemented here, yet.
/// See registerBridgedClass() in SILBridgingUtils.cpp.
final public class UnimplementedSingleValueInst : SingleValueInstruction {
}

final public class LoadInst : SingleValueInstruction, UnaryInstruction {}

final public class LoadBorrowInst : SingleValueInstruction, UnaryInstruction {}

final public class BuiltinInst : SingleValueInstruction {}

final public class UpcastInst : SingleValueInstruction, UnaryInstruction {}

final public
class UncheckedRefCastInst : SingleValueInstruction, UnaryInstruction {}

final public
class RawPointerToRefInst : SingleValueInstruction, UnaryInstruction {}

final public
class AddressToPointerInst : SingleValueInstruction, UnaryInstruction {}

final public
class PointerToAddressInst : SingleValueInstruction, UnaryInstruction {}

final public
class IndexAddrInst : SingleValueInstruction {}

final public
class InitExistentialRefInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialRefInst : SingleValueInstruction, UnaryInstruction {}

final public
class InitExistentialValueInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialValueInst : SingleValueInstruction, UnaryInstruction {}

final public
class InitExistentialAddrInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialAddrInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialBoxInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialBoxValueInst : SingleValueInstruction, UnaryInstruction {}

final public
class InitExistentialMetatypeInst : SingleValueInstruction, UnaryInstruction {}

final public
class OpenExistentialMetatypeInst : SingleValueInstruction, UnaryInstruction {}

final public
class ValueMetatypeInst : SingleValueInstruction, UnaryInstruction {}

final public
class ExistentialMetatypeInst : SingleValueInstruction, UnaryInstruction {}

public class GlobalAccessInst : SingleValueInstruction {
  final public var global: GlobalVariable {
    GlobalAccessInst_getGlobal(bridged).globalVar
  }
}

final public class GlobalAddrInst : GlobalAccessInst {}

final public class GlobalValueInst : GlobalAccessInst {}

final public class IntegerLiteralInst : SingleValueInstruction {}

final public class TupleInst : SingleValueInstruction {
}

final public class TupleExtractInst : SingleValueInstruction, UnaryInstruction {
  public var fieldIndex: Int { TupleExtractInst_fieldIndex(bridged) }
}

final public
class TupleElementAddrInst : SingleValueInstruction, UnaryInstruction {
  public var fieldIndex: Int { TupleElementAddrInst_fieldIndex(bridged) }
}

final public class StructInst : SingleValueInstruction {
}

final public class StructExtractInst : SingleValueInstruction, UnaryInstruction {
  public var fieldIndex: Int { StructExtractInst_fieldIndex(bridged) }
}

final public
class StructElementAddrInst : SingleValueInstruction, UnaryInstruction {
  public var fieldIndex: Int { StructElementAddrInst_fieldIndex(bridged) }
}

final public class EnumInst : SingleValueInstruction {
  public var caseIndex: Int { EnumInst_caseIndex(bridged) }
  
  public var operand: Value? { operands.first?.value }
}

final public
class UncheckedEnumDataInst : SingleValueInstruction, UnaryInstruction {
  public var caseIndex: Int { UncheckedEnumDataInst_caseIndex(bridged) }
}

final public class RefElementAddrInst : SingleValueInstruction, UnaryInstruction {
  public var fieldIndex: Int { RefElementAddrInst_fieldIndex(bridged) }
}

final public class RefTailAddrInst : SingleValueInstruction, UnaryInstruction {}

final public
class UnconditionalCheckedCastInst : SingleValueInstruction, UnaryInstruction {
  public override var mayTrap: Bool { true }
}

final public
class UnconditionalCheckedCastValueInst : SingleValueInstruction,
                                          UnaryInstruction {
  public override var mayTrap: Bool { true }
}

final public
class ConvertFunctionInst : SingleValueInstruction, UnaryInstruction {}

final public
class ThinToThickFunctionInst : SingleValueInstruction, UnaryInstruction {}

final public
class ObjCExistentialMetatypeToObjectInst : SingleValueInstruction,
                                            UnaryInstruction {}

final public
class ObjCMetatypeToObjectInst : SingleValueInstruction, UnaryInstruction {}

final public
class ValueToBridgeObjectInst : SingleValueInstruction, UnaryInstruction {}

final public class BridgeObjectToRefInst : SingleValueInstruction,
                                           UnaryInstruction {}

final public class BeginAccessInst : SingleValueInstruction, UnaryInstruction {}

final public class BeginBorrowInst : SingleValueInstruction, UnaryInstruction {}

final public class CopyValueInst : SingleValueInstruction, UnaryInstruction {}

final public class EndCOWMutationInst : SingleValueInstruction, UnaryInstruction {}

final public
class ClassifyBridgeObjectInst : SingleValueInstruction, UnaryInstruction {}

final public class PartialApplyInst : SingleValueInstruction, ApplySite {
  public var numArguments: Int { PartialApplyInst_numArguments(bridged) }
}

final public class ApplyInst : SingleValueInstruction, FullApplySite {
  public var numArguments: Int { ApplyInst_numArguments(bridged) }
  
  public var singleDirectResult: Value? { self }
}

final public class ClassMethodInst : SingleValueInstruction, UnaryInstruction {}

final public class SuperMethodInst : SingleValueInstruction, UnaryInstruction {}

final public class ObjCMethodInst : SingleValueInstruction, UnaryInstruction {}

final public class ObjCSuperMethodInst : SingleValueInstruction, UnaryInstruction {}

final public class WitnessMethodInst : SingleValueInstruction {}


//===----------------------------------------------------------------------===//
//                      single-value allocation instructions
//===----------------------------------------------------------------------===//

public protocol Allocation : AnyObject { }

final public class AllocStackInst : SingleValueInstruction, Allocation {
}

final public class AllocRefInst : SingleValueInstruction, Allocation {
}

final public class AllocRefDynamicInst : SingleValueInstruction, Allocation {
}

final public class AllocBoxInst : SingleValueInstruction, Allocation {
}

final public class AllocExistentialBoxInst : SingleValueInstruction, Allocation {
}

//===----------------------------------------------------------------------===//
//                            multi-value instructions
//===----------------------------------------------------------------------===//

final public class BeginCOWMutationInst : MultipleValueInstruction,
                                          UnaryInstruction {
  public var uniquenessResult: Value { return getResult(index: 0) }
  public var bufferResult: Value { return getResult(index: 1) }
}

final public class DestructureStructInst : MultipleValueInstruction {
}

final public class DestructureTupleInst : MultipleValueInstruction {
}

final public class BeginApplyInst : MultipleValueInstruction, FullApplySite {
  public var numArguments: Int { BeginApplyInst_numArguments(bridged) }
  
  public var singleDirectResult: Value? { nil }
}

//===----------------------------------------------------------------------===//
//                            terminator instructions
//===----------------------------------------------------------------------===//

public class TermInst : Instruction {
  final public var successors: SuccessorArray {
    SuccessorArray(succArray: TermInst_getSuccessors(bridged))
  }
}

final public class UnreachableInst : TermInst {
}

final public class ReturnInst : TermInst, UnaryInstruction {
}

final public class ThrowInst : TermInst, UnaryInstruction {
}

final public class YieldInst : TermInst {
}

final public class UnwindInst : TermInst {
}

final public class TryApplyInst : TermInst, FullApplySite {
  public var numArguments: Int { TryApplyInst_numArguments(bridged) }
  
  public var normalBlock: BasicBlock { successors[0] }
  public var errorBlock: BasicBlock { successors[1] }
  
  public var singleDirectResult: Value? { normalBlock.arguments[0] }
}

final public class BranchInst : TermInst {
  public var targetBlock: BasicBlock { BranchInst_getTargetBlock(bridged).block }
  
  public func getArgument(for operand: Operand) -> Argument {
    return targetBlock.arguments[operand.index]
  }
}

final public class CondBranchInst : TermInst {
}

final public class SwitchValueInst : TermInst {
}

final public class SwitchEnumInst : TermInst {

  public var enumOp: Value { operands[0].value }

  public struct CaseIndexArray : RandomAccessCollection {
    fileprivate let switchEnum: SwitchEnumInst

    public var startIndex: Int { return 0 }
    public var endIndex: Int { SwitchEnumInst_getNumCases(switchEnum.bridged) }

    public subscript(_ index: Int) -> Int {
      SwitchEnumInst_getCaseIndex(switchEnum.bridged, index)
    }
  }

  var caseIndices: CaseIndexArray { CaseIndexArray(switchEnum: self) }

  var cases: Zip2Sequence<CaseIndexArray, SuccessorArray> {
    zip(caseIndices, successors)
  }

  // This does not handle the special case where the default covers exactly
  // the "missing" case.
  public func getUniqueSuccessor(forCaseIndex: Int) -> BasicBlock? {
    cases.first(where: { $0.0 == forCaseIndex })?.1
  }

  // This does not handle the special case where the default covers exactly
  // the "missing" case.
  public func getUniqueCase(forSuccessor: BasicBlock) -> Int? {
    cases.first(where: { $0.1 == forSuccessor })?.0
  }
}

final public class SwitchEnumAddrInst : TermInst {
}

final public class DynamicMethodBranchInst : TermInst {
}

final public class AwaitAsyncContinuationInst : TermInst, UnaryInstruction {
}

final public class CheckedCastBranchInst : TermInst, UnaryInstruction {
}

final public class CheckedCastAddrBranchInst : TermInst, UnaryInstruction {
}

final public class CheckedCastValueBranchInst : TermInst, UnaryInstruction {
}

