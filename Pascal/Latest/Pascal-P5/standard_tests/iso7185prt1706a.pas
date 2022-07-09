{$i}
{

PRT test 1706a: It is an error to alter the value of a file-variable f when a
                reference to the buffer-variable f^ exists.

               ISO 7185 reference: 6.5.5

}

program iso7185prt1706a(output);

var a: file of integer;

procedure b(var c: integer);

begin

   get(a);

end;

begin

   rewrite(a);
   a^ := 1;
   put(a);
   reset(a);
   b(a^)

end.
