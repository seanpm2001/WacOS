{

PRT test 1915: Reference to outer block defintion before local definition.

    Use of an outer block definine before a definition in the same block.

}

program iso7185prt1762(output);

const one = 1;

procedure x;

const two = one;
      one = 2;
      
begin

  writeln(one, two)
  
end;

begin

   writeln(one)
   
end.
