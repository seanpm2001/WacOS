{

PRT test 1728: For pack, it is an error if the index-type of the unpacked 
               array is exceeded.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1728(output);

var a: array [1..20] of integer;
    b: packed array [1..10] of integer;
    i: integer;

begin

   for i := 1 to 20 do a[i] := i;
   pack(a, 15, b)

end.
