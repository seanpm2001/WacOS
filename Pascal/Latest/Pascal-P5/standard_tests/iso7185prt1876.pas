{

PRT test 1876: Test read of subrange of char out of bounds generates error.

    Tests if reading from a character file to subrange of char with an out of
    bounds value generates an error.
}

program iso7185prt1876;

var f: file of char;
    x: 'a'..'z';

begin

   rewrite(f);
   write(f, '1');
   reset(f);
   read(f, x)
   
end.