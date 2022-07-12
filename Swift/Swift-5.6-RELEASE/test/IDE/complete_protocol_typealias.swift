// RUN: %empty-directory(%t)
// RUN: %target-swift-ide-test -batch-code-completion -source-filename %s -filecheck %raw-FileCheck -completion-output-dir %t

protocol MyProto {
  typealias Content = Int
}
func testSimpleInTypeCompletion() -> MyProto.#^SIMPLE_IN_TYPE_COMPLETION^# {}
// SIMPLE_IN_TYPE_COMPLETION: Begin completions, 3 items
// SIMPLE_IN_TYPE_COMPLETION-DAG: Decl[TypeAlias]/CurrNominal:        Content[#Int#];
// SIMPLE_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Protocol[#MyProto.Protocol#];
// SIMPLE_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Type[#MyProto.Type#];
// SIMPLE_IN_TYPE_COMPLETION: End completions

func testUnconstrainedUnresolvedMember() {
  let _: MyProto = .#^UNCONSTRAINED_UNRESOLVED_MEMBER^#
// UNCONSTRAINED_UNRESOLVED_MEMBER: Begin completions, 1 item
// UNCONSTRAINED_UNRESOLVED_MEMBER-DAG: Decl[TypeAlias]/CurrNominal:        Content[#Int#];
// UNCONSTRAINED_UNRESOLVED_MEMBER: End completions
}

protocol MyOtherProto {
  associatedtype MyAssocType
}
extension MyOtherProto where MyAssocType == String {
  typealias Content = Int
}

// `Content` is actually accessible on `MyOtherProto` here, but that seems more like a bug of the language than a feature, so we don't want to promote it in code completion.
func testConstrainedInTypeCompletion() -> MyOtherProto.#^CONSTRAINED_IN_TYPE_COMPLETION^# {}
// CONSTRAINED_IN_TYPE_COMPLETION: Begin completions, 3 items
// CONSTRAINED_IN_TYPE_COMPLETION-DAG: Decl[AssociatedType]/CurrNominal:   MyAssocType;
// CONSTRAINED_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Protocol[#MyOtherProto.Protocol#];
// CONSTRAINED_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Type[#MyOtherProto.Type#];
// CONSTRAINED_IN_TYPE_COMPLETION: End completions

func testConstrainedUnresolvedMember() {
  let _: MyOtherProto = .#^CONSTRAINED_UNRESOLVED_MEMBER^#
// CONSTRAINED_UNRESOLVED_MEMBER: Begin completions, 1 item
// CONSTRAINED_UNRESOLVED_MEMBER-DAG: Decl[AssociatedType]/CurrNominal:   MyAssocType;
// CONSTRAINED_UNRESOLVED_MEMBER: End completions
}

protocol ProtoWithGenericTypealias {
  typealias Storage<T> = Array<T>
}
func testGenericInTypeCompletion() -> ProtoWithGenericTypealias.#^GENERIC_IN_TYPE_COMPLETION^# {}
// GENERIC_IN_TYPE_COMPLETION: Begin completions, 3 items
// GENERIC_IN_TYPE_COMPLETION-DAG: Decl[TypeAlias]/CurrNominal:        Storage[#Array<T>#];
// GENERIC_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Protocol[#ProtoWithGenericTypealias.Protocol#];
// GENERIC_IN_TYPE_COMPLETION-DAG: Keyword/None:                       Type[#ProtoWithGenericTypealias.Type#];
// GENERIC_IN_TYPE_COMPLETION: End completions

func testGenericUnresolvedMember() {
  let _: ProtoWithGenericTypealias = .#^GENERIC_UNRESOLVED_MEMBER^#
// GENERIC_UNRESOLVED_MEMBER: Begin completions, 1 item
// GENERIC_UNRESOLVED_MEMBER-DAG: Decl[TypeAlias]/CurrNominal:   Storage[#Array<T>#];
// GENERIC_UNRESOLVED_MEMBER: End completions
}

struct ConformingType: MyProto {
	func foo(content: #^GLOBAL_COMPLETE_IN_CONFORMING_TYPE^#) {}
// GLOBAL_COMPLETE_IN_CONFORMING_TYPE: Begin completions
// GLOBAL_COMPLETE_IN_CONFORMING_TYPE: Decl[TypeAlias]/Super:              Content[#Int#];
// GLOBAL_COMPLETE_IN_CONFORMING_TYPE: End completions
}