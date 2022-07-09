{

PRT test 1823: Invalid type substitutions. Use of subrange for VAR reference.

    ISO 7185 6.6.3.3

}

program iso7185prt1823(input);

var c: 1..10;

procedure a(var b: integer);

begin

   b := 1

end;

begin

   a(c)

end.