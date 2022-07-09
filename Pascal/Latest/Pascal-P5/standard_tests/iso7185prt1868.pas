{

PRT test 1868: Out of range index on unpack.

    Tests if providing a starting index to unpack generates an error.
    
}

program iso7185prt1868;

var upa: array [1..10] of integer;
    pa: array [2..5] of integer;
    i: integer;
    
begin

   for i := 2 to 5 do pa[i] := i+10;
   unpack(pa, upa, 0)
   
end.