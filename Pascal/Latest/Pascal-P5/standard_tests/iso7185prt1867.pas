{

PRT test 1867: Out of range index on pack.

    Tests if providing a starting index to pack generates an error.
    
}

program iso7185prt1867;

var upa: array [1..10] of integer;
    pa: array [2..5] of integer;
    i: integer;
    
begin

   for i := 1 to 10 do upa[i] := i+10;
   pack(upa, 0, pa)
   
end.