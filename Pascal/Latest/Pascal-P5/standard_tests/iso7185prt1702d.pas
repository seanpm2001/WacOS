{$u,i}
{

PRT test 1702d: It is an error unless a variant is active for the entirety of
                each reference and access to each component of the variant.

                ISO 7185 reference: 6.5.3.3

                There are four possible cases for active variants:

                   A: Reference to discriminated variant.
                   B: Change to the tagfield of a discriminated variant with
                      an outstanding reference.
                   C: Read of an undiscriminated variant after a write.
                   D: Write of an undiscriminated variant with outstanding
                      reference.

                This is case D.

}

program iso7185prt1702(output);

var a: record case boolean of

          true: (i: integer);
          false: (c: char);

       end;

procedure b(var i: integer);

begin

   { change the undiscriminated variant, then print the refered variable to be
     sure the compiler sees it. This would rely on the compiler both allocating
     a tagfield, and automatically assigning it on write. }
   a.c := 'c';
   writeln('i: ', i)

end;

begin

   a.i := 1;
   b(a.i)

end.
