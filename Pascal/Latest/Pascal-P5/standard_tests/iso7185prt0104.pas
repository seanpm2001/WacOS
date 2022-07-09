{

PRT test 104: Missing procedure identifier

}

program iso7185prt0104(output);

{ The appearance of a procedure with a matching list could conceivably
  allow recovery }
procedure x(a, b: integer);

begin

   writeln(a, b)

end;

begin

   (1, 2)

end.