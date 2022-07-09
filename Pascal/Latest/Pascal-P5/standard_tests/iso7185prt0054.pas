{

PRT test 54: Reverse order between var and procedure

}

program iso7185prt0054(output);

procedure x; begin end;

var y: integer;

begin

   x;
   writeln(y)

end.