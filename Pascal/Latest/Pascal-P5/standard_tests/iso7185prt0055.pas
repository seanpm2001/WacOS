{

PRT test 55: Reverse order between var and function

}

program iso7185prt0055(output);

function x: integer; begin x := 1 end;

var y: integer;

begin

   writeln(x, y)

end.