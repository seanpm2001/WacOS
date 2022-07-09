{

PRT test 1828: Out of bounds array access.

    Simple out of bounds access, with attempt to redirect to runtime.
    
    ISO 7185 6.5.3.2

}

program iso7185prt1828(output);

var a: array [1..10] of integer;
    i: integer;

begin

    for i := 1 to 10 do a[i] := 0;
    i := 11;
    writeln(a[i])

end.