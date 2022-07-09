{

PRT test 1739: For pred(x), the function yields a value whose ordinal number
               is one less than that of x, if such a value exists. It is an
               error if such a value does not exist.

               ISO 7185 reference: 6.6.6.4

}

program iso7185prt1739(output);

var a: integer;

begin

   a := -maxint;
   { for binary 2s complement math, which is asymetrical about 0, it would
     require 2 decrements to fail. However, the letter of the rule for ISO 7185
     pascal is that it should fail anytime the result is outside of
     -maxint..maxint. }
   a := pred(a);
   a := pred(a)

end.
