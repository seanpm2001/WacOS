{

PRT test 1907a: Use real value to form subrange

    A real value is used as a subrange bound.

}

program iso7185prt1907a;

type MySubrange = 1.1 .. 10;

var s: MySubrange;

begin

   s := 5

end.
