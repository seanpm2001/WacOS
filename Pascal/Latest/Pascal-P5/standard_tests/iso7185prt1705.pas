{$i}
{

PRT test 1705: It is an error to remove from its pointer-type the
               identifying-value of an identified-variable when a reference to
               the identified-variable exists.

               ISO 7185 reference: 6.5.4

}

program iso7185prt1705(output);

var a: ^integer;

procedure b(var c: integer);

begin

   c := 1;
   dispose(a)

end;

begin

   { allocate integer value and pass that as reference, then change the value
     of the pointer }
   new(a);
   b(a^)

end.
