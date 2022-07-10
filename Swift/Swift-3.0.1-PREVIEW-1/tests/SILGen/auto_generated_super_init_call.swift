// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

// Test that we emit a call to super.init at the end of the initializer, when none has been previously added.

class Parent { 
  init() {}
}

class SomeDerivedClass : Parent {
  var y: Int
  func foo() {}

  override init() {
    y = 42
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call16SomeDerivedClassc{{.*}} : $@convention(method) (@owned SomeDerivedClass) -> @owned SomeDerivedClass
// CHECK: integer_literal $Builtin.Int2048, 42
// CHECK: [[SELFLOAD:%[0-9]+]] = load [[SELF:%[0-9]+]] : $*SomeDerivedClass
// CHECK-NEXT: [[PARENT:%[0-9]+]] = upcast [[SELFLOAD]] : $SomeDerivedClass to $Parent
// CHECK: [[INITCALL1:%[0-9]+]] = function_ref @_TFC30auto_generated_super_init_call6ParentcfT_S0_ : $@convention(method) (@owned Parent) -> @owned Parent
// CHECK-NEXT: [[RES1:%[0-9]+]] = apply [[INITCALL1]]([[PARENT]])
// CHECK-NEXT: [[DOWNCAST:%[0-9]+]] = unchecked_ref_cast [[RES1]] : $Parent to $SomeDerivedClass
// CHECK-NEXT: store [[DOWNCAST]] to [[SELF]] : $*SomeDerivedClass 
  }
  
  init(x: Int) {
    y = x
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call16SomeDerivedClassc{{.*}} : $@convention(method) (Int, @owned SomeDerivedClass) -> @owned SomeDerivedClass
// CHECK: function_ref @_TFC30auto_generated_super_init_call6ParentcfT_S0_ : $@convention(method) (@owned Parent) -> @owned Parent
  }

  init(b: Bool) {
    if b {
      y = 0
      return
    } else {
      y = 10
    }
    return
// Check that we are emitting the super.init expr into the epilog block.
    
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call16SomeDerivedClassc{{.*}} : $@convention(method) (Bool, @owned SomeDerivedClass) -> @owned SomeDerivedClass    
// CHECK: bb4:
// CHECK: [[SELFLOAD:%[0-9]+]] = load [[SELF:%[0-9]+]] : $*SomeDerivedClass
// CHECK: function_ref @_TFC30auto_generated_super_init_call6ParentcfT_S0_ : $@convention(method) (@owned Parent) -> @owned Parent,
// CHECK-NEXT: apply
// CHECK-NEXT: unchecked_ref_cast
// CHECK-NEXT: store
// CHECK-NEXT: load
// CHECK-NEXT: strong_retain
// CHECK-NEXT: strong_release
// CHECK-NEXT: return
  }

  // One init has a call to super init. Make sure we don't insert more than one.
  init(b: Bool, i: Int) {
    if (b) {
      y = i
    } else {
      y = 0
    }
      
    super.init()
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call16SomeDerivedClassc{{.*}} : $@convention(method) (Bool, Int, @owned SomeDerivedClass) -> @owned SomeDerivedClass    
// CHECK: function_ref @_TFC30auto_generated_super_init_call6ParentcfT_S0_ : $@convention(method) (@owned Parent) -> @owned Parent
// CHECK: return
  }
}

// Check that we do call super.init.
class HasNoIVars : Parent {
  override init() {
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call10HasNoIVarsc{{.*}} : $@convention(method) (@owned HasNoIVars) -> @owned HasNoIVars
// CHECK: function_ref @_TFC30auto_generated_super_init_call6ParentcfT_S0_ : $@convention(method) (@owned Parent) -> @owned Parent
  }
}

// Check that we don't call super.init.
class ParentLess {
  var y: Int
  init() {
    y = 0
  }
}

class Grandparent {
  init() {}
}
// This should have auto-generated default initializer.
class ParentWithNoExplicitInit : Grandparent {
}
// Check that we add a call to super.init.
class ChildOfParentWithNoExplicitInit : ParentWithNoExplicitInit {
  var y: Int
  override init() {
    y = 10
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call31ChildOfParentWithNoExplicitInitc
// CHECK: function_ref @_TFC30auto_generated_super_init_call24ParentWithNoExplicitInitcfT_S0_ : $@convention(method) (@owned ParentWithNoExplicitInit) -> @owned ParentWithNoExplicitInit
  }
}

// This should have auto-generated default initializer.
class ParentWithNoExplicitInit2 : Grandparent {
  var i: Int = 0
}
// Check that we add a call to super.init.
class ChildOfParentWithNoExplicitInit2 : ParentWithNoExplicitInit2 {
  var y: Int
  override init() {
    y = 10
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call32ChildOfParentWithNoExplicitInit2c
// CHECK: function_ref @_TFC30auto_generated_super_init_call25ParentWithNoExplicitInit2cfT_S0_ : $@convention(method) (@owned ParentWithNoExplicitInit2) -> @owned ParentWithNoExplicitInit2
  }
}

// Do not insert the call nor warn - the user should call init(5).
class ParentWithNoDefaultInit {
  var i: Int
  init(x: Int) {
    i = x
  }
}
class ChildOfParentWithNoDefaultInit : ParentWithNoDefaultInit {
  var y: Int
  init() {
// CHECK-LABEL: sil hidden @_TFC30auto_generated_super_init_call30ChildOfParentWithNoDefaultInitc{{.*}} : $@convention(method) (@owned ChildOfParentWithNoDefaultInit) -> @owned ChildOfParentWithNoDefaultInit
// CHECK: bb0
// CHECK-NOT: apply
// CHECK: return
  }
}
