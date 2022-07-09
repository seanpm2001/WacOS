{

PRT test 1862: Procedure parameter parameter lists don't match.

    6.6.3.6 Parameter list congruity
    Two formal-parameter-lists shall be congruous if they contain the same
    number of formal-parametersections and if the formal-parameter-sections in
    corresponding positions match. Two formalparameter-sections shall match if
    any of the following statements is true.

    a) They are both value-parameter-specifications containing the same number
    of parameters and the type-identifier in each value-parameter-specification
    denotes the same type.
    
}

program iso7185prt1862;

type x = 1..10;

procedure y(n: x);

begin

end;

procedure z(procedure p(i: integer));

begin
end;

begin

   z(y)
   
end.