{

PRT test 1880: Tests binary write of out of bounds integer.

    Tests reading an out of bounds value to a subrange of integer generates
    an error.
}

program iso7185prt1880;

var f: file of 1..10;
    
begin

   rewrite(f);
   write(f, 42)
   
end.