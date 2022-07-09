{

PRT test 1738: For succ(x), the function yields a value whose ordinal number
               is one greater than that of x, if such a value exists. It is an
               error if such a value does not exist.

               ISO 7185 reference: 6.6.6.4

}

program iso7185prt1738;

var a: integer;

begin

   a := maxint;
   a := succ(a)

end.
