{

PRT test 1863: Procedure parameter parameter lists with VAR don't match.

    6.6.3.6 Parameter list congruity
    Two formal-parameter-lists shall be congruous if they contain the same
    number of formal-parametersections and if the formal-parameter-sections in
    corresponding positions match. Two formalparameter-sections shall match if
    any of the following statements is true.

    a) They are both value-parameter-specifications containing the same number
    of parameters and the type-identifier in each value-parameter-specification
    denotes the same type.
    
    b) They are both variable-parameter-specifications containing the same
    number of parameters and the type-identifier in each 
    variable-parameter-specification denotes the same type.
    
}

program iso7185prt1863;

procedure y(n: integer);

begin

end;

procedure z(procedure p(var i: integer));

begin
end;

begin

   z(y)
   
end.