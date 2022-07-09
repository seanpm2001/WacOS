{

PRT test 1857: record variant: tag constant outside of range.

   Tests tagfield case constant out of range.

}

program iso7185prt1857(output);

type enum = (one, two, three, four, five);
     enums = two..four;
     rec  = record case e: enums of
              two: (c: char);
              three: (b: boolean);
              four: ();
              five: ();
            end;

begin
end.