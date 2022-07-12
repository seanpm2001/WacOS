// ---------------------------------------------------------------------------
// Multiple explicit conformances to the same protocol
// ---------------------------------------------------------------------------

extension MFExplicit1 : P1 { }

struct MFExplicit2 : P1 { } // expected-note{{'MFExplicit2' declares conformance to protocol 'P1' here}}

// ---------------------------------------------------------------------------
// Multiple implicit conformances, with no ambiguities
// ---------------------------------------------------------------------------

struct MFMultipleImplicit1 : P2a { }

struct MFMultipleImplicit3 : P4 { }

struct MFMultipleImplicit4 : P4 { }

struct MFMultipleImplicit5 : P2a { }

// ---------------------------------------------------------------------------
// Explicit conformances conflicting with inherited conformances
// ---------------------------------------------------------------------------

class MFExplicitSub1 : ImplicitSuper1 { } // expected-note{{'MFExplicitSub1' inherits conformance to protocol 'P1' from superclass here}}

// ---------------------------------------------------------------------------
// Suppression of synthesized conformances
// ---------------------------------------------------------------------------
class MFSynthesizedClass2 { }

class MFSynthesizedClass3 : AnyObjectRefinement { }

class MFSynthesizedSubClass2 : MFSynthesizedClass2 { }

class MFSynthesizedSubClass3 : MFSynthesizedClass1 { }

extension MFSynthesizedSubClass4 : AnyObjectRefinement { }

enum MFSynthesizedEnum1 : Int { case a }
extension MFSynthesizedEnum2 : RawRepresentable { }

