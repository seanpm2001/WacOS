{

PRT test 1740: When eof(f) is activated, it is an error if f is undefined.

               ISO 7185 reference: 6.6.6.5

}

program iso7185prt1740;

var a: file of integer;

begin

   { As usual, it is possible that this could be completely optimized out }
   if eof(a) then

end.
