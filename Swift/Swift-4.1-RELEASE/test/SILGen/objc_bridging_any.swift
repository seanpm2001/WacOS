// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -Xllvm -sil-print-debuginfo -emit-silgen -enable-sil-ownership %s | %FileCheck %s
// REQUIRES: objc_interop

import Foundation
import objc_generics

protocol P {}
protocol CP: class {}

struct KnownUnbridged {}

// CHECK-LABEL: sil hidden @_T017objc_bridging_any11passingToId{{.*}}F
func passingToId<T: CP, U>(receiver: NSIdLover,
                           string: String,
                           nsString: NSString,
                           object: AnyObject,
                           classGeneric: T,
                           classExistential: CP,
                           generic: U,
                           existential: P,
                           error: Error,
                           any: Any,
                           knownUnbridged: KnownUnbridged,
                           optionalA: String?,
                           optionalB: NSString?,
                           optionalC: Any?) {
  // CHECK: bb0([[SELF:%.*]] : @owned $NSIdLover,
  // CHECK:   debug_value [[STRING:%.*]] : $String
  // CHECK:   debug_value [[NSSTRING:%.*]] : $NSString
  // CHECK:   debug_value [[OBJECT:%.*]] : $AnyObject
  // CHECK:   debug_value [[CLASS_GENERIC:%.*]] : $T
  // CHECK:   debug_value [[CLASS_EXISTENTIAL:%.*]] : $CP
  // CHECK:   debug_value_addr [[GENERIC:%.*]] : $*U
  // CHECK:   debug_value_addr [[EXISTENTIAL:%.*]] : $*P
  // CHECK:   debug_value [[ERROR:%.*]] : $Error
  // CHECK:   debug_value_addr [[ANY:%.*]] : $*Any
  // CHECK:   debug_value [[KNOWN_UNBRIDGED:%.*]] : $KnownUnbridged
  // CHECK:   debug_value [[OPT_STRING:%.*]] : $Optional<String>
  // CHECK:   debug_value [[OPT_NSSTRING:%.*]] : $Optional<NSString>
  // CHECK:   debug_value_addr [[OPT_ANY:%.*]] : $*Optional<Any>

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_STRING:%.*]] = begin_borrow [[STRING]]
  // CHECK:   [[STRING_COPY:%.*]] = copy_value [[BORROWED_STRING]]
  // CHECK:   [[BRIDGE_STRING:%.*]] = function_ref @_T0SS10FoundationE19_bridgeToObjectiveCSo8NSStringCyF
  // CHECK:   [[BORROWED_STRING_COPY:%.*]] = begin_borrow [[STRING_COPY]]
  // CHECK:   [[BRIDGED:%.*]] = apply [[BRIDGE_STRING]]([[BORROWED_STRING_COPY]])
  // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[BRIDGED]] : $NSString : $NSString, $AnyObject
  // CHECK:   end_borrow [[BORROWED_STRING_COPY]] from [[STRING_COPY]]
  // CHECK:   destroy_value [[STRING_COPY]]
  // CHECK:   end_borrow [[BORROWED_STRING]] from [[STRING]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]]
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(string)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_NSSTRING:%.*]] = begin_borrow [[NSSTRING]]
  // CHECK:   [[NSSTRING_COPY:%.*]] = copy_value [[BORROWED_NSSTRING]]
  // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[NSSTRING_COPY]] : $NSString : $NSString, $AnyObject
  // CHECK:   end_borrow [[BORROWED_NSSTRING]] from [[NSSTRING]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(nsString)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_CLASS_GENERIC:%.*]] = begin_borrow [[CLASS_GENERIC]]
  // CHECK:   [[CLASS_GENERIC_COPY:%.*]] = copy_value [[BORROWED_CLASS_GENERIC]]
  // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[CLASS_GENERIC_COPY]] : $T : $T, $AnyObject
  // CHECK:   end_borrow [[BORROWED_CLASS_GENERIC]] from [[CLASS_GENERIC]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(classGeneric)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_OBJECT:%.*]] = begin_borrow [[OBJECT]]
  // CHECK:   [[OBJECT_COPY:%.*]] = copy_value [[BORROWED_OBJECT]]
  // CHECK:   end_borrow [[BORROWED_OBJECT]] from [[OBJECT]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[OBJECT_COPY]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[OBJECT_COPY]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(object)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_CLASS_EXISTENTIAL:%.*]] = begin_borrow [[CLASS_EXISTENTIAL]]
  // CHECK:   [[CLASS_EXISTENTIAL_COPY:%.*]] = copy_value [[BORROWED_CLASS_EXISTENTIAL]]
  // CHECK:   [[OPENED:%.*]] = open_existential_ref [[CLASS_EXISTENTIAL_COPY]] : $CP
  // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[OPENED]]
  // CHECK:   end_borrow [[BORROWED_CLASS_EXISTENTIAL]] from [[CLASS_EXISTENTIAL]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(classExistential)

  // These cases perform a universal bridging conversion.

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[COPY:%.*]] = alloc_stack $U
  // CHECK:   copy_addr [[GENERIC]] to [initialization] [[COPY]]
  // CHECK:   // function_ref _bridgeAnythingToObjectiveC
  // CHECK:   [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<U>([[COPY]])
  // CHECK:   dealloc_stack [[COPY]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(generic)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[COPY:%.*]] = alloc_stack $P
  // CHECK:   copy_addr [[EXISTENTIAL]] to [initialization] [[COPY]]
  // CHECK:   [[OPENED_COPY:%.*]] = open_existential_addr immutable_access [[COPY]] : $*P to $*[[OPENED_TYPE:@opened.*P]],
  // CHECK:   [[TMP:%.*]] = alloc_stack $[[OPENED_TYPE]]
  // CHECK:   copy_addr [[OPENED_COPY]] to [initialization] [[TMP]]
  // CHECK:   // function_ref _bridgeAnythingToObjectiveC
  // CHECK:   [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
  // CHECK:   dealloc_stack [[TMP]]
  // CHECK:   destroy_addr [[COPY]]
  // CHECK:   dealloc_stack [[COPY]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(existential)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_ERROR:%.*]] = begin_borrow [[ERROR]]
  // CHECK:   [[ERROR_COPY:%.*]] = copy_value [[BORROWED_ERROR]] : $Error
  // CHECK:   [[ERROR_BOX:%[0-9]+]] = open_existential_box [[ERROR_COPY]] : $Error to $*@opened([[ERROR_ARCHETYPE:"[^"]*"]]) Error
  // CHECK:   [[ERROR_STACK:%[0-9]+]] = alloc_stack $@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK:   copy_addr [[ERROR_BOX]] to [initialization] [[ERROR_STACK]] : $*@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK:   [[BRIDGE_FUNCTION:%[0-9]+]] = function_ref @_T0s27_bridgeAnythingToObjectiveCyXlxlF
  // CHECK:   [[BRIDGED_ERROR:%[0-9]+]] = apply [[BRIDGE_FUNCTION]]<@opened([[ERROR_ARCHETYPE]]) Error>([[ERROR_STACK]])
  // CHECK:   dealloc_stack [[ERROR_STACK]] : $*@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK:   destroy_value [[ERROR_COPY]] : $Error
  // CHECK:   end_borrow [[BORROWED_ERROR]] from [[ERROR]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[BRIDGED_ERROR]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[BRIDGED_ERROR]] : $AnyObject
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(error)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[COPY:%.*]] = alloc_stack $Any
  // CHECK:   copy_addr [[ANY]] to [initialization] [[COPY]]
  // CHECK:   [[OPENED_COPY:%.*]] = open_existential_addr immutable_access [[COPY]] : $*Any to $*[[OPENED_TYPE:@opened.*Any]],
  // CHECK:   [[TMP:%.*]] = alloc_stack $[[OPENED_TYPE]]
  // CHECK:   copy_addr [[OPENED_COPY]] to [initialization] [[TMP]]
  // CHECK:   // function_ref _bridgeAnythingToObjectiveC
  // CHECK:   [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
  // CHECK:   destroy_addr [[COPY]]
  // CHECK:   dealloc_stack [[COPY]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(any)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[TMP:%.*]] = alloc_stack $KnownUnbridged
  // CHECK:   store [[KNOWN_UNBRIDGED]] to [trivial] [[TMP]]
  // CHECK:   [[BRIDGE_ANYTHING:%.*]] = function_ref @_T0s27_bridgeAnythingToObjectiveC{{.*}}F
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<KnownUnbridged>([[TMP]])
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(knownUnbridged)

  // These cases bridge using Optional's _ObjectiveCBridgeable conformance.

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_OPT_STRING:%.*]] = begin_borrow [[OPT_STRING]]
  // CHECK:   [[OPT_STRING_COPY:%.*]] = copy_value [[BORROWED_OPT_STRING]]
  // CHECK:   [[BRIDGE_OPTIONAL:%.*]] = function_ref @_T0Sq19_bridgeToObjectiveCyXlyF
  // CHECK:   [[TMP:%.*]] = alloc_stack $Optional<String>
  // CHECK:   [[BORROWED_OPT_STRING_COPY:%.*]] = begin_borrow [[OPT_STRING_COPY]]
  // CHECK:   store_borrow [[BORROWED_OPT_STRING_COPY]] to [[TMP]]
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_OPTIONAL]]<String>([[TMP]])
  // CHECK:   end_borrow [[BORROWED_OPT_STRING]] from [[OPT_STRING]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(optionalA)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[BORROWED_OPT_NSSTRING:%.*]] = begin_borrow [[OPT_NSSTRING]]
  // CHECK:   [[OPT_NSSTRING_COPY:%.*]] = copy_value [[BORROWED_OPT_NSSTRING]]
  // CHECK:   [[BRIDGE_OPTIONAL:%.*]] = function_ref @_T0Sq19_bridgeToObjectiveCyXlyF
  // CHECK:   [[TMP:%.*]] = alloc_stack $Optional<NSString>
  // CHECK:   [[BORROWED_OPT_NSSTRING_COPY:%.*]] = begin_borrow [[OPT_NSSTRING_COPY]]
  // CHECK:   store_borrow [[BORROWED_OPT_NSSTRING_COPY]] to [[TMP]]
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_OPTIONAL]]<NSString>([[TMP]])
  // CHECK:   end_borrow [[BORROWED_OPT_NSSTRING]] from [[OPT_NSSTRING]]
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(optionalB)

  // CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK:   [[TMP:%.*]] = alloc_stack $Optional<Any>
  // CHECK:   copy_addr [[OPT_ANY]] to [initialization] [[TMP]]
  // CHECK:   [[BRIDGE_OPTIONAL:%.*]] = function_ref @_T0Sq19_bridgeToObjectiveCyXlyF
  // CHECK:   [[ANYOBJECT:%.*]] = apply [[BRIDGE_OPTIONAL]]<Any>([[TMP]])
  // CHECK:   [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK:   apply [[METHOD]]([[ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesId(optionalC)

  // TODO: Property and subscript setters
}

// Once upon a time, as a workaround for rdar://problem/28318984, we had
// to skip the peephole for types with nontrivial SIL lowerings because we
// didn't correctly form the substitutions for a generic
// _bridgeAnythingToObjectiveC call.  That's not true anymore.
func zim() {}
struct Zang {}
// CHECK-LABEL: sil hidden @_T017objc_bridging_any27typesWithNontrivialLoweringySo9NSIdLoverC8receiver_tF
func typesWithNontrivialLowering(receiver: NSIdLover) {
  // CHECK: apply {{.*}}<() -> ()>
  receiver.takesId(zim)
  // CHECK: apply {{.*}}<Zang.Type>
  receiver.takesId(Zang.self)
  // CHECK: apply {{.*}}<(() -> (), Zang.Type)>
  receiver.takesId((zim, Zang.self))
  // CHECK: apply {{%.*}}<(Int, String)>
  receiver.takesId((0, "one"))
}

// CHECK-LABEL: sil hidden @_T017objc_bridging_any19passingToNullableId{{.*}}F
func passingToNullableId<T: CP, U>(receiver: NSIdLover,
                                   string: String,
                                   nsString: NSString,
                                   object: AnyObject,
                                   classGeneric: T,
                                   classExistential: CP,
                                   generic: U,
                                   existential: P,
                                   error: Error,
                                   any: Any,
                                   knownUnbridged: KnownUnbridged,
                                   optString: String?,
                                   optNSString: NSString?,
                                   optObject: AnyObject?,
                                   optClassGeneric: T?,
                                   optClassExistential: CP?,
                                   optGeneric: U?,
                                   optExistential: P?,
                                   optAny: Any?,
                                   optKnownUnbridged: KnownUnbridged?,
                                   optOptA: String??,
                                   optOptB: NSString??,
                                   optOptC: Any??)
{
  // CHECK: bb0([[SELF:%.*]] : @owned $NSIdLover,
  // CHECK: [[STRING:%.*]] : $String,
  // CHECK: [[NSSTRING:%.*]] : $NSString
  // CHECK: [[OBJECT:%.*]] : $AnyObject
  // CHECK: [[CLASS_GENERIC:%.*]] : $T
  // CHECK: [[CLASS_EXISTENTIAL:%.*]] : $CP
  // CHECK: [[GENERIC:%.*]] : $*U
  // CHECK: [[EXISTENTIAL:%.*]] : $*P
  // CHECK: [[ERROR:%.*]] : $Error
  // CHECK: [[ANY:%.*]] : $*Any,
  // CHECK: [[KNOWN_UNBRIDGED:%.*]] : $KnownUnbridged,
  // CHECK: [[OPT_STRING:%.*]] : $Optional<String>,
  // CHECK: [[OPT_NSSTRING:%.*]] : $Optional<NSString>
  // CHECK: [[OPT_OBJECT:%.*]] : $Optional<AnyObject>
  // CHECK: [[OPT_CLASS_GENERIC:%.*]] : $Optional<T>
  // CHECK: [[OPT_CLASS_EXISTENTIAL:%.*]] : $Optional<CP>
  // CHECK: [[OPT_GENERIC:%.*]] : $*Optional<U>
  // CHECK: [[OPT_EXISTENTIAL:%.*]] : $*Optional<P>
  // CHECK: [[OPT_ANY:%.*]] : $*Optional<Any>
  // CHECK: [[OPT_KNOWN_UNBRIDGED:%.*]] : $Optional<KnownUnbridged>
  // CHECK: [[OPT_OPT_A:%.*]] : $Optional<Optional<String>>
  // CHECK: [[OPT_OPT_B:%.*]] : $Optional<Optional<NSString>>
  // CHECK: [[OPT_OPT_C:%.*]] : $*Optional<Optional<Any>>

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_STRING:%.*]] = begin_borrow [[STRING]]
  // CHECK: [[STRING_COPY:%.*]] = copy_value [[BORROWED_STRING]]
  // CHECK: [[BRIDGE_STRING:%.*]] = function_ref @_T0SS10FoundationE19_bridgeToObjectiveCSo8NSStringCyF
  // CHECK: [[BORROWED_STRING_COPY:%.*]] = begin_borrow [[STRING_COPY]]
  // CHECK: [[BRIDGED:%.*]] = apply [[BRIDGE_STRING]]([[BORROWED_STRING_COPY]])
  // CHECK: [[ANYOBJECT:%.*]] = init_existential_ref [[BRIDGED]] : $NSString : $NSString, $AnyObject
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK: end_borrow [[BORROWED_STRING_COPY]] from [[STRING_COPY]]
  // CHECK: destroy_value [[STRING_COPY]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]]
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: destroy_value [[OPT_ANYOBJECT]]
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(string)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_NSSTRING:%.*]] = begin_borrow [[NSSTRING]]
  // CHECK: [[NSSTRING_COPY:%.*]] = copy_value [[BORROWED_NSSTRING]]
  // CHECK: [[ANYOBJECT:%.*]] = init_existential_ref [[NSSTRING_COPY]] : $NSString : $NSString, $AnyObject
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK: end_borrow [[BORROWED_NSSTRING]] from [[NSSTRING]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(nsString)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_OBJECT:%.*]] = begin_borrow [[OBJECT]]
  // CHECK: [[OBJECT_COPY:%.*]] = copy_value [[BORROWED_OBJECT]]
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[OBJECT_COPY]]
  // CHECK: end_borrow [[BORROWED_OBJECT]] from [[OBJECT]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(object)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_CLASS_GENERIC:%.*]] = begin_borrow [[CLASS_GENERIC]]
  // CHECK: [[CLASS_GENERIC_COPY:%.*]] = copy_value [[BORROWED_CLASS_GENERIC]]
  // CHECK: [[ANYOBJECT:%.*]] = init_existential_ref [[CLASS_GENERIC_COPY]] : $T : $T, $AnyObject
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK: end_borrow [[BORROWED_CLASS_GENERIC]] from [[CLASS_GENERIC]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(classGeneric)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_CLASS_EXISTENTIAL:%.*]] = begin_borrow [[CLASS_EXISTENTIAL]]
  // CHECK: [[CLASS_EXISTENTIAL_COPY:%.*]] = copy_value [[BORROWED_CLASS_EXISTENTIAL]]
  // CHECK: [[OPENED:%.*]] = open_existential_ref [[CLASS_EXISTENTIAL_COPY]] : $CP
  // CHECK: [[ANYOBJECT:%.*]] = init_existential_ref [[OPENED]]
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK: end_borrow [[BORROWED_CLASS_EXISTENTIAL]] from [[CLASS_EXISTENTIAL]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(classExistential)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK-NEXT: [[COPY:%.*]] = alloc_stack $U
  // CHECK-NEXT: copy_addr [[GENERIC]] to [initialization] [[COPY]]
  // CHECK-NEXT: // function_ref _bridgeAnythingToObjectiveC
  // CHECK-NEXT: [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK-NEXT: [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<U>([[COPY]])
  // CHECK-NEXT: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK-NEXT: dealloc_stack [[COPY]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK-NEXT: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK-NEXT: destroy_value [[OPT_ANYOBJECT]]
  // CHECK-NEXT: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(generic)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK-NEXT: [[COPY:%.*]] = alloc_stack $P
  // CHECK-NEXT: copy_addr [[EXISTENTIAL]] to [initialization] [[COPY]]
  // CHECK-NEXT: [[OPENED_COPY:%.*]] = open_existential_addr immutable_access [[COPY]] : $*P to $*[[OPENED_TYPE:@opened.*P]],
  // CHECK: [[TMP:%.*]] = alloc_stack $[[OPENED_TYPE]]
  // CHECK: copy_addr [[OPENED_COPY]] to [initialization] [[TMP]]
  // CHECK-NEXT: // function_ref _bridgeAnythingToObjectiveC
  // CHECK-NEXT: [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK-NEXT: [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
  // CHECK-NEXT: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
	// CHECK-NEXT: dealloc_stack [[TMP]]
  // CHECK-NEXT: destroy_addr [[COPY]]
	// CHECK-NEXT: dealloc_stack [[COPY]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK-NEXT: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK-NEXT: destroy_value [[OPT_ANYOBJECT]]
  // CHECK-NEXT: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(existential)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_ERROR:%.*]] = begin_borrow [[ERROR]]
  // CHECK-NEXT: [[ERROR_COPY:%.*]] = copy_value [[BORROWED_ERROR]] : $Error
  // CHECK-NEXT: [[ERROR_BOX:%[0-9]+]] = open_existential_box [[ERROR_COPY]] : $Error to $*@opened([[ERROR_ARCHETYPE:"[^"]*"]]) Error
  // CHECK-NEXT: [[ERROR_STACK:%[0-9]+]] = alloc_stack $@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK-NEXT: copy_addr [[ERROR_BOX]] to [initialization] [[ERROR_STACK]] : $*@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK: [[BRIDGE_FUNCTION:%[0-9]+]] = function_ref @_T0s27_bridgeAnythingToObjectiveCyXlxlF
  // CHECK-NEXT: [[BRIDGED_ERROR:%[0-9]+]] = apply [[BRIDGE_FUNCTION]]<@opened([[ERROR_ARCHETYPE]]) Error>([[ERROR_STACK]])
  // CHECK-NEXT: [[BRIDGED_ERROR_OPT:%[0-9]+]] = enum $Optional<AnyObject>, #Optional.some!enumelt.1, [[BRIDGED_ERROR]] : $AnyObject
  // CHECK-NEXT: dealloc_stack [[ERROR_STACK]] : $*@opened([[ERROR_ARCHETYPE]]) Error
  // CHECK-NEXT: destroy_value [[ERROR_COPY]] : $Error
  // CHECK-NEXT: end_borrow [[BORROWED_ERROR]] from [[ERROR]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK-NEXT: apply [[METHOD]]([[BRIDGED_ERROR_OPT]], [[BORROWED_SELF]])
  // CHECK-NEXT: destroy_value [[BRIDGED_ERROR_OPT]]
  // CHECK-NEXT: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(error)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK-NEXT: [[COPY:%.*]] = alloc_stack $Any
  // CHECK-NEXT: copy_addr [[ANY]] to [initialization] [[COPY]]
  // CHECK-NEXT: [[OPENED_COPY:%.*]] = open_existential_addr immutable_access [[COPY]] : $*Any to $*[[OPENED_TYPE:@opened.*Any]],
  // CHECK: [[TMP:%.*]] = alloc_stack $[[OPENED_TYPE]]
  // CHECK: copy_addr [[OPENED_COPY]] to [initialization] [[TMP]]
  // CHECK-NEXT: // function_ref _bridgeAnythingToObjectiveC
  // CHECK-NEXT: [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK-NEXT: [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
  // CHECK-NEXT: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK-NEXT: dealloc_stack [[TMP]]
  // CHECK-NEXT: destroy_addr [[COPY]]
  // CHECK-NEXT: dealloc_stack [[COPY]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK-NEXT: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK-NEXT: destroy_value [[OPT_ANYOBJECT]]
  // CHECK-NEXT: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(any)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[TMP:%.*]] = alloc_stack $KnownUnbridged
  // CHECK: store [[KNOWN_UNBRIDGED]] to [trivial] [[TMP]]
  // CHECK: [[BRIDGE_ANYTHING:%.*]] = function_ref @_T0s27_bridgeAnythingToObjectiveC{{.*}}F
  // CHECK: [[ANYOBJECT:%.*]] = apply [[BRIDGE_ANYTHING]]<KnownUnbridged>([[TMP]])
  // CHECK: [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]] : $NSIdLover,
  // CHECK: apply [[METHOD]]([[OPT_ANYOBJECT]], [[BORROWED_SELF]])
  // CHECK: end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(knownUnbridged)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_OPT_STRING:%.*]] = begin_borrow [[OPT_STRING]]
  // CHECK: [[OPT_STRING_COPY:%.*]] = copy_value [[BORROWED_OPT_STRING]]
  // CHECK: switch_enum [[OPT_STRING_COPY]] : $Optional<String>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
  //
  // CHECK: [[SOME_BB]]([[STRING_DATA:%.*]] : @owned $String):
  // CHECK:   [[BRIDGE_STRING:%.*]] = function_ref @_T0SS10FoundationE19_bridgeToObjectiveCSo8NSStringCyF
  // CHECK:   [[BORROWED_STRING_DATA:%.*]] = begin_borrow [[STRING_DATA]]
  // CHECK:   [[BRIDGED:%.*]] = apply [[BRIDGE_STRING]]([[BORROWED_STRING_DATA]])
  // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[BRIDGED]] : $NSString : $NSString, $AnyObject
  // CHECK:   [[OPT_ANYOBJECT:%.*]] = enum {{.*}} [[ANYOBJECT]]
  // CHECK:   end_borrow [[BORROWED_STRING_DATA]] from [[STRING_DATA]]
  // CHECK:   destroy_value [[STRING_DATA]]
  // CHECK:   br [[JOIN:bb.*]]([[OPT_ANYOBJECT]]
  //
  // CHECK: [[NONE_BB]]:
  // CHECK:   [[OPT_NONE:%.*]] = enum $Optional<AnyObject>, #Optional.none!enumelt
  // CHECK:   br [[JOIN]]([[OPT_NONE]]
  //
  // CHECK: [[JOIN]]([[PHI:%.*]] : @owned $Optional<AnyObject>):
  // CHECK:   end_borrow [[BORROWED_OPT_STRING]] from [[OPT_STRING]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]]
  // CHECK:   apply [[METHOD]]([[PHI]], [[BORROWED_SELF]])
  // CHECK:   destroy_value [[PHI]]
  // CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
  receiver.takesNullableId(optString)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]]
  receiver.takesNullableId(optNSString)

  // CHECK: [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
  // CHECK: [[BORROWED_OPT_OBJECT:%.*]] = begin_borrow [[OPT_OBJECT]]
  // CHECK: [[OPT_OBJECT_COPY:%.*]] = copy_value [[BORROWED_OPT_OBJECT]]
  // CHECK: [[METHOD:%.*]] = objc_method [[BORROWED_SELF]]
  // CHECK: apply [[METHOD]]([[OPT_OBJECT_COPY]], [[BORROWED_SELF]])
  receiver.takesNullableId(optObject)
  receiver.takesNullableId(optClassGeneric)
  receiver.takesNullableId(optClassExistential)
  receiver.takesNullableId(optGeneric)
  receiver.takesNullableId(optExistential)
  receiver.takesNullableId(optAny)
  receiver.takesNullableId(optKnownUnbridged)

  receiver.takesNullableId(optOptA)
  receiver.takesNullableId(optOptB)
  receiver.takesNullableId(optOptC)
}

protocol Anyable {
  init(any: Any)
  init(anyMaybe: Any?)
  var anyProperty: Any { get }
  var maybeAnyProperty: Any? { get }
}

// Make sure we generate correct bridging thunks
class SwiftIdLover : NSObject, Anyable {

  func methodReturningAny() -> Any {}
  // SEMANTIC ARC TODO: This is another case of pattern matching the body of one
  // function in a different function... Just pattern match the unreachable case
  // to preserve behavior. We should check if it is correct.

  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC18methodReturningAnyypyF : $@convention(method) (@guaranteed SwiftIdLover) -> @out Any
  // CHECK: unreachable
  // CHECK: } // end sil function '_T017objc_bridging_any12SwiftIdLoverC18methodReturningAnyypyF'

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC18methodReturningAnyypyFTo : $@convention(objc_method) (SwiftIdLover) -> @autoreleased AnyObject {
  // CHECK: bb0([[SELF:%[0-9]+]] : @unowned $SwiftIdLover):
  // CHECK:   [[NATIVE_RESULT:%.*]] = alloc_stack $Any
  // CHECK:   [[SELF_COPY:%.*]] = copy_value [[SELF]] : $SwiftIdLover
  // CHECK:   [[BORROWED_SELF_COPY:%.*]] = begin_borrow [[SELF_COPY]]
  // CHECK:   [[NATIVE_IMP:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC18methodReturningAnyypyF
  // CHECK:   apply [[NATIVE_IMP]]([[NATIVE_RESULT]], [[BORROWED_SELF_COPY]])
  // CHECK:   end_borrow [[BORROWED_SELF_COPY]] from [[SELF_COPY]]
  // CHECK:   destroy_value [[SELF_COPY]]
  // CHECK:   [[OPEN_RESULT:%.*]] = open_existential_addr immutable_access [[NATIVE_RESULT]]
	// CHECK:   [[TMP:%.*]] = alloc_stack
  // CHECK:   copy_addr [[OPEN_RESULT]] to [initialization] [[TMP]]
  // CHECK:   [[BRIDGE_ANYTHING:%.*]] = function_ref @_T0s27_bridgeAnythingToObjectiveC{{.*}}F
  // CHECK:   [[OBJC_RESULT:%.*]] = apply [[BRIDGE_ANYTHING]]<{{.*}}>([[TMP]])
  // CHECK:   return [[OBJC_RESULT]]
  // CHECK: } // end sil function '_T017objc_bridging_any12SwiftIdLoverC18methodReturningAnyypyFTo'

  func methodReturningOptionalAny() -> Any? {}
  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC26methodReturningOptionalAnyypSgyF
  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC26methodReturningOptionalAnyypSgyFTo
  // CHECK:       function_ref @_T0s27_bridgeAnythingToObjectiveC{{.*}}F

  @objc func methodTakingAny(a: Any) {}
  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC15methodTakingAnyyyp1a_tFTo : $@convention(objc_method) (AnyObject, SwiftIdLover) -> ()
  // CHECK:     bb0([[ARG:%.*]] : @unowned $AnyObject, [[SELF:%.*]] : @unowned $SwiftIdLover):
  // CHECK-NEXT:  [[ARG_COPY:%.*]] = copy_value [[ARG]]
  // CHECK-NEXT:  [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK-NEXT:  [[OPENED_SELF:%.*]] = open_existential_ref [[ARG_COPY]]
  // CHECK-NEXT:  [[RESULT:%.*]] = alloc_stack $Any
  // CHECK-NEXT:  [[INIT:%.*]] = init_existential_addr [[RESULT]] : $*Any
  // CHECK-NEXT:  store [[OPENED_SELF]] to [init] [[INIT]]
  // CHECK-NEXT:  [[BORROWED_SELF_COPY:%.*]] = begin_borrow [[SELF_COPY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[METHOD:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC15methodTakingAnyyyp1a_tF
  // CHECK-NEXT:  apply [[METHOD]]([[RESULT]], [[BORROWED_SELF_COPY]])
  // CHECK-NEXT:  end_borrow [[BORROWED_SELF_COPY]] from [[SELF_COPY]]
  // CHECK-NEXT:  dealloc_stack [[RESULT]]
  // CHECK-NEXT:  destroy_value [[SELF_COPY]]
  // CHECK-NEXT:  return

  func methodTakingOptionalAny(a: Any?) {}
  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC23methodTakingOptionalAnyyypSg1a_tF

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC23methodTakingOptionalAnyyypSg1a_tFTo

  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC017methodTakingBlockH3AnyyyypcF : $@convention(method) (@owned @noescape @callee_guaranteed (@in Any) -> (), @guaranteed SwiftIdLover) -> ()

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC017methodTakingBlockH3AnyyyypcFTo : $@convention(objc_method) (@convention(block) @noescape (AnyObject) -> (), SwiftIdLover) -> ()
  // CHECK:    bb0([[BLOCK:%.*]] : @unowned $@convention(block) @noescape (AnyObject) -> (), [[SELF:%.*]] : @unowned $SwiftIdLover):
  // CHECK-NEXT:  [[BLOCK_COPY:%.*]] = copy_block [[BLOCK]]
  // CHECK-NEXT:  [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK:       [[THUNK_FN:%.*]] = function_ref @_T0yXlIyBy_ypIgi_TR
  // CHECK-NEXT:  [[THUNK:%.*]] = partial_apply [callee_guaranteed] [[THUNK_FN]]([[BLOCK_COPY]])
  // CHECK-NEXT:  [[THUNK_CVT:%.*]] = convert_function [[THUNK]]
  // CHECK:       [[BORROWED_SELF_COPY:%.*]] = begin_borrow [[SELF_COPY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[METHOD:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC017methodTakingBlockH3AnyyyypcF
  // CHECK-NEXT:  [[RESULT:%.*]] = apply [[METHOD]]([[THUNK_CVT]], [[BORROWED_SELF_COPY]])
  // CHECK-NEXT:  end_borrow [[BORROWED_SELF_COPY]] from [[SELF_COPY]]
  // CHECK-NEXT:  destroy_value [[SELF_COPY]]
  // CHECK-NEXT:  return [[RESULT]]

  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0yXlIyBy_ypIgi_TR
  // CHECK:     bb0([[ANY:%.*]] : @trivial $*Any, [[BLOCK:%.*]] : @guaranteed $@convention(block) @noescape (AnyObject) -> ()):
  // CHECK-NEXT:  [[OPENED_ANY:%.*]] = open_existential_addr immutable_access [[ANY]] : $*Any to $*[[OPENED_TYPE:@opened.*Any]],
	// CHECK:   [[TMP:%.*]] = alloc_stack
  // CHECK:   copy_addr [[OPENED_ANY]] to [initialization] [[TMP]]
  // CHECK-NEXT:  // function_ref _bridgeAnythingToObjectiveC
  // CHECK-NEXT:  [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK-NEXT:  [[BRIDGED:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
  // CHECK-NEXT:  apply [[BLOCK]]([[BRIDGED]])
  // CHECK-NEXT:  [[VOID:%.*]] = tuple ()
  // CHECK-NEXT:  destroy_value [[BRIDGED]]
	// CHECK-NEXT: dealloc_stack [[TMP]]
  // CHECK-NEXT:  destroy_addr [[ANY]]
  // CHECK-NEXT:  return [[VOID]]

  @objc func methodTakingBlockTakingAny(_: (Any) -> ()) {}

  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC29methodReturningBlockTakingAnyyypcyF : $@convention(method) (@guaranteed SwiftIdLover) -> @owned @callee_guaranteed (@in Any) -> ()

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC29methodReturningBlockTakingAnyyypcyFTo : $@convention(objc_method) (SwiftIdLover) -> @autoreleased @convention(block) (AnyObject) -> ()
  // CHECK:     bb0([[SELF:%.*]] : @unowned $SwiftIdLover):
  // CHECK-NEXT:  [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK-NEXT:  [[BORROWED_SELF_COPY:%.*]] = begin_borrow [[SELF_COPY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[METHOD:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC29methodReturningBlockTakingAnyyypcyF
  // CHECK-NEXT:  [[RESULT:%.*]] = apply [[METHOD:%.*]]([[BORROWED_SELF_COPY]])
  // CHECK-NEXT:  end_borrow [[BORROWED_SELF_COPY]] from [[SELF_COPY]]
  // CHECK-NEXT:  destroy_value [[SELF_COPY]]
  // CHECK-NEXT:  [[BLOCK_STORAGE:%.*]] = alloc_stack $@block_storage @callee_guaranteed (@in Any) -> ()
  // CHECK-NEXT:  [[BLOCK_STORAGE_ADDR:%.*]] = project_block_storage [[BLOCK_STORAGE]]
  // CHECK-NEXT:  store [[RESULT:%.*]] to [init] [[BLOCK_STORAGE_ADDR]]
  // CHECK:       [[THUNK_FN:%.*]] = function_ref @_T0ypIegi_yXlIeyBy_TR
  // CHECK-NEXT:  [[BLOCK_HEADER:%.*]] = init_block_storage_header [[BLOCK_STORAGE]] : $*@block_storage @callee_guaranteed (@in Any) -> (), invoke [[THUNK_FN]]
  // CHECK-NEXT:  [[BLOCK:%.*]] = copy_block [[BLOCK_HEADER]]
  // CHECK-NEXT:  destroy_addr [[BLOCK_STORAGE_ADDR]]
  // CHECK-NEXT:  dealloc_stack [[BLOCK_STORAGE]]
  // CHECK-NEXT:  return [[BLOCK]]

  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0ypIegi_yXlIeyBy_TR : $@convention(c) (@inout_aliasable @block_storage @callee_guaranteed (@in Any) -> (), AnyObject) -> ()
  // CHECK:     bb0([[BLOCK_STORAGE:%.*]] : @trivial $*@block_storage @callee_guaranteed (@in Any) -> (), [[ANY:%.*]] : @unowned $AnyObject):
  // CHECK-NEXT:  [[BLOCK_STORAGE_ADDR:%.*]] = project_block_storage [[BLOCK_STORAGE]]
  // CHECK-NEXT:  [[FUNCTION:%.*]] = load [copy] [[BLOCK_STORAGE_ADDR]]
  // CHECK-NEXT:  [[ANY_COPY:%.*]] = copy_value [[ANY]]
  // CHECK-NEXT:  [[OPENED_ANY:%.*]] = open_existential_ref [[ANY_COPY]]
  // CHECK-NEXT:  [[RESULT:%.*]] = alloc_stack $Any
  // CHECK-NEXT:  [[INIT:%.*]] = init_existential_addr [[RESULT]] : $*Any
  // CHECK-NEXT:  store [[OPENED_ANY]] to [init] [[INIT]]
  // CHECK-NEXT:  [[BORROW_FUN:%.*]] =  begin_borrow [[FUNCTION]]
  // CHECK-NEXT:  apply [[BORROW_FUN]]([[RESULT]])
  // CHECK-NEXT:  end_borrow [[BORROW_FUN]] from [[FUNCTION]]
  // CHECK-NEXT:  [[VOID:%.*]] = tuple ()
  // CHECK-NEXT:  dealloc_stack [[RESULT]]
  // CHECK-NEXT:   destroy_value [[FUNCTION]]
  // CHECK-NEXT:  return [[VOID]] : $()

  @objc func methodTakingBlockTakingOptionalAny(_: (Any?) -> ()) {}

  @objc func methodReturningBlockTakingAny() -> ((Any) -> ()) {}

  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC29methodTakingBlockReturningAnyyypycF : $@convention(method) (@owned @noescape @callee_guaranteed () -> @out Any, @guaranteed SwiftIdLover) -> () {

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC29methodTakingBlockReturningAnyyypycFTo : $@convention(objc_method) (@convention(block) @noescape () -> @autoreleased AnyObject, SwiftIdLover) -> ()
  // CHECK:     bb0([[BLOCK:%.*]] : @unowned $@convention(block) @noescape () -> @autoreleased AnyObject, [[ANY:%.*]] : @unowned $SwiftIdLover):
  // CHECK-NEXT:  [[BLOCK_COPY:%.*]] = copy_block [[BLOCK]]
  // CHECK-NEXT:  [[ANY_COPY:%.*]] = copy_value [[ANY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[THUNK_FN:%.*]] = function_ref @_T0yXlIyBa_ypIgr_TR
  // CHECK-NEXT:  [[THUNK:%.*]] = partial_apply [callee_guaranteed] [[THUNK_FN]]([[BLOCK_COPY]])
  // CHECK-NEXT:  [[THUNK_CVT:%.*]] = convert_function [[THUNK]]
  // CHECK-NEXT:  [[BORROWED_ANY_COPY:%.*]] = begin_borrow [[ANY_COPY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[METHOD:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC29methodTakingBlockReturningAnyyypycF
  // CHECK-NEXT:  [[RESULT:%.*]] = apply [[METHOD]]([[THUNK_CVT]], [[BORROWED_ANY_COPY]])
  // CHECK-NEXT:  end_borrow [[BORROWED_ANY_COPY]] from [[ANY_COPY]]
  // CHECK-NEXT:  destroy_value [[ANY_COPY]]
  // CHECK-NEXT:  return [[RESULT]]

  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0yXlIyBa_ypIgr_TR : $@convention(thin) (@guaranteed @convention(block) @noescape () -> @autoreleased AnyObject) -> @out Any
  // CHECK:     bb0([[ANY_ADDR:%.*]] : @trivial $*Any, [[BLOCK:%.*]] : @guaranteed $@convention(block) @noescape () -> @autoreleased AnyObject):
  // CHECK-NEXT:  [[BRIDGED:%.*]] = apply [[BLOCK]]()
  // CHECK-NEXT:  [[OPTIONAL:%.*]] = unchecked_ref_cast [[BRIDGED]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[BRIDGE_TO_ANY:%.*]] = function_ref [[BRIDGE_TO_ANY_FUNC:@.*]] :
  // CHECK-NEXT:  [[RESULT_VAL:%.*]] = apply [[BRIDGE_TO_ANY]]([[ANY_ADDR]], [[OPTIONAL]])
  // CHECK-NEXT:  [[EMPTY:%.*]] = tuple ()
  // CHECK-NEXT:  return [[EMPTY]]

  @objc func methodReturningBlockTakingOptionalAny() -> ((Any?) -> ()) {}

  @objc func methodTakingBlockReturningAny(_: () -> Any) {}

  // CHECK-LABEL: sil hidden @_T017objc_bridging_any12SwiftIdLoverC020methodReturningBlockH3AnyypycyF : $@convention(method) (@guaranteed SwiftIdLover) -> @owned @callee_guaranteed () -> @out Any

  // CHECK-LABEL: sil hidden [thunk] @_T017objc_bridging_any12SwiftIdLoverC020methodReturningBlockH3AnyypycyFTo : $@convention(objc_method) (SwiftIdLover) -> @autoreleased @convention(block) () -> @autoreleased AnyObject
  // CHECK:     bb0([[SELF:%.*]] : @unowned $SwiftIdLover):
  // CHECK-NEXT:  [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK-NEXT:  [[BORROWED_SELF_COPY:%.*]] = begin_borrow [[SELF_COPY]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[METHOD:%.*]] = function_ref @_T017objc_bridging_any12SwiftIdLoverC020methodReturningBlockH3AnyypycyF
  // CHECK-NEXT:  [[FUNCTION:%.*]] = apply [[METHOD]]([[BORROWED_SELF_COPY]])
  // CHECK-NEXT:  end_borrow [[BORROWED_SELF_COPY]] from [[SELF_COPY]]
  // CHECK-NEXT:  destroy_value [[SELF_COPY]]
  // CHECK-NEXT:  [[BLOCK_STORAGE:%.*]] = alloc_stack $@block_storage @callee_guaranteed () -> @out Any
  // CHECK-NEXT:  [[BLOCK_STORAGE_ADDR:%.*]] = project_block_storage [[BLOCK_STORAGE]]
  // CHECK-NEXT:  store [[FUNCTION]] to [init] [[BLOCK_STORAGE_ADDR]]
  // CHECK-NEXT:  // function_ref
  // CHECK-NEXT:  [[THUNK_FN:%.*]] = function_ref @_T0ypIegr_yXlIeyBa_TR
  // CHECK-NEXT:  [[BLOCK_HEADER:%.*]] = init_block_storage_header [[BLOCK_STORAGE]] : $*@block_storage @callee_guaranteed () -> @out Any, invoke [[THUNK_FN]]
  // CHECK-NEXT:  [[BLOCK:%.*]] = copy_block [[BLOCK_HEADER]]
  // CHECK-NEXT:  destroy_addr [[BLOCK_STORAGE_ADDR]]
  // CHECK-NEXT:  dealloc_stack [[BLOCK_STORAGE]]
  // CHECK-NEXT:  return [[BLOCK]]

  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0ypIegr_yXlIeyBa_TR : $@convention(c) (@inout_aliasable @block_storage @callee_guaranteed () -> @out Any) -> @autoreleased AnyObject
  // CHECK:     bb0(%0 : @trivial $*@block_storage @callee_guaranteed () -> @out Any):
  // CHECK-NEXT:  [[BLOCK_STORAGE_ADDR:%.*]] = project_block_storage %0
  // CHECK-NEXT:  [[FUNCTION:%.*]] = load [copy] [[BLOCK_STORAGE_ADDR]]
  // CHECK-NEXT:  [[RESULT:%.*]] = alloc_stack $Any
  // CHECK-NEXT:  [[BORROW_FUN:%.*]] = begin_borrow [[FUNCTION]]
  // CHECK-NEXT:  apply [[BORROW_FUN]]([[RESULT]])
  // CHECK-NEXT:  end_borrow [[BORROW_FUN]] from [[FUNCTION]]
  // CHECK-NEXT:  [[OPENED:%.*]] = open_existential_addr immutable_access [[RESULT]] : $*Any to $*[[OPENED_TYPE:@opened.*Any]],
  // CHECK:       [[TMP:%.*]] = alloc_stack $[[OPENED_TYPE]]
  // CHECK:       copy_addr [[OPENED]] to [initialization] [[TMP]]
  // CHECK-NEXT:  // function_ref _bridgeAnythingToObjectiveC
  // CHECK-NEXT:  [[BRIDGE_ANYTHING:%.*]] = function_ref
  // CHECK-NEXT:  [[BRIDGED:%.*]] = apply [[BRIDGE_ANYTHING]]<[[OPENED_TYPE]]>([[TMP]])
	// CHECK-NEXT:  dealloc_stack [[TMP]]
  // CHECK-NEXT:  destroy_addr [[RESULT]]
  // CHECK-NEXT:  dealloc_stack [[RESULT]]
  // CHECK-NEXT:  destroy_value [[FUNCTION]]
  // CHECK-NEXT:  return [[BRIDGED]]

  @objc func methodTakingBlockReturningOptionalAny(_: () -> Any?) {}

  @objc func methodReturningBlockReturningAny() -> (() -> Any) {}

  @objc func methodReturningBlockReturningOptionalAny() -> (() -> Any?) {}
  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0ypSgIegr_yXlSgIeyBa_TR
  // CHECK: function_ref @_T0s27_bridgeAnythingToObjectiveC{{.*}}F

  override init() { super.init() }
  @objc dynamic required convenience init(any: Any) { self.init() }
  @objc dynamic required convenience init(anyMaybe: Any?) { self.init() }
  @objc dynamic var anyProperty: Any
  @objc dynamic var maybeAnyProperty: Any?

  subscript(_: IndexForAnySubscript) -> Any { get {} set {} }

  @objc func methodReturningAnyOrError() throws -> Any {}
}

class IndexForAnySubscript {}

func dynamicLookup(x: AnyObject) {
  _ = x.anyProperty
  _ = x[IndexForAnySubscript()]
}

extension GenericClass {
  // CHECK-LABEL: sil hidden @_T0So12GenericClassC17objc_bridging_anyE23pseudogenericAnyErasureypx1x_tF :
  func pseudogenericAnyErasure(x: T) -> Any {
    // CHECK: bb0([[ANY_OUT:%.*]] : @trivial $*Any, [[ARG:%.*]] : @owned $T, [[SELF:%.*]] : @guaranteed $GenericClass<T>
    // CHECK:   [[ANY_BUF:%.*]] = init_existential_addr [[ANY_OUT]] : $*Any, $AnyObject
    // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
    // CHECK:   [[ARG_COPY:%.*]] = copy_value [[BORROWED_ARG]]
    // CHECK:   [[ANYOBJECT:%.*]] = init_existential_ref [[ARG_COPY]] : $T : $T, $AnyObject
    // CHECK:   store [[ANYOBJECT]] to [init] [[ANY_BUF]]
    // CHECK:   end_borrow [[BORROWED_ARG]] from [[ARG]]
    // CHECK:   destroy_value [[ARG]]
    return x
  }
  // CHECK: } // end sil function '_T0So12GenericClassC17objc_bridging_anyE23pseudogenericAnyErasureypx1x_tF'
}

// Make sure AnyHashable erasure marks Hashable conformance as used
class AnyHashableClass : NSObject {
  // CHECK-LABEL: sil hidden @_T017objc_bridging_any16AnyHashableClassC07returnsdE0s0dE0VyF
  // CHECK: [[FN:%.*]] = function_ref @_T0s21_convertToAnyHashables0cD0Vxs0D0RzlF
  // CHECK: apply [[FN]]<GenericOption>({{.*}})
  func returnsAnyHashable() -> AnyHashable {
    return GenericOption.multithreaded
  }
}

// CHECK-LABEL: sil hidden @_T017objc_bridging_any33bridgeOptionalFunctionToAnyObjectyXlyycSg2fn_tF : $@convention(thin) (@owned Optional<@callee_guaranteed () -> ()>) -> @owned AnyObject
// CHECK: [[BRIDGE:%.*]] = function_ref @_T0Sq19_bridgeToObjectiveCyXlyF
// CHECK: [[FN:%.*]] = function_ref @_T0Ieg_ytytIegir_TR
// CHECK: partial_apply [callee_guaranteed] [[FN]]
// CHECK: [[SELF:%.*]] = alloc_stack $Optional<@callee_guaranteed (@in ()) -> @out ()>
// CHECK: apply [[BRIDGE]]<() -> ()>([[SELF]])
func bridgeOptionalFunctionToAnyObject(fn: (() -> ())?) -> AnyObject {
  return fn as AnyObject
}

// CHECK-LABEL: sil_witness_table shared [serialized] GenericOption: Hashable module objc_generics {
// CHECK-NEXT: base_protocol Equatable: GenericOption: Equatable module objc_generics
// CHECK-NEXT: method #Hashable.hashValue!getter.1: {{.*}} : @_T0SC13GenericOptionVs8Hashable13objc_genericssACP9hashValueSivgTW
// CHECK-NEXT: }
