{

PRT test 1853: Self referencing type. Self reference to same type.

   Tests a type that references its own definition.


}

program iso7185prt1853(output);

type r = record a: r end;

var x: r;

begin

end.