{

PRT test 30: Missing semicolon in type

}

program iso7185prt0030;

type  integer = char
      five = integer;

var i: integer;
    a: five;

begin

   i := 'a';
   a := 1

end.