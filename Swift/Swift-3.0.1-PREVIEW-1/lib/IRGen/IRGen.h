//===--- IRGen.h - Common Declarations for IR Generation --------*- C++ -*-===//
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
//
// This file defines some types that are generically useful in IR
// Generation.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_IRGEN_H
#define SWIFT_IRGEN_IRGEN_H

#include "llvm/Support/DataTypes.h"
#include "clang/AST/CharUnits.h"
#include "swift/AST/ResilienceExpansion.h"
#include "swift/SIL/AbstractionPattern.h"
#include <cassert>

namespace llvm {
  class Value;
}

namespace swift {
  class CanType;
  class ClusteredBitVector;
  enum ForDefinition_t : bool;
  
namespace irgen {
  using Lowering::AbstractionPattern;

/// In IRGen, we use Swift's ClusteredBitVector data structure to
/// store vectors of spare bits.
using SpareBitVector = ClusteredBitVector;

class Size;

enum IsPOD_t : bool { IsNotPOD, IsPOD };
inline IsPOD_t operator&(IsPOD_t l, IsPOD_t r) {
  return IsPOD_t(unsigned(l) & unsigned(r));
}
inline IsPOD_t &operator&=(IsPOD_t &l, IsPOD_t r) {
  return (l = (l & r));
}

enum IsFixedSize_t : bool { IsNotFixedSize, IsFixedSize };
inline IsFixedSize_t operator&(IsFixedSize_t l, IsFixedSize_t r) {
  return IsFixedSize_t(unsigned(l) & unsigned(r));
}
inline IsFixedSize_t &operator&=(IsFixedSize_t &l, IsFixedSize_t r) {
  return (l = (l & r));
}

enum IsLoadable_t : bool { IsNotLoadable, IsLoadable };
inline IsLoadable_t operator&(IsLoadable_t l, IsLoadable_t r) {
  return IsLoadable_t(unsigned(l) & unsigned(r));
}
inline IsLoadable_t &operator&=(IsLoadable_t &l, IsLoadable_t r) {
  return (l = (l & r));
}

enum IsBitwiseTakable_t : bool { IsNotBitwiseTakable, IsBitwiseTakable };
inline IsBitwiseTakable_t operator&(IsBitwiseTakable_t l, IsBitwiseTakable_t r) {
  return IsBitwiseTakable_t(unsigned(l) & unsigned(r));
}
inline IsBitwiseTakable_t &operator&=(IsBitwiseTakable_t &l, IsBitwiseTakable_t r) {
  return (l = (l & r));
}

/// The kind of reference counting implementation a heap object uses.
enum class ReferenceCounting : unsigned char {
  /// The object uses native Swift reference counting.
  Native,
  
  /// The object uses ObjC reference counting.
  ///
  /// When ObjC interop is enabled, native Swift class objects are also ObjC
  /// reference counting compatible. Swift non-class heap objects are never
  /// ObjC reference counting compatible.
  ///
  /// Blocks are always ObjC reference counting compatible.
  ObjC,
  
  /// The object uses _Block_copy/_Block_release reference counting.
  ///
  /// This is a strict subset of ObjC; all blocks are also ObjC reference
  /// counting compatible. The block is assumed to have already been moved to
  /// the heap so that _Block_copy returns the same object back.
  Block,
  
  /// The object has an unknown reference counting implementation.
  ///
  /// This uses maximally-compatible reference counting entry points in the
  /// runtime.
  Unknown,
  
  /// Cases prior to this one are binary-compatible with Unknown reference
  /// counting.
  LastUnknownCompatible = Unknown,

  /// The object has an unknown reference counting implementation and
  /// the reference value may contain extra bits that need to be masked.
  ///
  /// This uses maximally-compatible reference counting entry points in the
  /// runtime, with a masking layer on top. A bit inside the pointer is used
  /// to signal native Swift refcounting.
  Bridge,
  
  /// The object uses ErrorType's reference counting entry points.
  Error,
};

/// The atomicity of a reference counting operation to be used.
enum class Atomicity : bool {
  /// Atomic reference counting operations should be used.
  Atomic,
  /// Non-atomic reference counting operations can be used.
  NonAtomic,
};

/// Whether or not an object should be emitted on the heap.
enum OnHeap_t : unsigned char {
  NotOnHeap,
  OnHeap
};

/// Whether a function requires extra data.
enum class ExtraData : unsigned char {
  /// The function requires no extra data.
  None,

  /// The function requires a retainable object pointer of extra data.
  Retainable,
  
  /// The function takes its block object as extra data.
  Block,
  
  Last_ExtraData = Block
};

/// Given that we have metadata for a type, is it for exactly the
/// specified type, or might it be a subtype?
enum IsExact_t : bool {
  IsInexact = false,
  IsExact = true
};

/// Ways in which an object can be referenced.
///
/// See the comment in RelativePointer.h.

enum class SymbolReferenceKind : unsigned char {
  /// An absolute reference to the object, i.e. an ordinary pointer.
  ///
  /// Generally well-suited for when C compatibility is a must, dynamic
  /// initialization is the dominant case, or the runtime performance
  /// of accesses is an overriding concern.
  Absolute,

  /// A direct relative reference to the object, i.e. the offset of the
  /// object from the address at which the relative reference is stored.
  ///
  /// Generally well-suited for when the reference is always statically
  /// initialized and will always refer to another object within the
  /// same linkage unit.
  Relative_Direct,

  /// A direct relative reference that is guaranteed to be as wide as a
  /// pointer.
  ///
  /// Generally well-suited for when the reference may be dynamically
  /// initialized, but will only refer to objects within the linkage unit
  /// when statically initialized.
  Far_Relative_Direct,

  /// A relative reference that may be indirect: the direct reference is
  /// either directly to the object or to a variable holding an absolute
  /// reference to the object.
  ///
  /// The low bit of the target offset is used to mark an indirect reference,
  /// and so the low bit of the target address must be zero.  This means that,
  /// in general, it is not possible to form this kind of reference to a
  /// function (due to the THUMB bit) or unaligned data (such as a C string).
  ///
  /// Generally well-suited for when the reference is always statically
  /// initialized but may refer to something outside of the linkage unit.
  Relative_Indirectable,

  /// An indirectable reference to the object; guaranteed to be as wide
  /// as a pointer.
  ///
  /// Generally well-suited for when the reference may be dynamically
  /// initialized but may also statically refer outside of the linkage unit.
  Far_Relative_Indirectable,
};

/// Destructor variants.
enum class DestructorKind : uint8_t {
  /// A deallocating destructor destroys the object and deallocates
  /// the memory associated with it.
  Deallocating,

  /// A destroying destructor destroys the object but does not
  /// deallocate the memory associated with it.
  Destroying
};

/// Constructor variants.
enum class ConstructorKind : uint8_t {
  /// An allocating constructor allocates an object and initializes it.
  Allocating,

  /// An initializing constructor just initializes an existing object.
  Initializing
};

/// An alignment value, in eight-bit units.
class Alignment {
public:
  typedef uint32_t int_type;

  Alignment() : Value(0) {}
  explicit Alignment(int_type Value) : Value(Value) {}

  int_type getValue() const { return Value; }
  int_type getMaskValue() const { return Value - 1; }

  bool isOne() const { return Value == 1; }
  bool isZero() const { return Value == 0; }

  Alignment alignmentAtOffset(Size S) const;
  Size asSize() const;

  unsigned log2() const {
    return llvm::Log2_64(Value);
  }

  explicit operator bool() const { return Value != 0; }

  friend bool operator< (Alignment L, Alignment R){ return L.Value <  R.Value; }
  friend bool operator<=(Alignment L, Alignment R){ return L.Value <= R.Value; }
  friend bool operator> (Alignment L, Alignment R){ return L.Value >  R.Value; }
  friend bool operator>=(Alignment L, Alignment R){ return L.Value >= R.Value; }
  friend bool operator==(Alignment L, Alignment R){ return L.Value == R.Value; }
  friend bool operator!=(Alignment L, Alignment R){ return L.Value != R.Value; }

private:
  int_type Value;
};

/// A size value, in eight-bit units.
class Size {
public:
  typedef uint64_t int_type;

  constexpr Size() : Value(0) {}
  explicit constexpr Size(int_type Value) : Value(Value) {}

  /// An "invalid" size, equal to the maximum possible size.
  static constexpr Size invalid() { return Size(~int_type(0)); }

  /// Is this the "invalid" size value?
  bool isInvalid() const { return *this == Size::invalid(); }

  int_type getValue() const { return Value; }
  
  int_type getValueInBits() const { return Value * 8; }

  bool isZero() const { return Value == 0; }

  friend Size operator+(Size L, Size R) {
    return Size(L.Value + R.Value);
  }
  friend Size &operator+=(Size &L, Size R) {
    L.Value += R.Value;
    return L;
  }

  friend Size operator-(Size L, Size R) {
    return Size(L.Value - R.Value);
  }
  friend Size &operator-=(Size &L, Size R) {
    L.Value -= R.Value;
    return L;
  }

  friend Size operator*(Size L, int_type R) {
    return Size(L.Value * R);
  }
  friend Size operator*(int_type L, Size R) {
    return Size(L * R.Value);
  }
  friend Size &operator*=(Size &L, int_type R) {
    L.Value *= R;
    return L;
  }

  friend int_type operator/(Size L, Size R) {
    return L.Value / R.Value;
  }

  explicit operator bool() const { return Value != 0; }

  Size roundUpToAlignment(Alignment align) const {
    int_type value = getValue() + align.getValue() - 1;
    return Size(value & ~int_type(align.getValue() - 1));
  }

  bool isPowerOf2() const {
    auto value = getValue();
    return ((value & -value) == value);
  }

  bool isMultipleOf(Size other) const {
    return (Value % other.Value) == 0;
  }

  unsigned log2() const {
    return llvm::Log2_64(Value);
  }

  clang::CharUnits asCharUnits() const {
    return clang::CharUnits::fromQuantity(getValue());
  }

  friend bool operator< (Size L, Size R) { return L.Value <  R.Value; }
  friend bool operator<=(Size L, Size R) { return L.Value <= R.Value; }
  friend bool operator> (Size L, Size R) { return L.Value >  R.Value; }
  friend bool operator>=(Size L, Size R) { return L.Value >= R.Value; }
  friend bool operator==(Size L, Size R) { return L.Value == R.Value; }
  friend bool operator!=(Size L, Size R) { return L.Value != R.Value; }

  friend Size operator%(Size L, Alignment R) {
    return Size(L.Value % R.getValue());
  }

private:
  int_type Value;
};

/// Compute the alignment of a pointer which points S bytes after a
/// pointer with this alignment.
inline Alignment Alignment::alignmentAtOffset(Size S) const {
  assert(getValue() && "called on object with zero alignment");

  // If the offset is zero, use the original alignment.
  Size::int_type V = S.getValue();
  if (!V) return *this;

  // Find the offset's largest power-of-two factor.
  V = V & -V;

  // The alignment at the offset is then the min of the two values.
  if (V < getValue())
    return Alignment(static_cast<Alignment::int_type>(V));
  return *this;
}

/// Get this alignment asx a Size value.
inline Size Alignment::asSize() const {
  return Size(getValue());
}

} // end namespace irgen
} // end namespace swift

#endif
