{

PRT test 1870: Overflow test on unpack.

    Tests if overrunning the end of the unpacked array on unpack generates an error.
    
}

program iso7185prt1870;

var upa: array [1..10] of integer;
    pa: array [2..5] of integer;
    i: integer;
    
begin

   for i := 2 to 5 do pa[i] := i+10;
   unpack(pa, upa, 9)
   
end.