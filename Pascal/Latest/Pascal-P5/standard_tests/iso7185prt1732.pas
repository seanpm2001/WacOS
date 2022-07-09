{

PRT test 1732: Sqr(x) computes the square of x. It is an error if such a value
               does not exist.

               ISO 7185 reference: 6.6.6.2

}

program iso7185prt1732;

var a: integer;

begin

   a := maxint;
   a := sqr(maxint)

end.
