{

PRT test 1729: For unpack, it is an error if the parameter of ordinal-type is 
               not assignment-compatible with the index-type of the unpacked 
               array parameter.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1729;

var a: array [1..10] of integer;
    b: packed array [1..10] of integer;
    i: integer;

begin

   for i := 1 to 10 do b[i] := i;
   unpack(b, a, 'a')

end.
