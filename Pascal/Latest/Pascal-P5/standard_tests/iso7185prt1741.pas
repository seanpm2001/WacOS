{

PRT test 1741: When eoln(f) is activated, it is an error if f is undefined.

               ISO 7185 reference: 6.6.6.5

}

program iso7185prt1741(output);

var a: text;

begin

   { As usual, it is possible that this could be completely optimized out }
   if eoln(a) then

end.
