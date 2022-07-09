{

PRT test 1872: Use of tag allocated record for left side of assignment.

    Tests that use of tag allocated record as the left side (target) of 
    assignment generates error.
    
    6.6.5.3 Dynamic allocation procedures
    
    ...
    
    new(p,c l ,...,cn)
    
    ...
    
    It shall be an error if a variable created using the second form of new is
    accessed by the identified-variable of the variable-access of a factor, of
    an assignment-statement, or of an actual-parameter.

}

program iso7185prt1872;

type r = record case b: boolean of
           true: (i: integer);
           false: (c: char)
         end;
         
var rp: ^r;
    x: r;
            
begin

   new(rp, true);
   x.b := true;
   x.i := 42;
   rp^ := x
   
end.