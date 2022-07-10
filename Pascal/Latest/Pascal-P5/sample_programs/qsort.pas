program qsort(output);

const maxstr = 43;

type index = 1..maxstr;

var a: packed array [index] of char;

procedure sort(l, r: index);

var i, j: index; 
    x, w: char;

z: integer;

begin

    i := l;
    j := r;
    x := a[(l+r) div 2];
    repeat

        while a[i] < x do i := i+1;
        while x < a[j] do j := j-1;
        if i <= j then begin

            w := a[i]; a[i] := a[j]; a[j] := w;
            i := i+1;
            j := j-1

        end

    until i > j;
    if l < j then sort(l, j);
    if i < r then sort(i, r);

end;

begin

    a := 'erhklhklgsdtsknsknskdlhfksghskhskeljefhjgkh';
    sort(1, maxstr);
    writeln('Result: ', a);

end.