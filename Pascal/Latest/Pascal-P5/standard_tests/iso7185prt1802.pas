{

PRT test 1802: Threats to FOR statement index. VAR param.

    Threat within the controlled statement block, VAR param.
    
    ISO 7185 6.8.3.9

}

program iso7185prt1802(output);

var i: integer;

procedure a(var i: integer);

begin

   i := 1

end;

begin

   for i := 1 to 10 do begin

      write(i:1, ' ');
      a(i)

   end

end.