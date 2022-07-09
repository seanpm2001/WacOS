{

PRT test 1877: Tests read to subrange of integer out of bounds generates error.

    Tests reading an out of bounds value to a subrange of integer generates
    an error.
}

program iso7185prt1877;

var f: file of integer;
    x: 1..10;
    
begin

   rewrite(f);
   write(f, 42);
   reset(f);
   read(f, x)
   
end.