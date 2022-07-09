{

PRT test 1808: Validity of for loop index. Index out of current block.

    ISO 7185 6.8.3.9

}

program iso7185prt1808(output);

var i: integer;

procedure a;

begin

   for i := 1 to 10 do begin

      write(i:1, ' ')

   end

end;

begin

   a

end.