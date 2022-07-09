{

PRT test 31: Reverse order between const and type

}

program iso7185prt0031(output);

type  integer = char;

const one = 1;

var i: integer;

begin

   i := 'a';
   writeln(one, i)

end.