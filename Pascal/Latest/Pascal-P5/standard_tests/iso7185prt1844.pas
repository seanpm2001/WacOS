{

PRT test 1844: Variable reference to packed variable

   Passing a packed element as a variable reference.
   ISO 7185 6.6.3.3


}

program iso7185prt1844;

var r: packed record
          i: integer;
          b: boolean
       end;

procedure a(var b: boolean);

begin

   b := true

end;

begin

   a(r.b)

end.