// RUN: %swift -target x86_64-apple-macosx10.9 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s
// RUN: %swift -target i386-apple-ios7.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target x86_64-apple-ios7.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s
// RUN: %swift -target i386-apple-tvos9.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target x86_64-apple-tvos9.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s
// RUN: %swift -target i386-apple-watchos2.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target armv7-apple-ios7.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target arm64-apple-ios7.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s
// RUN: %swift -target armv7-apple-tvos9.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target arm64-apple-tvos9.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s
// RUN: %swift -target armv7k-apple-watchos2.0 -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-32 %s
// RUN: %swift -target x86_64-unknown-linux-gnu -disable-objc-interop -emit-ir -parse-stdlib -primary-file %s | %FileCheck --check-prefix=CHECK --check-prefix=CHECK-64 %s

// REQUIRES: X86

// CHECK: define hidden %swift.type* [[GENERIC_TYPEOF:@_TF17generic_metatypes13genericTypeof.*]](%swift.opaque* noalias nocapture, %swift.type* [[TYPE:%.*]])
func genericTypeof<T>(_ x: T) -> T.Type {
  // CHECK: [[METATYPE:%.*]] = call %swift.type* @swift_getDynamicType(%swift.opaque* {{.*}}, %swift.type* [[TYPE]])
  // CHECK: ret %swift.type* [[METATYPE]]
  return type(of: x)
}

struct Foo {}
class Bar {}

// CHECK: define hidden %swift.type* @_TF17generic_metatypes27remapToSubstitutedMetatypes{{.*}}(%C17generic_metatypes3Bar*) {{.*}} {
func remapToSubstitutedMetatypes(_ x: Foo, y: Bar)
  -> (Foo.Type, Bar.Type)
{
  // CHECK: call %swift.type* [[GENERIC_TYPEOF]](%swift.opaque* noalias nocapture undef, %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}})
  // CHECK: [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
  // CHECK: [[BAR_META:%.*]] = call %swift.type* [[GENERIC_TYPEOF]](%swift.opaque* noalias nocapture {{%.*}}, %swift.type* [[T0]])
  // CHECK: ret %swift.type* [[BAR_META]]
  return (genericTypeof(x), genericTypeof(y))
}


// CHECK: define hidden void @_TF17generic_metatypes23remapToGenericMetatypesFT_T_()
func remapToGenericMetatypes() {
  // CHECK: [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
  // CHECK: call void @_TF17generic_metatypes16genericMetatypes{{.*}}(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}} %swift.type* [[T0]], %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}} %swift.type* [[T0]])
  genericMetatypes(Foo.self, Bar.self)
}

func genericMetatypes<T, U>(_ t: T.Type, _ u: U.Type) {}

protocol Bas {}

// CHECK: define hidden { %swift.type*, i8** } @_TF17generic_metatypes14protocolTypeof{{.*}}(%P17generic_metatypes3Bas_* noalias nocapture dereferenceable({{.*}}))
func protocolTypeof(_ x: Bas) -> Bas.Type {
  // CHECK: [[METADATA_ADDR:%.*]] = getelementptr inbounds %P17generic_metatypes3Bas_, %P17generic_metatypes3Bas_* [[X:%.*]], i32 0, i32 1
  // CHECK: [[METADATA:%.*]] = load %swift.type*, %swift.type** [[METADATA_ADDR]]
  // CHECK: [[BUFFER:%.*]] = getelementptr inbounds %P17generic_metatypes3Bas_, %P17generic_metatypes3Bas_* [[X]], i32 0, i32 0
  // CHECK: [[METADATA_I8:%.*]] = bitcast %swift.type* [[METADATA]] to i8***
  // CHECK-32: [[VW_ADDR:%.*]] = getelementptr inbounds i8**, i8*** [[METADATA_I8]], i32 -1
  // CHECK-64: [[VW_ADDR:%.*]] = getelementptr inbounds i8**, i8*** [[METADATA_I8]], i64 -1
  // CHECK: [[VW:%.*]] = load i8**, i8*** [[VW_ADDR]]
  // CHECK: [[PROJECT_ADDR:%.*]] = getelementptr inbounds i8*, i8** [[VW]], i32 2
  // CHECK-32: [[PROJECT_PTR:%.*]] = load i8*, i8** [[PROJECT_ADDR]], align 4
  // CHECK-64: [[PROJECT_PTR:%.*]] = load i8*, i8** [[PROJECT_ADDR]], align 8
  // CHECK-32: [[PROJECT:%.*]] = bitcast i8* [[PROJECT_PTR]] to %swift.opaque* ([12 x i8]*, %swift.type*)*
  // CHECK-64: [[PROJECT:%.*]] = bitcast i8* [[PROJECT_PTR]] to %swift.opaque* ([24 x i8]*, %swift.type*)*
  // CHECK-32: [[PROJECTION:%.*]] = call %swift.opaque* [[PROJECT]]([12 x i8]* [[BUFFER]], %swift.type* [[METADATA]])
  // CHECK-64: [[PROJECTION:%.*]] = call %swift.opaque* [[PROJECT]]([24 x i8]* [[BUFFER]], %swift.type* [[METADATA]])
  // CHECK: [[METATYPE:%.*]] = call %swift.type* @swift_getDynamicType(%swift.opaque* [[PROJECTION]], %swift.type* [[METADATA]])
  // CHECK: [[T0:%.*]] = getelementptr inbounds %P17generic_metatypes3Bas_, %P17generic_metatypes3Bas_* [[X]], i32 0, i32 2
  // CHECK-32: [[WTABLE:%.*]] = load i8**, i8*** [[T0]], align 4
  // CHECK-64: [[WTABLE:%.*]] = load i8**, i8*** [[T0]], align 8
  // CHECK: [[T0:%.*]] = insertvalue { %swift.type*, i8** } undef, %swift.type* [[METATYPE]], 0
  // CHECK: [[T1:%.*]] = insertvalue { %swift.type*, i8** } [[T0]], i8** [[WTABLE]], 1
  // CHECK: ret { %swift.type*, i8** } [[T1]]
  return type(of: x)
}

struct Zim : Bas {}
class Zang : Bas {}

// CHECK-LABEL: define hidden { %swift.type*, i8** } @_TF17generic_metatypes15metatypeErasureFMVS_3ZimPMPS_3Bas_() #0
func metatypeErasure(_ z: Zim.Type) -> Bas.Type {
  // CHECK: ret { %swift.type*, i8** } {{.*}} @_TMfV17generic_metatypes3Zim, {{.*}} @_TWPV17generic_metatypes3ZimS_3BasS_
  return z
}

// CHECK-LABEL: define hidden { %swift.type*, i8** } @_TF17generic_metatypes15metatypeErasureFMCS_4ZangPMPS_3Bas_(%swift.type*) #0
func metatypeErasure(_ z: Zang.Type) -> Bas.Type {
  // CHECK: [[RET:%.*]] = insertvalue { %swift.type*, i8** } undef, %swift.type* %0, 0
  // CHECK: [[RET2:%.*]] = insertvalue { %swift.type*, i8** } [[RET]], i8** getelementptr inbounds ([0 x i8*], [0 x i8*]* @_TWPC17generic_metatypes4ZangS_3BasS_, i32 0, i32 0), 1
  // CHECK: ret { %swift.type*, i8** } [[RET2]]
  return z
}

struct OneArg<T> {}
struct TwoArgs<T, U> {}
struct ThreeArgs<T, U, V> {}
struct FourArgs<T, U, V, W> {}
struct FiveArgs<T, U, V, W, X> {}

func genericMetatype<A>(_ x: A.Type) {}

// CHECK-LABEL: define hidden void @_TF17generic_metatypes20makeGenericMetatypesFT_T_() {{.*}} {
func makeGenericMetatypes() {
  // CHECK: call %swift.type* @_TMaGV17generic_metatypes6OneArgVS_3Foo_() [[NOUNWIND_READNONE:#[0-9]+]]
  genericMetatype(OneArg<Foo>.self)

  // CHECK: call %swift.type* @_TMaGV17generic_metatypes7TwoArgsVS_3FooCS_3Bar_() [[NOUNWIND_READNONE]]
  genericMetatype(TwoArgs<Foo, Bar>.self)

  // CHECK: call %swift.type* @_TMaGV17generic_metatypes9ThreeArgsVS_3FooCS_3BarS1__() [[NOUNWIND_READNONE]]
  genericMetatype(ThreeArgs<Foo, Bar, Foo>.self)

  // CHECK: call %swift.type* @_TMaGV17generic_metatypes8FourArgsVS_3FooCS_3BarS1_S2__() [[NOUNWIND_READNONE]]
  genericMetatype(FourArgs<Foo, Bar, Foo, Bar>.self)

  // CHECK: call %swift.type* @_TMaGV17generic_metatypes8FiveArgsVS_3FooCS_3BarS1_S2_S1__() [[NOUNWIND_READNONE]]
  genericMetatype(FiveArgs<Foo, Bar, Foo, Bar, Foo>.self)
}

// CHECK: define linkonce_odr hidden %swift.type* @_TMaGV17generic_metatypes6OneArgVS_3Foo_() [[NOUNWIND_READNONE_OPT:#[0-9]+]]
// CHECK:   call %swift.type* @_TMaV17generic_metatypes6OneArg(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}) [[NOUNWIND_READNONE:#[0-9]+]]

// CHECK-LABEL: define hidden %swift.type* @_TMaV17generic_metatypes6OneArg(%swift.type*)
// CHECK:   [[BUFFER:%.*]] = alloca { %swift.type* }
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.start
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type* }, { %swift.type* }* [[BUFFER]], i32 0, i32 0
// CHECK:   store %swift.type* %0, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type* }* [[BUFFER]] to i8*
// CHECK:   [[METADATA:%.*]] = call %swift.type* @rt_swift_getGenericMetadata(%swift.type_pattern* {{.*}} @_TMPV17generic_metatypes6OneArg {{.*}}, i8* [[BUFFER_PTR]])
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.end
// CHECK:   ret %swift.type* [[METADATA]]

// CHECK: define linkonce_odr hidden %swift.type* @_TMaGV17generic_metatypes7TwoArgsVS_3FooCS_3Bar_() [[NOUNWIND_READNONE_OPT]]
// CHECK:   [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
// CHECK:   call %swift.type* @_TMaV17generic_metatypes7TwoArgs(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]])

// CHECK-LABEL: define hidden %swift.type* @_TMaV17generic_metatypes7TwoArgs(%swift.type*, %swift.type*)
// CHECK:   [[BUFFER:%.*]] = alloca { %swift.type*, %swift.type* }
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.start
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type* }, { %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 0
// CHECK:   store %swift.type* %0, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type* }, { %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 1
// CHECK:   store %swift.type* %1, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   [[METADATA:%.*]] = call %swift.type* @rt_swift_getGenericMetadata(%swift.type_pattern* {{.*}} @_TMPV17generic_metatypes7TwoArgs {{.*}}, i8* [[BUFFER_PTR]])
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.end
// CHECK:   ret %swift.type* [[METADATA]]

// CHECK: define linkonce_odr hidden %swift.type* @_TMaGV17generic_metatypes9ThreeArgsVS_3FooCS_3BarS1__() [[NOUNWIND_READNONE_OPT]]
// CHECK:   [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
// CHECK:   call %swift.type* @_TMaV17generic_metatypes9ThreeArgs(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]], %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}) [[NOUNWIND_READNONE]]

// CHECK-LABEL: define hidden %swift.type* @_TMaV17generic_metatypes9ThreeArgs(%swift.type*, %swift.type*, %swift.type*)
// CHECK:   [[BUFFER:%.*]] = alloca { %swift.type*, %swift.type*, %swift.type* }
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.start
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 0
// CHECK:   store %swift.type* %0, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 1
// CHECK:   store %swift.type* %1, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 2
// CHECK:   store %swift.type* %2, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   [[METADATA:%.*]] = call %swift.type* @rt_swift_getGenericMetadata(%swift.type_pattern* {{.*}} @_TMPV17generic_metatypes9ThreeArgs {{.*}}, i8* [[BUFFER_PTR]])
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.end
// CHECK:   ret %swift.type* [[METADATA]]

// CHECK: define linkonce_odr hidden %swift.type* @_TMaGV17generic_metatypes8FourArgsVS_3FooCS_3BarS1_S2__() [[NOUNWIND_READNONE_OPT]]
// CHECK:   [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
// CHECK:   call %swift.type* @_TMaV17generic_metatypes8FourArgs(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]], %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]]) [[NOUNWIND_READNONE]]

// CHECK-LABEL: define hidden %swift.type* @_TMaV17generic_metatypes8FourArgs(%swift.type*, %swift.type*, %swift.type*, %swift.type*)
// CHECK:   [[BUFFER:%.*]] = alloca { %swift.type*, %swift.type*, %swift.type*, %swift.type* }
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.start
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 0
// CHECK:   store %swift.type* %0, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 1
// CHECK:   store %swift.type* %1, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 2
// CHECK:   store %swift.type* %2, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 3
// CHECK:   store %swift.type* %3, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   [[METADATA:%.*]] = call %swift.type* @rt_swift_getGenericMetadata(%swift.type_pattern* {{.*}} @_TMPV17generic_metatypes8FourArgs {{.*}}, i8* [[BUFFER_PTR]])
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.end
// CHECK:   ret %swift.type* [[METADATA]]

// CHECK: define linkonce_odr hidden %swift.type* @_TMaGV17generic_metatypes8FiveArgsVS_3FooCS_3BarS1_S2_S1__() [[NOUNWIND_READNONE_OPT]]
// CHECK:   [[T0:%.*]] = call %swift.type* @_TMaC17generic_metatypes3Bar()
// CHECK:   call %swift.type* @_TMaV17generic_metatypes8FiveArgs(%swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]], %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}, %swift.type* [[T0]], %swift.type* {{.*}} @_TMfV17generic_metatypes3Foo, {{.*}}) [[NOUNWIND_READNONE]]

// CHECK-LABEL: define hidden %swift.type* @_TMaV17generic_metatypes8FiveArgs(%swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type*)
// CHECK:   [[BUFFER:%.*]] = alloca { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.start
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 0
// CHECK:   store %swift.type* %0, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 1
// CHECK:   store %swift.type* %1, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 2
// CHECK:   store %swift.type* %2, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_ELT:%.*]] = getelementptr inbounds { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }, { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]], i32 0, i32 3
// CHECK:   store %swift.type* %3, %swift.type** [[BUFFER_ELT]]
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   [[METADATA:%.*]] = call %swift.type* @rt_swift_getGenericMetadata(%swift.type_pattern* {{.*}} @_TMPV17generic_metatypes8FiveArgs {{.*}}, i8* [[BUFFER_PTR]])
// CHECK:   [[BUFFER_PTR:%.*]] = bitcast { %swift.type*, %swift.type*, %swift.type*, %swift.type*, %swift.type* }* [[BUFFER]] to i8*
// CHECK:   call void @llvm.lifetime.end
// CHECK:   ret %swift.type* [[METADATA]]

// CHECK: attributes [[NOUNWIND_READNONE_OPT]] = { nounwind readnone "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "target-cpu"
// CHECK: attributes [[NOUNWIND_READNONE]] = { nounwind readnone }
