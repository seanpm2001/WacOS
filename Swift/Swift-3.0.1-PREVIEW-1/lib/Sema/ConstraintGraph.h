//===--- ConstraintGraph.h - Constraint Graph -------------------*- C++ -*-===//
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
// This file defines the \c ConstraintGraph class, which describes the
// relationships among the type variables within a constraint system.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_SEMA_CONSTRAINT_GRAPH_H
#define SWIFT_SEMA_CONSTRAINT_GRAPH_H

#include "swift/Basic/LLVM.h"
#include "swift/AST/Identifier.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <functional>
#include <utility>

namespace swift {

class Type;
class TypeBase;
class TypeVariableType;

namespace constraints {

class Constraint;
class ConstraintGraph;
class ConstraintGraphScope;
class ConstraintSystem;

/// A single node in the constraint graph, which represents a type variable.
class ConstraintGraphNode {
  /// Describes information about an adjacency between two type variables.
  struct Adjacency {
    /// Index into the vector of adjacent type variables, \c Adjacencies.
    unsigned Index : 31;

    /// Whether a fixed type binding relates the two type variables.
    unsigned FixedBinding : 1;

    /// The number of constraints that link this type variable to the
    /// enclosing node.
    unsigned NumConstraints;

    bool empty() const {
      return !FixedBinding && !NumConstraints;
    }
  };

public:
  explicit ConstraintGraphNode(TypeVariableType *typeVar) : TypeVar(typeVar) { }

  ConstraintGraphNode(const ConstraintGraphNode&) = delete;
  ConstraintGraphNode &operator=(const ConstraintGraphNode&) = delete;

  /// Retrieve the type variable this node represents.
  TypeVariableType *getTypeVariable() const { return TypeVar; }

  /// Retrieve the set of constraints that mention this type variable.
  ///
  /// These are the hyperedges of the graph, connecting this node to
  /// various other nodes.
  ArrayRef<Constraint *> getConstraints() const { return Constraints; }

  /// Retrieve the set of type variables to which this node is adjacent.
  ArrayRef<TypeVariableType *> getAdjacencies() const {
    return Adjacencies;
  }

  /// Retrieve all of the type variables in the same equivalence class
  /// as this type variable.
  ArrayRef<TypeVariableType *> getEquivalenceClass() const;

private:
  /// Retrieve all of the type variables in the same equivalence class
  /// as this type variable.
  ArrayRef<TypeVariableType *> getEquivalenceClassUnsafe() const;

  /// Add a constraint to the list of constraints.
  void addConstraint(Constraint *constraint);

  /// Remove a constraint from the list of constraints.
  ///
  /// Note that this only removes the constraint itself; it does not
  /// remove the corresponding adjacencies.
  void removeConstraint(Constraint *constraint);

  /// Retrieve adjacency information for the given type variable.
  Adjacency &getAdjacency(TypeVariableType *typeVar);

  /// Modify the adjacency information for the given type variable
  /// directly. If the adjacency becomes empty afterward, it will be
  /// removed.
  void modifyAdjacency(TypeVariableType *typeVar,
                       std::function<void(Adjacency& adj)> modify);

  /// Add an adjacency to the list of adjacencies.
  void addAdjacency(TypeVariableType *typeVar);

  /// Remove an adjacency from the list of adjacencies.
  void removeAdjacency(TypeVariableType *typeVar);

  /// Add the given type variables to this node's equivalence class.
  void addToEquivalenceClass(ArrayRef<TypeVariableType *> typeVars);

  /// Add a type variable related to this type variable through fixed
  /// bindings.
  void addFixedBinding(TypeVariableType *typeVar);
  
  /// Remove a type variable from the fixed-binding relationship.
  void removeFixedBinding(TypeVariableType *typeVar);

  /// Retrieves the member type of this type variable that corresponds
  /// to the given name.
  ///
  /// \param name The name of the type member.
  ///
  /// \param create If the member does not already exist, this
  /// callback will be invoked to create it.
  TypeVariableType *getMemberType(Identifier name,
                                  std::function<TypeVariableType *()> create);

  /// The type variable this node represents.
  TypeVariableType *TypeVar;

  /// The vector of constraints that mention this type variable, in a stable
  /// order for iteration.
  SmallVector<Constraint *, 2> Constraints;

  /// A mapping from the set of constraints that mention this type variable
  /// to the index within the vector of constraints.
  llvm::SmallDenseMap<Constraint *, unsigned, 2> ConstraintIndex;

  /// The set of adjacent type variables, in a stable order.
  SmallVector<TypeVariableType *, 2> Adjacencies;

  /// A mapping from each of the type variables adjacent to this
  /// type variable to the index of the adjacency information in
  /// \c Adjacencies.
  llvm::SmallDenseMap<TypeVariableType *, Adjacency, 2> AdjacencyInfo;

  /// All of the type variables in the same equivalence class as this
  /// representative type variable.
  ///
  /// Note that this field is only valid for type variables that
  /// are representatives of their equivalence classes.
  mutable SmallVector<TypeVariableType *, 2> EquivalenceClass;

  /// A set of (name, type variable) pairs representing the member types of 
  /// the given type variable.
  llvm::SmallVector<std::pair<Identifier, TypeVariableType *>, 2> MemberTypes;

  /// A mapping from the names of members of this type variable to an
  /// index into the \c MemberTypes vector.
  llvm::SmallDenseMap<Identifier, unsigned> MemberTypeIndex;

  /// Print this graph node.
  void print(llvm::raw_ostream &out, unsigned indent);

  LLVM_ATTRIBUTE_DEPRECATED(void dump() LLVM_ATTRIBUTE_USED,
                            "only for use within the debugger");

  /// Verify the invariants of this node within the given constraint graph.
  void verify(ConstraintGraph &cg);

  friend class ConstraintGraph;
};

/// A graph that describes the relationships among the various type variables
/// and constraints within a constraint system.
///
/// The constraint graph is a hypergraph where the nodes are type variables and
/// the edges are constraints. Any given constraint connects a type variable to
/// zero or more other type variables. Because these adjacencies are as
/// important as the edges themselves and are expensive to calculate from the
/// constraints, each node in the graph tracks both its edges (constraints) and
/// its adjacencies (the type variables) separately.
class ConstraintGraph {
public:
  /// Constraint a constraint graph for the given constraint system.
  ConstraintGraph(ConstraintSystem &cs);

  /// Destroy the given constraint graph.
  ~ConstraintGraph();

  ConstraintGraph(const ConstraintGraph &) = delete;
  ConstraintGraph &operator=(const ConstraintGraph &) = delete;


  /// Retrieve the constraint system this graph describes.
  ConstraintSystem &getConstraintSystem() const { return CS; }

  /// Access the node corresponding to the given type variable.
  ConstraintGraphNode &operator[](TypeVariableType *typeVar) {
    return lookupNode(typeVar).first;
  }

  /// Retrieve the node and index corresponding to the given type variable.
  std::pair<ConstraintGraphNode &, unsigned> 
  lookupNode(TypeVariableType *typeVar);

  /// Add a new constraint to the graph.
  void addConstraint(Constraint *constraint);

  /// Remove a constraint from the graph.
  void removeConstraint(Constraint *constraint);

  /// Retrieves the member type of a type variable that corresponds to
  /// the given name.
  ///
  /// \param typeVar The type variable.
  ///
  /// \param name The name of the type member.
  ///
  /// \param create If the member does not already exist, this
  /// callback will be invoked to create it.
  ///
  /// \returns the type variable representing the named member of the
  /// given type variable.
  TypeVariableType *getMemberType(TypeVariableType *typeVar,
                                  Identifier name,
                                  std::function<TypeVariableType *()> create);

  /// Merge the two nodes for the two given type variables.
  ///
  /// The type variables must actually have been merged already; this
  /// operation merges the two nodes.
  void mergeNodes(TypeVariableType *typeVar1, TypeVariableType *typeVar2);

  /// Bind the given type variable to the given fixed type.
  void bindTypeVariable(TypeVariableType *typeVar, Type fixedType);

  /// Gather the set of constraints that involve the given type variable,
  /// i.e., those constraints that will be affected when the type variable
  /// gets merged or bound to a fixed type.
  ///
  /// The resulting set of constraints may contain duplicates.
  void gatherConstraints(TypeVariableType *typeVar,
                         SmallVectorImpl<Constraint *> &constraints);

  /// Retrieve the type variables that correspond to nodes in the graph.
  ///
  /// The subscript operator can be used to retrieve the nodes that
  /// correspond to these type variables.
  ArrayRef<TypeVariableType *> getTypeVariables() const {
    return TypeVariables;
  }

  /// Compute the connected components of the graph.
  ///
  /// \param typeVars The type variables that occur within the
  /// connected components. If a non-empty vector is passed in, the algorithm
  /// will only consider type variables reachable from the initial set.
  ///
  /// \param components Receives the component numbers for each type variable
  /// in \c typeVars.
  ///
  /// \returns the number of connected components in the graph.
  unsigned computeConnectedComponents(
             SmallVectorImpl<TypeVariableType *> &typeVars,
             SmallVectorImpl<unsigned> &components);

  /// Print the graph.
  void print(llvm::raw_ostream &out);

  LLVM_ATTRIBUTE_DEPRECATED(void dump() LLVM_ATTRIBUTE_USED,
                            "only for use within the debugger");

  /// Print the connected components of the graph.
  void printConnectedComponents(llvm::raw_ostream &out);

  LLVM_ATTRIBUTE_DEPRECATED(void dumpConnectedComponents() LLVM_ATTRIBUTE_USED,
                            "only for use within the debugger");

  /// Verify the invariants of the graph.
  void verify();

  /// Optimize the constraint graph by eliminating simple transitive
  /// connections between nodes.
  void optimize();

private:
  /// Remove the node corresponding to the given type variable.
  ///
  /// This operation assumes that the any constraints that refer to
  /// this type variable have been or will be removed before other
  /// graph queries are performed.
  ///
  /// Note that this change is not recorded and cannot be undone. Use with
  /// caution.
  void removeNode(TypeVariableType *typeVar);

  /// Unbind the given type variable from the given fixed type.
  ///
  /// Note that this change is not recorded and cannot be undone. Use with
  /// caution.
  void unbindTypeVariable(TypeVariableType *typeVar, Type fixedType);


  /// Perform edge contraction on the constraint graph, merging equivalence
  /// classes until a fixed point is reached.
  bool contractEdges();

  /// To support edge contraction, remove a constraint from both the constraint
  /// graph and its enclosing constraint system.
  void removeEdge(Constraint *constraint);

  /// The constraint system.
  ConstraintSystem &CS;

  /// The type variables in this graph, in stable order.
  SmallVector<TypeVariableType *, 4> TypeVariables;

  /// The kind of change made to the graph.
  enum class ChangeKind {
    /// Added a type variable.
    AddedTypeVariable,
    /// Added a new constraint.
    AddedConstraint,
    /// Removed an existing constraint
    RemovedConstraint,
    /// Extended the equivalence class of a type variable.
    ExtendedEquivalenceClass,
    /// Added a fixed binding for a type variable.
    BoundTypeVariable,
    /// Added a member type.
    AddedMemberType
  };

  /// A change made to the constraint graph.
  ///
  /// Each change can be undone (once, and in reverse order) by calling the
  /// undo() method.
  class Change {
    /// The kind of change.
    ChangeKind Kind;

    union {
      TypeVariableType *TypeVar;
      Constraint *TheConstraint;

      struct {
        /// The type variable whose equivalence class was extended.
        TypeVariableType *TypeVar;

        /// The previous size of the equivalence class.
        unsigned PrevSize;
      } EquivClass;

      struct {
        /// The type variable being bound to a fixed type.
        TypeVariableType *TypeVar;

        /// The fixed type to which the type variable was bound.
        TypeBase *FixedType;
      } Binding;

      struct {
        /// The type variable whose member type was created.
        TypeVariableType *TypeVar;

        /// The name of the member.
        Identifier Name;
      } MemberType;
    };

  public:
    Change() : Kind(ChangeKind::AddedTypeVariable), TypeVar(nullptr) { }

    /// Create a change that added a type variable.
    static Change addedTypeVariable(TypeVariableType *typeVar);

    /// Create a change that added a constraint.
    static Change addedConstraint(Constraint *constraint);

    /// Create a change that removed a constraint.
    static Change removedConstraint(Constraint *constraint);

    /// Create a change that extended an equivalence class.
    static Change extendedEquivalenceClass(TypeVariableType *typeVar,
                                           unsigned prevSize);

    /// Create a change that bound a type variable to a fixed type.
    static Change boundTypeVariable(TypeVariableType *typeVar, Type fixed);

    /// Create a change that added a member type with the given name.
    static Change addedMemberType(TypeVariableType *typeVar, Identifier name);

    /// Undo this change, reverting the constraint graph to the state it
    /// had prior to this change.
    ///
    /// Changes must be undone in stack order.
    void undo(ConstraintGraph &cg);
  };

  /// The currently active scope, or null if we aren't tracking changes made
  /// to the constraint graph.
  ConstraintGraphScope *ActiveScope = nullptr;

  /// The set of changes made to this constraint graph.
  ///
  /// As the constraint graph is extended and mutated, additional changes are
  /// introduced into this vector. Each scope
  llvm::SmallVector<Change, 4> Changes;

  friend class ConstraintGraphScope;
};

} // end namespace swift::constraints

} // end namespace swift

#endif // LLVM_SWIFT_SEMA_CONSTRAINT_GRAPH_H
