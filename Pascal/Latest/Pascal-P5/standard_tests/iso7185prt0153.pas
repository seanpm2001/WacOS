{

PRT test 153: Missing unsigned integer in goto statement

}

program iso7185prt0153;

{ Theoretically the compiler could determine that only one label is possible,
  and use that to recover. }
label 1;

begin

   goto ;

   1:

end.