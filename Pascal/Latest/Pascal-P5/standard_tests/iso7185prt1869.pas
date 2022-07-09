{

PRT test 1869: Overflow test on pack.

    Tests if overrunning the end of the unpacked array on pack generates an error.
    
}

program iso7185prt1869;

var upa: array [1..10] of integer;
    pa: array [2..5] of integer;
    i: integer;
    
begin

   for i := 1 to 10 do upa[i] := i+10;
   pack(upa, 9, pa)
   
end.