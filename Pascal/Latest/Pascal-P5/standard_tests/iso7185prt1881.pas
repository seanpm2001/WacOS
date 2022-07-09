{

PRT test 1881: Tests creation of array to large for memory.

    Create an array impossible to contain in memory.
    
}

program iso7185prt1881;

var a: array[0..maxint] of integer;

begin

   a[maxint] := 0

end.