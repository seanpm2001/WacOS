{

PRT test 1720: For dispose(p), it is an error if the identifying-value had
               been created using the form new(p,c l ,...,c n ).

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1720(output);

type a = record case b: boolean of

          true:  (c: integer);
          false: (d: char)

       end;
var e: ^a;

begin

   new(e, true);
   dispose(e)

end.
