{

PRT test 1875: Test read to enum subrange out of bounds generates error.

    Tests if reading a value out of bounds to a subrange of enum generates an
    error.
}

program iso7185prt1875;

type enum = (one, two, three, four, five);

var f: file of enum;
    x: two..three;

begin

   rewrite(f);
   write(f, four);
   reset(f);
   read(f, x)
   
end.