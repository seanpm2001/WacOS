{

PRT test 1804: Threats to FOR statement index. Reuse of index.

    Threat within the controlled statement block, reuse of index in nested
    FOR loop.
    
    ISO 7185 6.8.3.9

}

program iso7185prt1804(output);

var i: integer;

begin

   for i := 1 to 10 do begin

      write(i:1, ' ');
      for i := 1 to 10 do write('hi')

   end

end.