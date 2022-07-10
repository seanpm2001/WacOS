//===--- ValueTracking.cpp - SIL Value Tracking Analysis ------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sil-value-tracking"
#include "swift/SILOptimizer/Analysis/ValueTracking.h"
#include "swift/SILOptimizer/Analysis/SimplifyInstruction.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/InstructionUtils.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SIL/PatternMatch.h"
#include "llvm/Support/Debug.h"
using namespace swift;
using namespace swift::PatternMatch;

bool swift::isNotAliasingArgument(SILValue V,
                                  InoutAliasingAssumption isInoutAliasing) {
  auto *Arg = dyn_cast<SILArgument>(V);
  if (!Arg || !Arg->isFunctionArg())
    return false;

  return isNotAliasedIndirectParameter(Arg->getArgumentConvention(),
                                       isInoutAliasing);
}

bool swift::pointsToLocalObject(SILValue V,
                                InoutAliasingAssumption isInoutAliasing) {
  V = getUnderlyingObject(V);
  return isa<AllocationInst>(V) ||
        isNotAliasingArgument(V, isInoutAliasing);
}

/// Check if the value \p Value is known to be zero, non-zero or unknown.
IsZeroKind swift::isZeroValue(SILValue Value) {
  // Inspect integer literals.
  if (auto *L = dyn_cast<IntegerLiteralInst>(Value)) {
    if (!L->getValue())
      return IsZeroKind::Zero;
    return IsZeroKind::NotZero;
  }

  // Inspect Structs.
  switch (Value->getKind()) {
    // Bitcast of zero is zero.
    case ValueKind::UncheckedTrivialBitCastInst:
    // Extracting from a zero class returns a zero.
    case ValueKind::StructExtractInst:
      return isZeroValue(cast<SILInstruction>(Value)->getOperand(0));
    default:
      break;
  }

  // Inspect casts.
  if (auto *BI = dyn_cast<BuiltinInst>(Value)) {
    switch (BI->getBuiltinInfo().ID) {
      case BuiltinValueKind::IntToPtr:
      case BuiltinValueKind::PtrToInt:
      case BuiltinValueKind::ZExt:
        return isZeroValue(BI->getArguments()[0]);
      case BuiltinValueKind::UDiv:
      case BuiltinValueKind::SDiv: {
        if (IsZeroKind::Zero == isZeroValue(BI->getArguments()[0]))
          return IsZeroKind::Zero;
        return IsZeroKind::Unknown;
      }
      case BuiltinValueKind::Mul:
      case BuiltinValueKind::SMulOver:
      case BuiltinValueKind::UMulOver: {
        IsZeroKind LHS = isZeroValue(BI->getArguments()[0]);
        IsZeroKind RHS = isZeroValue(BI->getArguments()[1]);
        if (LHS == IsZeroKind::Zero || RHS == IsZeroKind::Zero)
          return IsZeroKind::Zero;

        return IsZeroKind::Unknown;
      }
      default:
        return IsZeroKind::Unknown;
    }
  }

  // Handle results of XXX_with_overflow arithmetic.
  if (auto *T = dyn_cast<TupleExtractInst>(Value)) {
    // Make sure we are extracting the number value and not
    // the overflow flag.
    if (T->getFieldNo() != 0)
      return IsZeroKind::Unknown;

    BuiltinInst *BI = dyn_cast<BuiltinInst>(T->getOperand());
    if (!BI)
      return IsZeroKind::Unknown;

    return isZeroValue(BI);
  }

  //Inspect allocations and pointer literals.
  if (isa<StringLiteralInst>(Value) ||
      isa<AllocationInst>(Value) ||
      isa<GlobalAddrInst>(Value))
    return IsZeroKind::NotZero;

  return IsZeroKind::Unknown;
}

/// Check if the sign bit of the value \p V is known to be:
/// set (true), not set (false) or unknown (None).
Optional<bool> swift::computeSignBit(SILValue V) {
  SILValue Value = V;
  while (true) {
    ValueBase *Def = Value;
    // Inspect integer literals.
    if (auto *L = dyn_cast<IntegerLiteralInst>(Def)) {
      if (L->getValue().isNonNegative())
        return false;
      return true;
    }

    switch (Def->getKind()) {
    // Bitcast of non-negative is non-negative
    case ValueKind::UncheckedTrivialBitCastInst:
      Value = cast<SILInstruction>(Def)->getOperand(0);
      continue;
    default:
      break;
    }

    if (auto *BI = dyn_cast<BuiltinInst>(Def)) {
      switch (BI->getBuiltinInfo().ID) {
      // Sizeof always returns non-negative results.
      case BuiltinValueKind::Sizeof:
        return false;
      // Strideof always returns non-negative results.
      case BuiltinValueKind::Strideof:
        return false;
      // StrideofNonZero always returns positive results.
      case BuiltinValueKind::StrideofNonZero:
        return false;
      // Alignof always returns non-negative results.
      case BuiltinValueKind::Alignof:
        return false;
      // Both operands to AND must have the top bit set for V to.
      case BuiltinValueKind::And: {
        // Compute the sign bit of the LHS and RHS.
        auto Left = computeSignBit(BI->getArguments()[0]);
        auto Right = computeSignBit(BI->getArguments()[1]);

        // We don't know either's sign bit so we can't
        // say anything about the result.
        if (!Left && !Right) {
          return None;
        }

        // Now we know that we were able to determine the sign bit
        // for at least one of Left/Right. Canonicalize the determined
        // sign bit on the left.
        if (Right) {
          std::swap(Left, Right);
        }

        // We know we must have at least one result and it must be on
        // the Left. If Right is still not None, then get both values
        // and AND them together.
        if (Right) {
          return Left.getValue() && Right.getValue();
        }

        // Now we know that Right is None and Left has a value. If
        // Left's value is true, then we return None as the final
        // sign bit depends on the unknown Right value.
        if (Left.getValue()) {
          return None;
        }

        // Otherwise, Left must be false and false AND'd with anything
        // else yields false.
        return false;
      }
      // At least one operand to OR must have the top bit set.
      case BuiltinValueKind::Or: {
        // Compute the sign bit of the LHS and RHS.
        auto Left = computeSignBit(BI->getArguments()[0]);
        auto Right = computeSignBit(BI->getArguments()[1]);

        // We don't know either's sign bit so we can't
        // say anything about the result.
        if (!Left && !Right) {
          return None;
        }

        // Now we know that we were able to determine the sign bit
        // for at least one of Left/Right. Canonicalize the determined
        // sign bit on the left.
        if (Right) {
          std::swap(Left, Right);
        }

        // We know we must have at least one result and it must be on
        // the Left. If Right is still not None, then get both values
        // and OR them together.
        if (Right) {
          return Left.getValue() || Right.getValue();
        }

        // Now we know that Right is None and Left has a value. If
        // Left's value is false, then we return None as the final
        // sign bit depends on the unknown Right value.
        if (!Left.getValue()) {
          return None;
        }

        // Otherwise, Left must be true and true OR'd with anything
        // else yields true.
        return true;
      }
      // Only one of the operands to XOR must have the top bit set.
      case BuiltinValueKind::Xor: {
        // Compute the sign bit of the LHS and RHS.
        auto Left = computeSignBit(BI->getArguments()[0]);
        auto Right = computeSignBit(BI->getArguments()[1]);

        // If either Left or Right is unknown then we can't say
        // anything about the sign of the final result since
        // XOR does not short-circuit.
        if (!Left || !Right) {
          return None;
        }

        // Now we know that both Left and Right must have a value.
        // For the sign of the final result to be set, only one
        // of Left or Right should be true.
        return Left.getValue() != Right.getValue();
      }
      case BuiltinValueKind::LShr: {
        // If count is provably >= 1, then top bit is not set.
        auto *ILShiftCount = dyn_cast<IntegerLiteralInst>(BI->getArguments()[1]);
        if (ILShiftCount) {
          if (ILShiftCount->getValue().isStrictlyPositive()) {
            return false;
          }
        }
        // May be top bit is not set in the value being shifted.
        Value = BI->getArguments()[0];
        continue;
      }

      // Source and target type sizes are the same.
      // S->U conversion can only succeed if
      // the sign bit of its operand is 0, i.e. it is >= 0.
      // The sign bit of a result is 0 only if the sign
      // bit of a source operand is 0.
      case BuiltinValueKind::SUCheckedConversion:
        Value = BI->getArguments()[0];
        continue;

      // Source and target type sizes are the same.
      // U->S conversion can only succeed if
      // the top bit of its operand is 0, i.e.
      // it is representable as a signed integer >=0.
      // The sign bit of a result is 0 only if the sign
      // bit of a source operand is 0.
      case BuiltinValueKind::USCheckedConversion:
        Value = BI->getArguments()[0];
        continue;

      // Sign bit of the operand is promoted.
      case BuiltinValueKind::SExt:
        Value = BI->getArguments()[0];
        continue;

      // Source type is always smaller than the target type.
      // Therefore the sign bit of a result is always 0.
      case BuiltinValueKind::ZExt:
        return false;

      // Sign bit of the operand is promoted.
      case BuiltinValueKind::SExtOrBitCast:
        Value = BI->getArguments()[0];
        continue;

      // TODO: If source type size is smaller than the target type
      // the result will be always false.
      case BuiltinValueKind::ZExtOrBitCast:
        Value = BI->getArguments()[0];
        continue;

      // Inspect casts.
      case BuiltinValueKind::IntToPtr:
      case BuiltinValueKind::PtrToInt:
        Value = BI->getArguments()[0];
        continue;
      default:
        return None;
      }
    }

    return None;
  }
}

/// Check if a checked trunc instruction can overflow.
/// Returns false if it can be proven that no overflow can happen.
/// Otherwise returns true.
static bool checkTruncOverflow(BuiltinInst *BI) {
  SILValue Left, Right;
  if (match(BI, m_CheckedTrunc(m_And(m_SILValue(Left),
                               m_SILValue(Right))))) {
    // [US]ToSCheckedTrunc(And(x, mask)) cannot overflow
    // if mask has the following properties:
    // Only the first (N-1) bits are allowed to be set, where N is the width
    // of the trunc result type.
    //
    // [US]ToUCheckedTrunc(And(x, mask)) cannot overflow
    // if mask has the following properties:
    // Only the first N bits are allowed to be set, where N is the width
    // of the trunc result type.
    if (auto BITy = BI->getType().
                        getTupleElementType(0).
                        getAs<BuiltinIntegerType>()) {
      unsigned Width = BITy->getFixedWidth();

      switch (BI->getBuiltinInfo().ID) {
      case BuiltinValueKind::SToSCheckedTrunc:
      case BuiltinValueKind::UToSCheckedTrunc:
        // If it is a trunc to a signed value
        // then sign bit should not be set to avoid overflows.
        --Width;
        break;
      default:
        break;
      }

      if (auto *ILLeft = dyn_cast<IntegerLiteralInst>(Left)) {
        APInt Value = ILLeft->getValue();
        if (Value.isIntN(Width)) {
          return false;
        }
      }

      if (auto *ILRight = dyn_cast<IntegerLiteralInst>(Right)) {
        APInt Value = ILRight->getValue();
        if (Value.isIntN(Width)) {
          return false;
        }
      }
    }
  }
  return true;
}

/// Check if execution of a given Apply instruction can result in overflows.
/// Returns true if an overflow can happen. Otherwise returns false.
bool swift::canOverflow(BuiltinInst *BI) {
  if (simplifyOverflowBuiltinInstruction(BI) != SILValue())
    return false;

  if (!checkTruncOverflow(BI))
      return false;

  // Conservatively assume that an overflow can happen
  return true;
}
