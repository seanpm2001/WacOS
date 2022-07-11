//===--- Registration.swift - register SIL classes ------------------------===//
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

private func register<T: AnyObject>(_ cl: T.Type) {
  String(describing: cl).withBridgedStringRef { nameStr in
    let metatype = unsafeBitCast(cl, to: SwiftMetatype.self)
    registerBridgedClass(nameStr, metatype)
  }
}

public func registerSILClasses() {
  register(Function.self)
  register(BasicBlock.self)
  register(GlobalVariable.self)

  // The "unimplemented" registrations must be done before all other node
  // registrations. In the order from super -> sub class.
  register(UnimplementedInstruction.self)
  register(UnimplementedSingleValueInst.self)
  register(UnimplementedRefCountingInst.self)
  register(MultipleValueInstructionResult.self)

  register(Undef.self)
  register(PlaceholderValue.self)

  register(FunctionArgument.self)
  register(BlockArgument.self)

  register(StoreInst.self)
  register(CopyAddrInst.self)
  register(EndAccessInst.self)
  register(EndBorrowInst.self)
  register(DeallocStackInst.self)
  register(CondFailInst.self)
  register(FixLifetimeInst.self)
  register(DebugValueInst.self)
  register(UnconditionalCheckedCastAddrInst.self)
  register(SetDeallocatingInst.self)
  register(DeallocRefInst.self)
  register(StrongRetainInst.self)
  register(RetainValueInst.self)
  register(StrongReleaseInst.self)
  register(ReleaseValueInst.self)
  register(DestroyValueInst.self)
  register(DestroyAddrInst.self)
  register(LoadInst.self)
  register(LoadBorrowInst.self)
  register(BuiltinInst.self)
  register(UpcastInst.self)
  register(UncheckedRefCastInst.self)
  register(RawPointerToRefInst.self)
  register(AddressToPointerInst.self)
  register(PointerToAddressInst.self)
  register(IndexAddrInst.self)
  register(InitExistentialRefInst.self)
  register(OpenExistentialRefInst.self)
  register(InitExistentialValueInst.self)
  register(OpenExistentialValueInst.self)
  register(InitExistentialAddrInst.self)
  register(OpenExistentialAddrInst.self)
  register(OpenExistentialBoxInst.self)
  register(OpenExistentialBoxValueInst.self)
  register(InitExistentialMetatypeInst.self)
  register(OpenExistentialMetatypeInst.self)
  register(ValueMetatypeInst.self)
  register(ExistentialMetatypeInst.self)
  register(GlobalAddrInst.self)
  register(GlobalValueInst.self)
  register(IntegerLiteralInst.self)
  register(TupleInst.self)
  register(TupleExtractInst.self)
  register(TupleElementAddrInst.self)
  register(StructInst.self)
  register(StructExtractInst.self)
  register(StructElementAddrInst.self)
  register(EnumInst.self)
  register(UncheckedEnumDataInst.self)
  register(RefElementAddrInst.self)
  register(RefTailAddrInst.self)
  register(UnconditionalCheckedCastInst.self)
  register(UnconditionalCheckedCastValueInst.self)
  register(ConvertFunctionInst.self)
  register(ThinToThickFunctionInst.self)
  register(ObjCExistentialMetatypeToObjectInst.self)
  register(ObjCMetatypeToObjectInst.self)
  register(ValueToBridgeObjectInst.self)
  register(BridgeObjectToRefInst.self)
  register(BeginAccessInst.self)
  register(BeginBorrowInst.self)
  register(CopyValueInst.self)
  register(EndCOWMutationInst.self)
  register(ClassifyBridgeObjectInst.self)
  register(PartialApplyInst.self)
  register(ApplyInst.self)
  register(ClassMethodInst.self)
  register(SuperMethodInst.self)
  register(ObjCMethodInst.self)
  register(ObjCSuperMethodInst.self)
  register(WitnessMethodInst.self)
  register(AllocStackInst.self)
  register(AllocRefInst.self)
  register(AllocRefDynamicInst.self)
  register(AllocBoxInst.self)
  register(AllocExistentialBoxInst.self)

  register(BeginCOWMutationInst.self)
  register(DestructureStructInst.self)
  register(DestructureTupleInst.self)
  register(BeginApplyInst.self)

  register(UnreachableInst.self)
  register(ReturnInst.self)
  register(ThrowInst.self)
  register(YieldInst.self)
  register(UnwindInst.self)
  register(TryApplyInst.self)
  register(BranchInst.self)
  register(CondBranchInst.self)
  register(SwitchValueInst.self)
  register(SwitchEnumInst.self)
  register(SwitchEnumAddrInst.self)
  register(DynamicMethodBranchInst.self)
  register(AwaitAsyncContinuationInst.self)
  register(CheckedCastBranchInst.self)
  register(CheckedCastAddrBranchInst.self)
  register(CheckedCastValueBranchInst.self)
}
