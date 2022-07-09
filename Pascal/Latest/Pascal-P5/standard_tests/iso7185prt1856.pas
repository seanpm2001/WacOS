{

PRT test 1856: record variant: all tag constants appear

   Tests if a variant contains all constants of the tag field.

}

program iso7185prt1856(output);

type enum = (one, two, three);
     rec  = record case e: enum of
              one: (i: integer);
              two: (c: char);
            end;

begin
end.
