{

PRT test 1900: Elide of type

   Type description completely missing.


}

program iso7185prt1900(output);

var
    avi:   { packed [1..10] of integer} ;
    pavi:  packed array [1..10] of integer;
    i:     integer;

begin

    for i := 1 to 10 do pavi[i] := i+10;
    unpack(pavi, avi, 1);

end.
