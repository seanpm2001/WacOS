{

PRT test 24: Reverse order between label and const

}

program iso7185prt0024(output);

const one = 1;

label 1;

begin

   writeln(one);

   goto 1;

   1:

end.