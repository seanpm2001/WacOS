{

PRT test 1722: For dispose(p,k l ,...,k, ), it is an error if the variants in
               the variable identified by the pointer value of p are different
               from those specified by the case-constants k l ,...,k,,,,.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1722;

type a = record case b: boolean of

          true:  (c: integer);
          false: (d: char)

       end;
var e: ^a;

begin

   new(e, true);
   e^.b := true;
   dispose(e, false)

end.
