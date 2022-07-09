{

PRT test 1878: Tests binary write of enum to file.

    Tests if writing an out of range value to a file of enum generates
    an error. Since this is technically assigning to the buffer variable,
    this is equivalent to a range limited assignment.
}

program iso7185prt1878;

type enum = (one, two, three, four, five);

var f: file of two..three;

begin

   rewrite(f);
   write(f, four)
   
end.