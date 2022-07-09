{

PRT test 1719: For new(p,c l ,...,c n,), it is an error if a variant of a
               variant-part within the new variable becomes active and a
               different variant of the variant-part is one of the specified
               variants.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1719(output);

type a = record case b: boolean of

          true:  (c: integer);
          false: (d: char)

       end;

var e: ^a;

begin

   new(e, true);
   e^.b := false

end.
