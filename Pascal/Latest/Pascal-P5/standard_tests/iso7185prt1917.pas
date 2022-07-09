{

PRT test 1917: Redefinition of same constant.

    Defining a constant as itself is both a reference and a defining point, thus
    it is inherently illegal.

}

program iso7185prt1917(output);

const one = 1;

procedure x;

const one = one;

begin

   write(one)
   
end;

begin

   writeln('Value is: ', one)
   
end.
