{

PRT test 1809: Validity of for loop index. Index not ordinal type.

    ISO 7185 6.8.3.9

}

program iso7185prt1809(output);

var i: real;

begin

   for i := 1 to 10 do begin

      write(i:1, ' ')

   end

end.