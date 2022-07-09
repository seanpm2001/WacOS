{

PRT test 1750: For an assignment-statement, it is an error if the expression 
               is of a set-type whose value is not assignment-compatible with
               the type possessed by the variable.

               ISO 7185 reference: 6.8.2.2

}

program iso7185prt1750(output);

var a: set of 1..10;

begin

   a := [1, 2, 11]

end.
