{

PRT test 1854: Self referencing type. Same type but outter block.

   Tests a type that references its own definition, but the definition exists
   in outter block.


}

program iso7185prt1854(output);

type r = integer;

procedure y;

type r = record a: r end;

var x: r;

begin
end;

begin

end.