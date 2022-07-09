{

PRT test 1873: Use of tag allocated record as VAR parameter.

    Tests that use of tag allocated record as a VAR parameter assignment 
    generates error.
    
    6.6.5.3 Dynamic allocation procedures
    
    ...
    
    new(p,c l ,...,cn)
    
    ...
    
    It shall be an error if a variable created using the second form of new is
    accessed by the identified-variable of the variable-access of a factor, of
    an assignment-statement, or of an actual-parameter.

}

program iso7185prt1873;

type r = record case b: boolean of
           true: (i: integer);
           false: (c: char)
         end;
         
var rp: ^r;
    x: r;
            
procedure y(var z: r);

begin

end;

begin

   new(rp, true);
   rp^.b := true;
   rp^.i := 42;
   y(rp^)
   
end.