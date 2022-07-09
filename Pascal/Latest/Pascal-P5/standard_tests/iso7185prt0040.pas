{

PRT test 40: Reverse order between type and var

}

program iso7185prt0040(output);

var   one: boolean;

type  integer = char;

begin

   one := true;
   writeln(one)

end.