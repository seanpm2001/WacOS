{

PRT test 1733: For ln(x), it is an error if x is not greater than zero.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1733;

var a: integer;
    r: real;

begin

   a := 0;
   r := ln(a)

end.
