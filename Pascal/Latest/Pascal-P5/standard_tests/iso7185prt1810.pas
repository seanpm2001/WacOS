{

PRT test 1810: Validity of for loop index. Index is part of structured type.

    ISO 7185 6.8.3.9

}

program iso7185prt1810(output);

var r: record

        i: integer;
        b: boolean

    end;

begin

   for r.i := 1 to 10 do begin

      write(r.i:1, ' ')

   end

end.