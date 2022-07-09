{

PRT test 1734: For sqrt(x), it is an error if x is negative.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1734;
                    
var a: integer;
    b: real;

begin

   a := -1;
   b := sqrt(a)

end.
