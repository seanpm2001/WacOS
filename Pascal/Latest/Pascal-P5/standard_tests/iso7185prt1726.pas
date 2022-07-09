{

PRT test 1726: For pack, it is an error if the parameter of ordinal-type is 
               not assignment-compatible with the index-type of the unpacked 
               array parameter.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1726;

var a: array [1..10] of integer;
    b: packed array [1..10] of integer;
    i: integer;

begin

   { define the array so that we are not invoking the undefined access rule }
   for i := 1 to 10 do a[i] := i;
   pack(a, 'a', b)

end.
