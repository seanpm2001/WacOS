{

PRT test 1843: Variable reference to tagfield.

   Pass of a tagfield as a variable reference.
   ISO 7185 6.6.3.3

}

program iso7185prt1843;

var r: record
          case b: boolean of
             true:  ();
             false: ();
       end;

procedure a(var b: boolean);

begin

   b := true

end;

begin

   a(r.b)

end.