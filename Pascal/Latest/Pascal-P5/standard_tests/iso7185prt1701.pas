{

PRT test 1701: For an indexed-variable closest-containing a single
               index-expression, it is an error if the value of the
               index-expression is not assignment-compatible with the
               index-type of the array-type.

               ISO 7185 reference: 6.5.3.2

}

program iso7185prt1701;

var a: array [1..10] of integer;

begin

   a['6'] := 1

end.
