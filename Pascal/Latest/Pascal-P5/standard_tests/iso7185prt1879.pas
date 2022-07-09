{

PRT test 1879: Tests binary write of out of bounds character.

    Tests if writing an out of range value to a file of char generates
    an error. Since this is technically assigning to the buffer variable,
    this is equivalent to a range limited assignment.
}

program iso7185prt1879;

var f: file of 'a'..'z';

begin

   rewrite(f);
   write(f, '1')
   
end.