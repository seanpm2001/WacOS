{

PRT test 1842: Use of text procedure with non-text. Readln on integer file.

    Use readln with integer file.
    
    ISO 7185 6.9.2

}

program iso7185prt1842(output);

var f: file of integer;
    i: integer;

begin

   rewrite(f);
   write(f, 1);
   reset(f);
   readln(f, i)

end.