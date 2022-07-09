{

PRT test 1907b: Use real value to form subrange

    A real value is used as a subrange bound.

}

program iso7185prt1907b;

type MySubrange = 1 .. 10.1;

var s: MySubrange;

begin

   s := 5

end.
