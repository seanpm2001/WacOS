{

PRT test 1735: For trunc(x), the value of trunc(x) is such that if x is
               positive or zero then 0 < x-trunc(x) < 1; otherwise 1 <x-
               trunc(x) < 0. It is an error if such a value does not exist.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1735;

var a: integer;
    b: real;

begin

   { assign maximum value of integer }
   b := maxint;
   { now move it completely out of range in floating point only }
   b := b*b;
   { now the assignment is invalid }
   a := trunc(b)

end.
