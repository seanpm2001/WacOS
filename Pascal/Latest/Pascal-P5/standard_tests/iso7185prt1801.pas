{

PRT test 1801: Threats to FOR statement index. Assign within block.

    Threat within the controlled statement block, assignment.
    
    ISO 7185 6.8.3.9

}

program iso7185prt1801(output);

var i: integer;

begin

   for i := 1 to 10 do begin

      write(i:1, ' ');
      i := 10

   end

end.