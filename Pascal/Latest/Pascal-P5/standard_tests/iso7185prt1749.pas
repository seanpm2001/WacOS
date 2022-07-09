{

PRT test 1749: For an assignment-statement, it is an error if the expression 
               is of an ordinal-type whose value is not assignment-compatible
               with the type possessed by the variable or function-identifier.

               ISO 7185 reference: 6.8.2.2

}

program iso7185prt1749;

var a: integer;

begin

   a := 'c'

end.
