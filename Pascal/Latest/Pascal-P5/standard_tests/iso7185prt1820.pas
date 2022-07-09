{

PRT test 1820: Backwards pointer association. Backwards association.

    Indicates an error unless a pointer reference uses backwards assocation,
    which is incorrect.
    
    ISO 7185 6.2.2.9, 6.4.1

}

program iso7185prt1820(output);

type a = integer;

var k: a;

procedure b;

type b = ^a;
     a = char;

var cp: b;

begin

    new(cp);
    cp^ := 1

end;

begin

   k := 1;
   b


end.