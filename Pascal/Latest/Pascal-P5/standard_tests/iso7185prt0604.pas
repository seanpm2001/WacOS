{

PRT test 604: Missing ':' on parameter specification

}

program iso7185prt0604;

procedure a(b, c integer);

begin

   b := 1;
   c := 1

end;

begin

   a(1, 2)

end.
