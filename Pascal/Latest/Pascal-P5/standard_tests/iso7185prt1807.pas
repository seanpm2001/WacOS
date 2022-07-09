{

PRT test 1807: Threats to FOR statement index. Read or readln, same block.

    Threat in same scope block, read or readln.
    
    ISO 7185 6.8.3.9

}

program iso7185prt1807(output);

var i: integer;
    f: file of integer;

procedure a;

begin

   read(f, i)

end;

begin

   rewrite(f);
   write(f, 10);
   reset(f);
   for i := 1 to 10 do begin

      write(i:1, ' ')

   end;
   a

end.