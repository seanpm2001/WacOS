{

PRT test 1851: Reference to undefined variant

   Test if undefined variant can be detected after the variant is changed.


}

program iso7185prt1851(output);

var r: record

          case b: boolean of

             true:  (i: integer);
             false: (c: char)

       end;
    c: char;

begin

   r.b := true;
   r.i := 1;
   r.b := false;
   writeln('before error');
   c := r.c

end.