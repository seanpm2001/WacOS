{

PRT test 1758a: On writing to a textfile, the values of TotalWidth and 
                FracDigits are greater than or equal to one ; it is an error if
                either value is less than one.

                ISO 7185 reference: 6.9.3.1

                Divided into:

                A: TotalWidth is zero.

                B: FracDigits is zero.

}

program iso7185prt1758a(output);

var a: real;

begin

   a := 1.0;
   write(a: 0)

end.
