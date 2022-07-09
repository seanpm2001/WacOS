{

PRT test 1803: Threats to FOR statement index. Read or readln.

    Threat within the controlled statement block, read or readln.
    
    ISO 7185 6.8.3.9

}

program iso7185prt1803(output);

var i: integer;
    f: file of integer;

begin

   rewrite(f);
   write(f, 10);
   reset(f);
   for i := 1 to 10 do begin

      write(i:1, ' ');
      read(f, i)

   end

end.