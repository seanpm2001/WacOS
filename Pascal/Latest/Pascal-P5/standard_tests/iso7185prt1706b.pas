{$i}
{

PRT test 1706b: It is an error to alter the value of a file-variable f when a
                reference to the buffer-variable f^ exists.

                ISO 7185 reference: 6.5.5

}

program iso7185prt1706(output);

var a: text;

procedure b(var c: char);

begin

   get(a);

end;

begin

   rewrite(a);
   a^ := 'x';
   put(a);
   reset(a);
   b(a^)

end.
