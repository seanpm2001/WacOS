{$p}
{

PRT test 1800: Access to dynamic variable after dispose.

    ISO 7185 6.6.5.3

}

program iso7185prt1800;

var p, p2: ^char;

begin

   new(p);
   { on P5, this is required otherwise disposing of the single variable will
     cause all space to be removed }
   new(p2);
   dispose(p);
   p^ := 'a'

end.
