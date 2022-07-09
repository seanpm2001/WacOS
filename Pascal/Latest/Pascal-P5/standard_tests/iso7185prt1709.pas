{

PRT test 1709: It is an error if the file mode is not Generation immediately
               prior to any use of put, write, writeln or page.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1709(output);

var a: file of integer;

begin

   rewrite(a);
   a^ := 1;
   put(a);
   reset(a);
   { This could fail on the write to the file buffer variable }
   a^ := 1;
   put(a)

end.
