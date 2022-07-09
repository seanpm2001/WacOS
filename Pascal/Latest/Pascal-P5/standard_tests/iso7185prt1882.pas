{

PRT test 1882: Tests creation of array to large for memory.

    Create an array impossible to contain in memory.
    
}

program iso7185prt1882;

var a: array[-maxint..maxint] of integer;

begin

   a[maxint] := 0

end.