:orphan:

.. @raise litre.TestsAreMissing
.. _ABI:

.. highlight:: none

Mangling
--------
::

  mangled-name ::= '_S' global

All Swift-mangled names begin with this prefix.

The basic mangling scheme is a list of 'operators' where the operators are
structured in a post-fix order. For example the mangling may start with an
identifier but only later in the mangling a type-like operator defines how this
identifier has to be interpreted::

  4Test3FooC   // The trailing 'C' says that 'Foo' is a class in module 'Test'



Operators are either identifiers or a sequence of one or more characters,
like ``C`` for class.
All operators share the same name-space. Important operators are a single
character, which means that no other operator may start with the same
character.

Some less important operators are longer and may also contain one or more
natural numbers. But it's always important that the demangler can identify the
end (the last character) of an operator. For example, it's not possible to
determine the last character if there are two operators ``M`` and ``Ma``:
``a`` could belong to ``M`` or it could be the first character of the next
operator.

The intention of the post-fix order is to optimize for common pre-fixes.
Regardless, if it's the mangling for a metatype or a function in a module, the
mangled name will start with the module name (after the ``_S``).

In the following, productions which are only _part_ of an operator, are
named with uppercase letters.

Globals
~~~~~~~

::

  global ::= type 'N'                    // type metadata (address point)
                                         // -- type starts with [BCOSTV]
  global ::= type 'Mf'                   // 'full' type metadata (start of object)
  global ::= type 'MP'                   // type metadata pattern
  global ::= type 'Ma'                   // type metadata access function
  global ::= type 'ML'                   // type metadata lazy cache variable
  global ::= nominal-type 'Mm'           // class metaclass
  global ::= nominal-type 'Mn'           // nominal type descriptor
  global ::= protocol 'Mp'               // protocol descriptor
  global ::= type 'MF'                   // metadata for remote mirrors: field descriptor
  global ::= type 'MB'                   // metadata for remote mirrors: builtin type descriptor
  global ::= protocol-conformance 'MA'   // metadata for remote mirrors: associated type descriptor
  global ::= nominal-type 'MC'           // metadata for remote mirrors: superclass descriptor

  // TODO check this::
  global ::= mangled-name 'TA'                     // partial application forwarder
  global ::= mangled-name 'Ta'                     // ObjC partial application forwarder

  global ::= type 'w' VALUE-WITNESS-KIND // value witness
  global ::= protocol-conformance 'Wa'   // protocol witness table accessor
  global ::= protocol-conformance 'WG'   // generic protocol witness table
  global ::= protocol-conformance 'WI'   // generic protocol witness table instantiation function
  global ::= type protocol-conformance 'WL'   // lazy protocol witness table cache variable
  global ::= entity 'Wo'                 // witness table offset
  global ::= protocol-conformance 'WP'   // protocol witness table

  global ::= protocol-conformance identifier 'Wt' // associated type metadata accessor
  global ::= protocol-conformance assoc_type_path nominal-type 'WT' // associated type witness table accessor
  global ::= type protocol-conformance 'Wl' // lazy protocol witness table accessor
  global ::= type 'WV'                   // value witness table
  global ::= entity 'Wv' DIRECTNESS      // field offset

  global ::= type 'Wy' // Outlined Copy Function Type
  global ::= type 'We' // Outlined Consume Function Type
  global ::= type 'Wr' // Outlined Retain Function Type
  global ::= type 'Ws' // Outlined Release Function Type
  global ::= type 'Wb' INDEX // Outlined InitializeWithTake Function Type
  global ::= type 'Wc' INDEX // Outlined InitializeWithCopy Function Type
  global ::= type 'Wd' INDEX // Outlined AssignWithTake Function Type
  global ::= type 'Wf' INDEX // Outlined AssignWithCopy Function Type
  global ::= type 'Wh' INDEX // Outlined Destroy Function Type

  assoc_type_path ::= identifier '_' identifier*

  DIRECTNESS ::= 'd'                         // direct
  DIRECTNESS ::= 'i'                         // indirect

A direct symbol resolves directly to the address of an object.  An
indirect symbol resolves to the address of a pointer to the object.
They are distinct manglings to make a certain class of bugs
immediately obvious.

The terminology is slightly overloaded when discussing offsets.  A
direct offset resolves to a variable holding the true offset.  An
indirect offset resolves to a variable holding an offset to be applied
to type metadata to get the address of the true offset.  (Offset
variables are required when the object being accessed lies within a
resilient structure.  When the layout of the object may depend on
generic arguments, these offsets must be kept in metadata.  Indirect
field offsets are therefore required when accessing fields in generic
types where the metadata itself has unknown layout.)

::

  global ::= global 'TO'                 // ObjC-as-swift thunk
  global ::= global 'To'                 // swift-as-ObjC thunk
  global ::= global 'TD'                 // dynamic dispatch thunk
  global ::= global 'Td'                 // direct method reference thunk
  global ::= entity entity 'TV'          // vtable override thunk, derived followed by base
  global ::= type 'D'                    // type mangling for the debugger. TODO: check if we really need this
  global ::= protocol-conformance entity 'TW' // protocol witness thunk
  global ::= context identifier identifier 'TB' // property behavior initializer thunk (not used currently)
  global ::= context identifier identifier 'Tb' // property behavior setter thunk (not used currently)
  global ::= global specialization       // function specialization
  global ::= global 'Tm'                 // merged function
  global ::= entity                      // some identifiable thing
  global ::= type type generic-signature? 'T' REABSTRACT-THUNK-TYPE   // reabstraction thunk helper function
  global ::= entity generic-signature? type type* 'TK' // key path getter
  global ::= entity generic-signature? type type* 'Tk' // key path setter
  global ::= type generic-signature 'TH' // key path equality
  global ::= type generic-signature 'Th' // key path hasher

  REABSTRACT-THUNK-TYPE ::= 'R'          // reabstraction thunk helper function
  REABSTRACT-THUNK-TYPE ::= 'r'          // reabstraction thunk

The types in a reabstraction thunk helper function are always non-polymorphic
``<impl-function-type>`` types.

::

  VALUE-WITNESS-KIND ::= 'al'           // allocateBuffer
  VALUE-WITNESS-KIND ::= 'ca'           // assignWithCopy
  VALUE-WITNESS-KIND ::= 'ta'           // assignWithTake
  VALUE-WITNESS-KIND ::= 'de'           // deallocateBuffer
  VALUE-WITNESS-KIND ::= 'xx'           // destroy
  VALUE-WITNESS-KIND ::= 'XX'           // destroyBuffer
  VALUE-WITNESS-KIND ::= 'Xx'           // destroyArray
  VALUE-WITNESS-KIND ::= 'CP'           // initializeBufferWithCopyOfBuffer
  VALUE-WITNESS-KIND ::= 'Cp'           // initializeBufferWithCopy
  VALUE-WITNESS-KIND ::= 'cp'           // initializeWithCopy
  VALUE-WITNESS-KIND ::= 'TK'           // initializeBufferWithTakeOfBuffer
  VALUE-WITNESS-KIND ::= 'Tk'           // initializeBufferWithTake
  VALUE-WITNESS-KIND ::= 'tk'           // initializeWithTake
  VALUE-WITNESS-KIND ::= 'pr'           // projectBuffer
  VALUE-WITNESS-KIND ::= 'xs'           // storeExtraInhabitant
  VALUE-WITNESS-KIND ::= 'xg'           // getExtraInhabitantIndex
  VALUE-WITNESS-KIND ::= 'Cc'           // initializeArrayWithCopy
  VALUE-WITNESS-KIND ::= 'Tt'           // initializeArrayWithTakeFrontToBack
  VALUE-WITNESS-KIND ::= 'tT'           // initializeArrayWithTakeBackToFront
  VALUE-WITNESS-KIND ::= 'ug'           // getEnumTag
  VALUE-WITNESS-KIND ::= 'up'           // destructiveProjectEnumData
  VALUE-WITNESS-KIND ::= 'ui'           // destructiveInjectEnumTag

``<VALUE-WITNESS-KIND>`` differentiates the kinds of value
witness functions for a type.

Entities
~~~~~~~~

::

  entity ::= nominal-type                    // named type declaration
  entity ::= context entity-spec static? curry-thunk?

  static ::= 'Z'
  curry-thunk ::= 'Tc'

  // The leading type is the function type
  entity-spec ::= type file-discriminator? 'fC'      // allocating constructor
  entity-spec ::= type file-discriminator? 'fc'      // non-allocating constructor
  entity-spec ::= type 'fU' INDEX            // explicit anonymous closure expression
  entity-spec ::= type 'fu' INDEX            // implicit anonymous closure
  entity-spec ::= 'fA' INDEX                 // default argument N+1 generator
  entity-spec ::= 'fi'                       // non-local variable initializer
  entity-spec ::= 'fD'                       // deallocating destructor; untyped
  entity-spec ::= 'fd'                       // non-deallocating destructor; untyped
  entity-spec ::= 'fE'                       // ivar destroyer; untyped
  entity-spec ::= 'fe'                       // ivar initializer; untyped
  entity-spec ::= 'Tv' NATURAL               // outlined global variable (from context function)
  entity-spec ::= 'Te' bridge-spec           // outlined objective c method call

  entity-spec ::= decl-name function-signature generic-signature? 'F'    // function
  entity-spec ::= type file-discriminator? 'i' ACCESSOR                  // subscript
  entity-spec ::= decl-name type 'v' ACCESSOR                            // variable
  entity-spec ::= decl-name type 'fp'                // generic type parameter
  entity-spec ::= decl-name type 'fo'                // enum element (currently not used)

  ACCESSOR ::= 'm'                           // materializeForSet
  ACCESSOR ::= 's'                           // setter
  ACCESSOR ::= 'g'                           // getter
  ACCESSOR ::= 'G'                           // global getter
  ACCESSOR ::= 'w'                           // willSet
  ACCESSOR ::= 'W'                           // didSet
  ACCESSOR ::= 'a' ADDRESSOR-KIND            // mutable addressor
  ACCESSOR ::= 'l' ADDRESSOR-KIND            // non-mutable addressor
  ACCESSOR ::= 'p'                           // pseudo accessor referring to the storage itself
                                         
  ADDRESSOR-KIND ::= 'u'                     // unsafe addressor (no owner)
  ADDRESSOR-KIND ::= 'O'                     // owning addressor (non-native owner)
  ADDRESSOR-KIND ::= 'o'                     // owning addressor (native owner)
  ADDRESSOR-KIND ::= 'p'                     // pinning addressor (native owner)

  decl-name ::= identifier
  decl-name ::= identifier 'L' INDEX         // locally-discriminated declaration
  decl-name ::= identifier identifier 'LL'   // file-discriminated declaration

  file-discriminator ::= identifier 'Ll'     // anonymous file-discriminated declaration

The identifier in a ``<file-discriminator>`` and the first identifier in a
file-discriminated ``<decl-name>`` is a string that represents the file the
original declaration came from. It should be considered unique within the
enclosing module. The second identifier is the name of the entity. Not all
declarations marked ``private`` declarations will use this mangling; if the
entity's context is enough to uniquely identify the entity, the simple
``identifier`` form is preferred.

Outlined bridged Objective C method call mangling includes which parameters and
return value are bridged and the type of pattern outlined.

::

  bridge-spec := bridged-kind bridged-param* bridged-return '_'

  bridged-param := 'n' // not bridged parameter
  bridged-param := 'b' // bridged parameter

  bridged-return := 'n' // not bridged return
  bridged-return := 'b' // bridged return

  bridged-kind := 'm' // bridged method
  bridged-kind := 'a' // bridged property (by address)
  bridged-kind := 'p' // bridged property (by value)

Declaration Contexts
~~~~~~~~~~~~~~~~~~~~

These manglings identify the enclosing context in which an entity was declared,
such as its enclosing module, function, or nominal type.

::

  context ::= module
  context ::= entity
  context ::= entity module generic-signature? 'E'

An ``extension`` mangling is used whenever an entity's declaration context is
an extension *and* the entity being extended is in a different module. In this
case the extension's module is mangled first, followed by the entity being
extended. If the extension and the extended entity are in the same module, the
plain ``entity`` mangling is preferred. If the extension is constrained, the
constraints on the extension are mangled in its generic signature.

When mangling the context of a local entity within a constructor or
destructor, the non-allocating or non-deallocating variant is used.

::

  module ::= identifier                      // module name
  module ::= known-module                    // abbreviation

  known-module ::= 's'                       // Swift
  known-module ::= 'SC'                      // C
  known-module ::= 'So'                      // Objective-C

The Objective-C module is used as the context for mangling Objective-C
classes as ``<type>``\ s.


Types
~~~~~

::

  any-generic-type ::= substitution
  any-generic-type ::= context decl-name 'C'     // nominal class type
  any-generic-type ::= context decl-name 'O'     // nominal enum type
  any-generic-type ::= context decl-name 'V'     // nominal struct type
  any-generic-type ::= protocol 'P'              // nominal protocol type
  any-generic-type ::= context decl-name 'a'     // typealias type (used in DWARF and USRs)

  any-generic-type ::= 'S' KNOWN-TYPE-KIND       // known nominal type substitution
  any-generic-type ::= 'S' NATURAL KNOWN-TYPE-KIND    // repeated known type substitutions of the same kind

  KNOWN-TYPE-KIND ::= 'a'                    // Swift.Array
  KNOWN-TYPE-KIND ::= 'b'                    // Swift.Bool
  KNOWN-TYPE-KIND ::= 'c'                    // Swift.UnicodeScalar
  KNOWN-TYPE-KIND ::= 'd'                    // Swift.Float64
  KNOWN-TYPE-KIND ::= 'f'                    // Swift.Float32
  KNOWN-TYPE-KIND ::= 'i'                    // Swift.Int
  KNOWN-TYPE-KIND ::= 'V'                    // Swift.UnsafeRawPointer
  KNOWN-TYPE-KIND ::= 'v'                    // Swift.UnsafeMutableRawPointer
  KNOWN-TYPE-KIND ::= 'P'                    // Swift.UnsafePointer
  KNOWN-TYPE-KIND ::= 'p'                    // Swift.UnsafeMutablePointer
  KNOWN-TYPE-KIND ::= 'Q'                    // Swift.ImplicitlyUnwrappedOptional
  KNOWN-TYPE-KIND ::= 'q'                    // Swift.Optional
  KNOWN-TYPE-KIND ::= 'R'                    // Swift.UnsafeBufferPointer
  KNOWN-TYPE-KIND ::= 'r'                    // Swift.UnsafeMutableBufferPointer
  KNOWN-TYPE-KIND ::= 'S'                    // Swift.String
  KNOWN-TYPE-KIND ::= 'u'                    // Swift.UInt

  protocol ::= context decl-name

  type ::= 'Bb'                              // Builtin.BridgeObject
  type ::= 'BB'                              // Builtin.UnsafeValueBuffer
  type ::= 'Bf' NATURAL '_'                  // Builtin.Float<n>
  type ::= 'Bi' NATURAL '_'                  // Builtin.Int<n>
  type ::= 'BO'                              // Builtin.UnknownObject
  type ::= 'Bo'                              // Builtin.NativeObject
  type ::= 'Bp'                              // Builtin.RawPointer
  type ::= 'Bt'                              // Builtin.SILToken
  type ::= type 'Bv' NATURAL '_'             // Builtin.Vec<n>x<type>
  type ::= 'Bw'                              // Builtin.Word
  type ::= function-signature 'c'            // function type
  type ::= function-signature 'X' FUNCTION-KIND // special function type
  type ::= bound-generic-type
  type ::= type 'Sg'                         // optional type, shortcut for: type 'ySqG'
  type ::= type 'Xo'                         // @unowned type
  type ::= type 'Xu'                         // @unowned(unsafe) type
  type ::= type 'Xw'                         // @weak type
  type ::= impl-function-type 'XF'           // function implementation type (currently unused)
  type ::= type 'Xb'                         // SIL @box type (deprecated)
  type ::= type-list 'Xx'                    // SIL box type
  type ::= type-list type-list generic-signature 'XX'
                                             // Generic SIL box type
  type ::= type 'XD'                         // dynamic self type
  type ::= type 'm'                          // metatype without representation
  type ::= type 'XM' METATYPE-REPR           // metatype with representation
  type ::= type 'Xp'                         // existential metatype without representation
  type ::= type 'Xm' METATYPE-REPR           // existential metatype with representation
  type ::= 'Xe'                              // error or unresolved type
 
  bound-generic-type ::= type 'y' (type* '_')* type* 'G'   // one type-list per nesting level of type
  bound-generic-type ::= substitution

  FUNCTION-KIND ::= 'f'                      // @thin function type
  FUNCTION-KIND ::= 'U'                      // uncurried function type (currently not used) 
  FUNCTION-KIND ::= 'K'                      // @auto_closure function type
  FUNCTION-KIND ::= 'B'                      // objc block function type
  FUNCTION-KIND ::= 'C'                      // C function pointer type

  function-signature ::= params-type params-type throws? // results and parameters

  params-type := type 'z'? 'h'?              // tuple in case of multiple parameters or a single parameter with a single tuple type
                                             // with optional inout convention, shared convention
  params-type := empty-list                  // shortcut for no parameters

  throws ::= 'K'                             // 'throws' annotation on function types

  type-list ::= list-type '_' list-type*     // list of types
  type-list ::= empty-list

                                                  // FIXME: Consider replacing 'h' with a two-char code
  list-type ::= type identifier? 'z'? 'h'? 'd'?   // type with optional label, inout convention, shared convention, and variadic specifier

  METATYPE-REPR ::= 't'                      // Thin metatype representation
  METATYPE-REPR ::= 'T'                      // Thick metatype representation
  METATYPE-REPR ::= 'o'                      // ObjC metatype representation

  type ::= archetype
  type ::= associated-type
  type ::= any-generic-type
  type ::= protocol-list 'p'                 // existential type
  type ::= protocol-list superclass 'Xc'     // existential type with superclass
  type ::= protocol-list 'Xl'                // existential type with AnyObject
  type ::= type-list 't'                     // tuple
  type ::= type generic-signature 'u'        // generic type
  type ::= 'x'                               // generic param, depth=0, idx=0
  type ::= 'q' GENERIC-PARAM-INDEX           // dependent generic parameter
  type ::= type assoc-type-name 'qa'         // associated type of non-generic param
  type ::= assoc-type-name 'Qy' GENERIC-PARAM-INDEX  // associated type
  type ::= assoc-type-name 'Qz'                      // shortcut for 'Qyz'
  type ::= assoc-type-list 'QY' GENERIC-PARAM-INDEX  // associated type at depth
  type ::= assoc-type-list 'QZ'                      // shortcut for 'QYz'

  protocol-list ::= protocol '_' protocol*
  protocol-list ::= empty-list

  assoc-type-list ::= assoc-type-name '_' assoc-type-name*

  archetype ::= context 'Qq' INDEX           // archetype+context (DWARF only)
  archetype ::= associated-type

  associated-type ::= substitution
  associated-type ::= protocol 'QP'          // self type of protocol
  associated-type ::= archetype identifier 'Qa' // associated type
  
  assoc-type-name ::= identifier                // associated type name without protocol
  assoc-type-name ::= identifier protocol 'P'   //

  empty-list ::= 'y'

Associated types use an abbreviated mangling when the base generic parameter
or associated type is constrained by a single protocol requirement. The
associated type in this case can be referenced unambiguously by name alone.
If the base has multiple conformance constraints, then the protocol name is
mangled in to disambiguate.

::

  impl-function-type ::= type* 'I' FUNC-ATTRIBUTES '_'
  impl-function-type ::= type* generic-signature 'I' PSEUDO-GENERIC? FUNC-ATTRIBUTES '_'

  FUNC-ATTRIBUTES ::= CALLEE-ESCAPE? CALLEE-CONVENTION FUNC-REPRESENTATION? PARAM-CONVENTION* RESULT-CONVENTION* ('z' RESULT-CONVENTION)

  PSEUDO-GENERIC ::= 'P'

  CALLEE-ESCAPE ::= 'e'                      // @escaping (inverse of SIL @noescape)

  CALLEE-CONVENTION ::= 'y'                  // @callee_unowned
  CALLEE-CONVENTION ::= 'g'                  // @callee_guaranteed
  CALLEE-CONVENTION ::= 'x'                  // @callee_owned
  CALLEE-CONVENTION ::= 't'                  // thin

  FUNC-REPRESENTATION ::= 'B'                // C block invocation function
  FUNC-REPRESENTATION ::= 'C'                // C global function
  FUNC-REPRESENTATION ::= 'M'                // Swift method
  FUNC-REPRESENTATION ::= 'J'                // ObjC method
  FUNC-REPRESENTATION ::= 'K'                // closure
  FUNC-REPRESENTATION ::= 'W'                // protocol witness

  PARAM-CONVENTION ::= 'i'                   // indirect in
  PARAM-CONVENTION ::= 'c'                   // indirect in constant
  PARAM-CONVENTION ::= 'l'                   // indirect inout
  PARAM-CONVENTION ::= 'b'                   // indirect inout aliasable
  PARAM-CONVENTION ::= 'n'                   // indirect in guaranteed
  PARAM-CONVENTION ::= 'x'                   // direct owned
  PARAM-CONVENTION ::= 'y'                   // direct unowned
  PARAM-CONVENTION ::= 'g'                   // direct guaranteed
  PARAM-CONVENTION ::= 'e'                   // direct deallocating

  RESULT-CONVENTION ::= 'r'                  // indirect
  RESULT-CONVENTION ::= 'o'                  // owned
  RESULT-CONVENTION ::= 'd'                  // unowned
  RESULT-CONVENTION ::= 'u'                  // unowned inner pointer
  RESULT-CONVENTION ::= 'a'                  // auto-released

For the most part, manglings follow the structure of formal language
types.  However, in some cases it is more useful to encode the exact
implementation details of a function type.

The ``type*`` list contains parameter and return types (including the error
result), in that order.
The number of parameters and results must match with the number of
``<PARAM-CONVENTION>`` and ``<RESULT-CONVENTION>`` characters after the
``<FUNC-REPRESENTATION>``.
The ``<generic-signature>`` is used if the function is polymorphic.

Generics
~~~~~~~~

::

  protocol-conformance ::= type protocol module generic-signature?

``<protocol-conformance>`` refers to a type's conformance to a protocol. The
named module is the one containing the extension or type declaration that
declared the conformance.

::

  protocol-conformance ::= context identifier protocol identifier generic-signature?  // Property behavior conformance

Property behaviors are implemented using private protocol conformances.

::

  generic-signature ::= requirement* 'l'     // one generic parameter
  generic-signature ::= requirement* 'r' GENERIC-PARAM-COUNT* 'l'

  GENERIC-PARAM-COUNT ::= 'z'                // zero parameters
  GENERIC-PARAM-COUNT ::= INDEX              // N+1 parameters

  requirement ::= protocol 'R' GENERIC-PARAM-INDEX                  // protocol requirement
  requirement ::= protocol assoc-type-name 'Rp' GENERIC-PARAM-INDEX // protocol requirement on associated type
  requirement ::= protocol assoc-type-list 'RP' GENERIC-PARAM-INDEX // protocol requirement on associated type at depth
  requirement ::= protocol substitution 'RQ'                        // protocol requirement with substitution
  requirement ::= type 'Rb' GENERIC-PARAM-INDEX                     // base class requirement
  requirement ::= type assoc-type-name 'Rc' GENERIC-PARAM-INDEX     // base class requirement on associated type
  requirement ::= type assoc-type-list 'RC' GENERIC-PARAM-INDEX     // base class requirement on associated type at depth
  requirement ::= type substitution 'RB'                            // base class requirement with substitution
  requirement ::= type 'Rs' GENERIC-PARAM-INDEX                     // same-type requirement
  requirement ::= type assoc-type-name 'Rt' GENERIC-PARAM-INDEX     // same-type requirement on associated type
  requirement ::= type assoc-type-list 'RT' GENERIC-PARAM-INDEX     // same-type requirement on associated type at depth
  requirement ::= type substitution 'RS'                            // same-type requirement with substitution
  requirement ::= type 'Rl' GENERIC-PARAM-INDEX LAYOUT-CONSTRAINT   // layout requirement
  requirement ::= type assoc-type-name 'Rm' GENERIC-PARAM-INDEX LAYOUT-CONSTRAINT    // layout requirement on associated type
  requirement ::= type assoc-type-list 'RM' GENERIC-PARAM-INDEX LAYOUT-CONSTRAINT    // layout requirement on associated type at depth
  requirement ::= type substitution 'RM' LAYOUT-CONSTRAINT                           // layout requirement with substitution

  GENERIC-PARAM-INDEX ::= 'z'                // depth = 0,   idx = 0
  GENERIC-PARAM-INDEX ::= INDEX              // depth = 0,   idx = N+1
  GENERIC-PARAM-INDEX ::= 'd' INDEX INDEX    // depth = M+1, idx = N

  LAYOUT-CONSTRAINT ::= 'N'  // NativeRefCountedObject 
  LAYOUT-CONSTRAINT ::= 'R'  // RefCountedObject 
  LAYOUT-CONSTRAINT ::= 'T'  // Trivial 
  LAYOUT-CONSTRAINT ::= 'C'  // Class
  LAYOUT-CONSTRAINT ::= 'D'  // NativeClass 
  LAYOUT-CONSTRAINT ::= 'E' LAYOUT-SIZE-AND-ALIGNMENT  // Trivial of exact size 
  LAYOUT-CONSTRAINT ::= 'e' LAYOUT-SIZE  // Trivial of exact size 
  LAYOUT-CONSTRAINT ::= 'M' LAYOUT-SIZE-AND-ALIGNMENT  // Trivial of size at most N bits 
  LAYOUT-CONSTRAINT ::= 'm' LAYOUT-SIZE  // Trivial of size at most N bits 
  LAYOUT-CONSTRAINT ::= 'U'  // Unknown layout

  LAYOUT-SIZE ::= INDEX // Size only
  LAYOUT-SIZE-AND-ALIGNMENT ::= INDEX INDEX // Size followed by alignment



A generic signature begins with an optional list of requirements.
The ``<GENERIC-PARAM-COUNT>`` describes the number of generic parameters at
each depth of the signature. As a special case, no ``<GENERIC-PARAM-COUNT>``
values indicates a single generic parameter at the outermost depth::

  x_xCru                           // <T_0_0> T_0_0 -> T_0_0
  d_0__xCr_0_u                     // <T_0_0><T_1_0, T_1_1> T_0_0 -> T_1_1

A generic signature must only precede an operator character which is different
from any character in a ``<GENERIC-PARAM-COUNT>``.

Identifiers
~~~~~~~~~~~

::

  identifier ::= substitution
  identifier ::= NATURAL IDENTIFIER-STRING   // identifier without word substitutions
  identifier ::= '0' IDENTIFIER-PART         // identifier with word substitutions

  IDENTIFIER-PART ::= NATURAL IDENTIFIER-STRING
  IDENTIFIER-PART ::= [a-z]                  // word substitution (except the last one)
  IDENTIFIER-PART ::= [A-Z]                  // last word substitution in identifier

  IDENTIFIER-STRING ::= IDENTIFIER-START-CHAR IDENTIFIER-CHAR*
  IDENTIFIER-START-CHAR ::= [_a-zA-Z]
  IDENTIFIER-CHAR ::= [_$a-zA-Z0-9]

``<identifier>`` is run-length encoded: the natural indicates how many
characters follow. Operator characters are mapped to letter characters as
given. In neither case can an identifier start with a digit, so
there's no ambiguity with the run-length.

If the run-length start with a ``0`` the identifier string contains
word substitutions. A word is a sub-string of an identifier which contains
letters and digits ``[A-Za-z0-9]``. Words are separated by underscores
``_``. In addition a new word begins with an uppercase letter ``[A-Z]``
if the previous character is not an uppercase letter::

  Abc1DefG2HI          // contains four words 'Abc1', 'Def' and 'G2' and 'HI'
  _abc1_def_G2hi       // contains three words 'abc1', 'def' and G2hi

The words of all identifiers, which are encoded in the current mangling are
enumerated and assigned to a letter: a = first word, b = second word, etc.

An identifier containing word substitutions is a sequence of run-length encoded
sub-strings and references to previously mangled words.
All but the last word-references are lowercase letters and the last one is an
uppercase letter. If there is no literal sub-string after the last
word-reference, the last word-reference is followed by a ``0``.

Let's assume the current mangling already encoded the identifier ``AbcDefGHI``::

  02Myac1_B    // expands to: MyAbcGHI_Def

A maximum of 26 words in a mangling can be used for substitutions.

::

  identifier ::= '00' natural '_'? IDENTIFIER-CHAR+  // '_' is inserted if the identifier starts with a digit or '_'.

Identifiers that contain non-ASCII characters are encoded using the Punycode
algorithm specified in RFC 3492, with the modifications that ``_`` is used
as the encoding delimiter, and uppercase letters A through J are used in place
of digits 0 through 9 in the encoding character set. The mangling then
consists of an ``00`` followed by the run length of the encoded string and the
encoded string itself. For example, the identifier ``vergüenza`` is mangled
to ``0012vergenza_JFa``. (The encoding in standard Punycode would be
``vergenza-95a``)

If the encoded string starts with a digit or an ``_``, an additional ``_`` is
inserted between the run length and the encoded string.

::

  identifier ::= identifier 'o' OPERATOR-FIXITY

  OPERATOR-FIXITY ::= 'p'                    // prefix operator
  OPERATOR-FIXITY ::= 'P'                    // postfix operator
  OPERATOR-FIXITY ::= 'i'                    // infix operator

  OPERATOR-CHAR ::= 'a'                      // & 'and'
  OPERATOR-CHAR ::= 'c'                      // @ 'commercial at'
  OPERATOR-CHAR ::= 'd'                      // / 'divide'
  OPERATOR-CHAR ::= 'e'                      // = 'equals'
  OPERATOR-CHAR ::= 'g'                      // > 'greater'
  OPERATOR-CHAR ::= 'l'                      // < 'less'
  OPERATOR-CHAR ::= 'm'                      // * 'multiply'
  OPERATOR-CHAR ::= 'n'                      // ! 'not'
  OPERATOR-CHAR ::= 'o'                      // | 'or'
  OPERATOR-CHAR ::= 'p'                      // + 'plus'
  OPERATOR-CHAR ::= 'q'                      // ? 'question'
  OPERATOR-CHAR ::= 'r'                      // % 'remainder'
  OPERATOR-CHAR ::= 's'                      // - 'subtract'
  OPERATOR-CHAR ::= 't'                      // ~ 'tilde'
  OPERATOR-CHAR ::= 'x'                      // ^ 'xor'
  OPERATOR-CHAR ::= 'z'                      // . 'zperiod'

If an identifier is followed by an ``o`` its text is interpreted as an
operator. Each lowercase character maps to an operator character
(``OPERATOR-CHAR``).

Operators that contain non-ASCII characters are mangled by first mapping the
ASCII operator characters to letters as for pure ASCII operator names, then
Punycode-encoding the substituted string.
For example, the infix operator ``«+»`` is mangled to
``007p_qcaDcoi`` (``p_qcaDc`` being the encoding of the substituted
string ``«p»``).

Substitutions
~~~~~~~~~~~~~

::

  substitution ::= 'A' INDEX                  // substitution of N+26
  substitution ::= 'A' SUBST_IDX* LAST-SUBST-IDX    // One or more consecutive substitutions of N < 26
  SUBST-IDX ::= [a-z]
  SUBST-IDX ::= NATURAL [a-z]
  LAST-SUBST-IDX ::= [A-Z]
  LAST-SUBST-IDX ::= NATURAL [A-Z]


``<substitution>`` is a back-reference to a previously mangled entity. The mangling
algorithm maintains a mapping of entities to substitution indices as it runs.
When an entity that can be represented by a substitution (a module, nominal
type, or protocol) is mangled, a substitution is first looked for in the
substitution map, and if it is present, the entity is mangled using the
associated substitution index. Otherwise, the entity is mangled normally, and
it is then added to the substitution map and associated with the next
available substitution index.

For example, in mangling a function type
``(zim.zang.zung, zim.zang.zung, zim.zippity) -> zim.zang.zoo`` (with module
``zim`` and class ``zim.zang``),
the recurring contexts ``zim``, ``zim.zang``, and ``zim.zang.zung``
will be mangled using substitutions after being mangled
for the first time. The first argument type will mangle in long form,
``3zim4zang4zung``, and in doing so, ``zim`` will acquire substitution ``AA``,
``zim.zang`` will acquire substitution ``AB``, and ``zim.zang.zung`` will
acquire ``AC``. The second argument is the same as the first and will mangle
using its substitution, ``AC``. The
third argument type will mangle using the substitution for ``zim``,
``AA7zippity``. (It also acquires substitution ``AD`` which would be used
if it mangled again.) The result type will mangle using the substitution for
``zim.zang``, ``AB3zoo`` (and acquire substitution ``AE``).

There are some pre-defined substitutions, see ``KNOWN-TYPE-KIND``.

If the mangling contains two or more consecutive substitutions, it can be
abbreviated with the ``A`` substitution. Similar to word-substitutions the
index is encoded as letters, whereas the last letter is uppercase::

  AaeB      // equivalent to A_A4_A0_

Repeated substitutions are encoded with a natural prefix number::

  A3a2B     // equivalent to AaaabB

Numbers and Indexes
~~~~~~~~~~~~~~~~~~~

::

  INDEX ::= '_'                               // 0
  INDEX ::= NATURAL '_'                       // N+1
  NATURAL ::= [1-9] [0-9]*
  NATURAL_ZERO ::= [0-9]+

``<INDEX>`` is a production for encoding numbers in contexts that can't
end in a digit; it's optimized for encoding smaller numbers.

Function Specializations
~~~~~~~~~~~~~~~~~~~~~~~~

::

  specialization ::= type '_' type* 'Tg' SPEC-INFO     // Generic re-abstracted specialization
  specialization ::= type '_' type* 'TG' SPEC-INFO     // Generic not re-abstracted specialization

The types are the replacement types of the substitution list.

::

  specialization ::= type 'Tp' SPEC-INFO // Partial generic specialization
  specialization ::= type 'TP' SPEC-INFO // Partial generic specialization, not re-abstracted

The type is the function type of the specialized function.

::

  specialization ::= spec-arg* 'Tf' SPEC-INFO UNIQUE-ID? ARG-SPEC-KIND* '_' ARG-SPEC-KIND  // Function signature specialization kind

The ``<ARG-SPEC-KIND>`` describes how arguments are specialized.
Some kinds need arguments, which precede ``Tf``.

::

  spec-arg ::= identifier
  spec-arg ::= type

  SPEC-INFO ::= FRAGILE? PASSID

  PASSID ::= '0'                             // AllocBoxToStack,
  PASSID ::= '1'                             // ClosureSpecializer,
  PASSID ::= '2'                             // CapturePromotion,
  PASSID ::= '3'                             // CapturePropagation,
  PASSID ::= '4'                             // FunctionSignatureOpts,
  PASSID ::= '5'                             // GenericSpecializer,

  FRAGILE ::= 'q'

  UNIQUE-ID ::= NATURAL                      // Used to make unique function names

  ARG-SPEC-KIND ::= 'n'                      // Unmodified argument
  ARG-SPEC-KIND ::= 'c'                      // Consumes n 'type' arguments which are closed over types in argument order
                                             // and one 'identifier' argument which is the closure symbol name
  ARG-SPEC-KIND ::= 'p' CONST-PROP           // Constant propagated argument
  ARG-SPEC-KIND ::= 'd' 'G'? 'X'?            // Dead argument, with optional owned=>guaranteed or exploded-specifier
  ARG-SPEC-KIND ::= 'g' 'X'?                 // Owned => Guaranteed,, with optional exploded-specifier
  ARG-SPEC-KIND ::= 'x'                      // Exploded
  ARG-SPEC-KIND ::= 'i'                      // Box to value
  ARG-SPEC-KIND ::= 's'                      // Box to stack

  CONST-PROP ::= 'f'                         // Consumes one identifier argument which is a function symbol name
  CONST-PROP ::= 'g'                         // Consumes one identifier argument which is a global symbol name
  CONST-PROP ::= 'i' NATURAL_ZERO            // 64-bit-integer
  CONST-PROP ::= 'd' NATURAL_ZERO            // float-as-64-bit-integer
  CONST-PROP ::= 's' ENCODING                // string literal. Consumes one identifier argument.

  ENCODING ::= 'b'                           // utf8
  ENCODING ::= 'w'                           // utf16
  ENCODING ::= 'c'                           // utf16

If the first character of the string literal is a digit ``[0-9]`` or an
underscore ``_``, the identifier for the string literal is prefixed with an
additional underscore ``_``.
