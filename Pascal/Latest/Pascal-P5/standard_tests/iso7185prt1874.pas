{

PRT test 1874: Test dispose of with referenced block generates error.

    If any outstanding reference to a dynamic variable exists on dispose, an
    error should result.
    
    6.5.4 Identified-variables

    A pointer-variable shall be a variable-access that denotes a variable
    possessing a pointer-type. It shall be an error if the pointer-variable of
    an identified-variable either denotes a nil-value or is undefined. It shall
    be an error to remove from the set of values of the pointer-type the 
    identifying-value of an identified-variable (see 6.6.5.3) when a reference
    to the identified-variable exists.
}

program iso7185prt1874;

type r = record i: integer end;

var rp: ^r;

begin

   new(rp);
   with rp^ do begin
   
      rp^.i := 42;
      dispose(rp)
      
   end
   
end.