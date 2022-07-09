{

PRT test 1702a: It is an error unless a variant is active for the entirety of
                each reference and access to each component of the variant.

               ISO 7185 reference: 6.5.3.3

               There are four possible cases for active variants:

                  A: Reference to discriminated variant.
                  B: Change to the tagfield of a discriminated variant with
                     an outstanding reference.
                  C: Read of an undiscriminated variant after a write.
                  D: Write of an undiscriminated variant with outstanding
                     reference.

               This is case A, discrminated variant with standard reference.
}

program iso7185prt1702(output);

var a: record case val: boolean of

          true: (i: integer);
          false: (c: char);

       end;

begin

   a.val := true;
   a.c := 'c';

end.
