(*$l-*)
{******************************************************************************
*                                                                             *
*                      TEST SUITE FOR ISO 7185 PASCAL                         *
*                                                                             *
*                       The "PASCAL ACCEPTANCE TEST"                          *
*                                                                             *
*                              Version 1.1                                    *
*                                                                             *
*            Copyright (C) 2010 S. A. Moore - All rights reserved             *
*                                                                             *
* This program attempts to use and display the results of each feature of     *
* standard pascal. It is a "positive" test in that it should compile and run  *
* error free, and thus does not check error conditions/detection.             *
*                                                                             *
* Each test is labeled and numbered, and the expected result also output, so  *
* that the output can be self evidently hand checked.                         *
*                                                                             *
* The output can be redirected to a printer or a file to facillitate such     *
* checking.                                                                   *
*                                                                             *
* The output can also be automatically checked by comparing a known good file *
* to the generated file. To this end, we have regularized the output,         *
* specifying all output field widths that are normally compiler dependent.    *
*                                                                             *
* Only the following factors exist that are allowed to be compiler dependent, *
* but could cause a miscompare of the output:                                 *
*                                                                             *
*    1. The case of output booleans. We have choosen a standard format of     *
*       LOWER case for such output. Note that compilers can choose any case,  *
*       or mixture of cases.                                                  *
*                                                                             *
* Because of this, it may be required to return to hand checking when         *
* encountering a differing compiler system.                                   *
*                                                                             *
* Notes:                                                                      *
*                                                                             *
* 1. This test will not run or compile unless "set of char" is possible.      *
* This does not mean that compilers lacking in "set of char" capability are   *
* not standard. However, in the authors opinion, this is a crippling          *
* limitation for any Pascal compiler.                                         *
*                                                                             *
* 2. Because there is no "close" function in ISO 7185 Pascal, the file        *
* handling contained with is likely to generate a large number of open        *
* temporary files. This may cause some implementations to trip a limit on the *
* number of total open files. If this occurs, turn the constant "testfile"    *
* below to "false". This will cause the temporary files test to be skipped.   *
*                                                                             *
* 3. The test assumes that both upper and lower case characters are           *
* available, both in source and in the text files that are processed. The     *
* ISO 7185 standard does not technically require this.                        *
*                                                                             *
* 4. The test assumes that the alternative tolkens @, (. and .) is assumed.   *
* In ISO 7185 6.1.9 "Lexical alternatives" is implementation defined.         *
*                                                                             *
* The following sections need to be completed:                                *
*                                                                             *
* 1. Buffer variables. The full suite of handing tests need to be applied to  *
* file buffer variables as well. This means all integer, character, boolean,  *
* etc.                                                                        *
*                                                                             *
* 2. Arrays, records and pointers containing files.                           *
*                                                                             *
* 3. Pointer variables, array variables, and other complex accesses need to   *
* subjected to the same extentive tests that base variables are.              *
*                                                                             *
* 4. Need a test for access to locals of a surrounding procedure. This tests  *
* access to a procedure that is local, but not in the same scope.             *
*                                                                             *
* 5. Need a dynamic storage test that allocates various sizes, not just       *
* integers.                                                                   *
*                                                                             *
******************************************************************************}

program iso7185pat(input, output);

label
      0, 3, 9999, 0004;

const

      { flags to control run }

      { the pointer torture test takes time and isn't run for interpreted
        systems }
      doptrtortst = false;
      
      tcnst = 768;
      scst = 'this is a string';
      ccst = 'v';
      tsncst = -52;
      rcnst = 43.33;
      rscst = -84.22;
      tsncst2 = -tcnst;
      tsncst3 = -tsncst;
      rscst2 = -rcnst;
      rscst3 = -rscst;
      testfile = true;
      mmaxint = -maxint;
      cone = 1;

type
     string10 = packed array [1..10] of char;
     enum  = (one, two, three, four, five, six, seven, eight, nine, ten);
     esub  = three..six;
     subr  = 10..20;
     (* Note use of alternatives for '[' and ']'. The availablity of these
        alternates is implementation defined. *)
     arri  = array (.1..10.) of integer;
     arrim = array [1..2, 1..2] of array [1..2, 1..2, 1..2, 1..2] of integer;
     cset  = set of char;
     { Note that the availability of the alternate '@' is implementation
       defined }
     iptr  = @integer;
     recs  = record

               a: integer;
               b: char

            end;
     rec = record

              i:   integer;
              b:   boolean;
              c:   char;
              e:   enum;
              es:  esub;
              s:   subr;
              r:   real;
              st:  string10;
              a:   arri;
              rc:  recs;
              stc: cset;
              p:   iptr

           end;
     prec = packed record

              i:   integer;
              b:   boolean;
              c:   char;
              e:   enum;
              es:  esub;
              s:   subr;
              r:   real;
              st:  string10;
              a:   arri;
              rc:  recs;
              stc: cset;
              p:   iptr

           end;
     recv = record

               a: integer;
               b: char;
               case c: boolean of

                  false: (d: string10);
                  true:  (e: enum)

               { end }

            end;
     recvb = record
     
                i: integer;
                case b: boolean of
              
                   true: (c: char);
                   false: (
                 
                      case q: boolean of
                    
                         true: (r: real);
                         false: (n: boolean)
                       
                   )
                       
             end;
     recvc = record

              case vt: subr of

                 10, 11, 12, 13, 14, 15: (vi: integer);
                 16, 17, 18, 19, 20: (vb: boolean)

              { end }

           end;
     recvd = record
     
               case z: boolean of
                    
                 true: (r: real);
                false: (n: boolean)
              
             end;
     recve = record
     
               case b: boolean of
              
                 true: (c: char);
                 false: (q: recvd; n: integer)
                       
             end;
     arrr = array [1..10] of recs;
     vart = (vti, vtb, vtc, vte, vtes, vts, vtr, vtst, vta, vtrc, vtstc, vtp);
     intalias = integer;

var
    i, x, y, z, q, n, t : integer;
    width: integer;
    mandig, expdig: integer;
    blt, blf:       integer;
    bcu, bcl:       boolean;
    srx, sry, srz: 0..100;
    sras, srbs, srcs, srds, sres: -100..100;
    a : array [1..10] of integer;
    r : record

           rx: integer;
           rc: char;
           ry: integer;
           rb: boolean;
           rs: packed array [1..10] of char;

        end;
    da:    array [1..10, 1..10] of integer;
    sa, sb, sc : packed array [1..10] of char;
    ca, cb, cc : char;
    car :  array ['a'..'z'] of integer;
    sar:   array [1..10] of packed array [1..10] of char;
    ba, bb, bc : boolean;
    sva, svb, svc : (mon, tue, wed, thur, fri, sat, sun);
    s:     string10;
    as, bs, cs, ds, es, gs, hs : integer;
    vnum: -maxint..maxint;
    ra, rb, rc, rd, re: real;
    sta,   stb, stc, std: set of 1..100;
    ste:   set of 1..10;
    stf:   packed set of 1..10;
    stg:   packed set of 1..20;
    csta,  cstb, cstc, cstd: set of char;
    cste:  set of 'a'..'z';
    cstf:  packed set of 'a'..'f';
    cstg:  packed set of char;
    ci:    char;
    sena,  senb, senc, send: set of enum;
    sene:  set of one..five;
    senf:  packed set of enum;
    seng:  packed set of one..seven;
    ei, ea: enum;
    sba,   sbb, sbc, sbd: set of boolean;
    sbe:   set of false..true;
    sbf:   packed set of boolean;
    sbg:   packed set of false..true;
    ai:    arri;
    arec:  rec;
    parec: prec;
    vrec:  recv;
    ip:    iptr;
    avi:   arri;
    avi2:  arri;
    pavi:  packed array [1..10] of integer;
    avis:  array [1..10] of 10..20;
    pavis: packed array [1..10] of 10..20;
    avb:   array [1..10] of boolean;
    pavb:  packed array [1..10] of boolean;
    avr:   array [1..10] of real;
    pavr:  packed array [1..10] of real;
    avc:   array [1..10] of char;
    pavc:  packed array [1..10] of char;
    avcs:  array [1..10] of 'g'..'p';
    pavcs: packed array [1..10] of 'g'..'p';
    ave:   array [1..10] of enum;
    pave:  packed array [1..10] of enum;
    aves:  array [1..10] of esub;
    paves: packed array [1..10] of esub;
    avs:   array [1..10] of cset;
    pavs:  packed array [1..10] of cset;
    avrc:  array [1..10] of recs;
    pavrc: packed array [1..10] of recs;
    avf:   array [1..10] of text;
    pavf:  packed array [1..10] of text;
    avp:   array [1..10] of iptr;
    pavp:  packed array [1..10] of iptr;
    bia:   array [boolean] of integer;
    pbia:  packed array [boolean] of integer;
    cia:   array [char] of integer;
    pcia:  packed array [char] of integer;
    csia:  array ['a'..'z'] of integer;
    pcsia: packed array ['a'..'z'] of integer;
    eia:   array [enum] of integer;
    peia:  packed array [enum] of integer;
    esia:  array [two..six] of integer;
    pesia: packed array [two..six] of integer;
    mdar:  arrim;
    mdar2: arrim;
    vra:   record

              i: integer;
              case vt: vart of

                 vti:   (vdi:   integer;  a: integer);
                 vtb:   (vdb:   boolean;  b: integer);
                 vtc:   (vdc:   char;     c: integer);
                 vte:   (vde:   enum;     d: integer);
                 vtes:  (vdes:  esub;     e: integer);
                 vts:   (vds:   subr;     f: integer);
                 vtr:   (vdr:   real;     g: integer);
                 vtst:  (vdst:  string10; h: integer);
                 vta:   (vda:   arri;     j: integer);
                 vtrc:  (vdrc:  recs;     k: integer);
                 vtstc: (vdstc: cset;     l: integer);
                 vtp:   (vdp:   iptr;     m: integer)

              { end }

           end;
    vvrs:  record

              case vt: subr of

                 10, 11, 12, 13, 14, 15: (vi: integer);
                 16, 17, 18, 19, 20: (vb: boolean)

              { end }

           end;
    vvrb:  record

              case vt:boolean of

                 true: (vi: integer);
                 false: (vb: boolean)

              { end }

           end;
    vvre:  record

              case vt: enum of

                 one, two, three, four, five: (vi: integer);
                 six, seven, eight, nine, ten: (vb: boolean)

              { end }

           end;
    vvres: record

              case vt: esub of

                 three, four: (vi: integer);
                 five, six: (vb: boolean)

              { end }

           end;
    nvr:   record

              i: integer;
              r: record

                 i: integer;
                 r: record

                    i: integer;
                    r: record

                       i: integer;
                       r: record

                          i: integer;
                          r: record

                             i: integer;
                             r: record

                                i: integer;
                                r: record

                                   i: integer;
                                   r: record

                                      i: integer;
                                      r: record

                                         i: integer

                                      end

                                   end

                                end

                             end

                          end

                       end

                    end

                 end

              end

           end;
    rpa:   ^rec;
    rpb:   ^recvb;
    rpc:   ^recvc;
    rpd:   ^recve;
    ara:   arrr;
    fi, fia, fib, fic, fid, fie, fig, fih: file of integer;
    pfi:   packed file of integer;
    fb:    file of boolean;
    pfb:   packed file of boolean;
    fc:    file of char;
    pfc:   packed file of char;
    fe:    file of enum;
    pfe:   packed file of enum;
    fes:   file of esub;
    pfes:  packed file of esub;
    fs:    file of subr;
    pfs:   packed file of subr;
    fr:    file of real;
    pfr:   packed file of real;
    fst:   file of string10;
    pfst:  packed file of string10;
    fa:    file of arri;
    pfa:   packed file of arri;
    frc:   file of recs;
    pfrc:  packed file of recs;
    fstc:  file of cset;
    pfstc: packed file of cset;
    fp:    file of iptr;
    pfp:   packed file of iptr;
    ft:    text;
    pti, pti1: ^integer;
    pti2:  iptr;
    ptb:   ^boolean;
    ptc:   ^char;
    pte:   ^enum;
    ptes:  ^esub;
    pts:   ^subr;
    ptr:   ^real;
    ptst:  ^string10;
    pta:   ^arri;
    ptrc:  ^recs;
    ptstc: ^cset;
    ptp:   ^iptr;
    ipa,       ipb, ipc, ipd, ipe: ^integer;
    iap:       array [1..100] of ^integer;
    rndseq:    integer;
    cnt, cnt2: integer;
    rn:        integer;
    rcastt: integer;
    rcast: record case rcastt: boolean of true: (); false: () end;
    pi1, pi2: ^integer;
    intaliasv: intalias;
    iso7185pat: integer;
    MyOwnInteger: integer;
    myvar: integer;
    myvarmyvar: integer;
    myvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar: integer;
    la1: array [maxint..maxint] of integer;
    la2: array [-maxint..-maxint] of integer;

procedure junk1(z, q : integer);

begin

   write(z:1, ' ', q:1);

end;

procedure junk2(var z : integer);

begin

   z := z + 1

end;

procedure junk3(var p : string10);

begin

   write(p)

end;

procedure junk4(p : string10);

begin

   p[5] := '?';
   write(p)

end;

function junk5(x : integer) : integer;

begin

   junk5 := x + 1

end;

procedure junk6;

begin

   goto 09999

end;

function junk7(a, b, c: integer): integer; forward;

function junk7;

var x, y, z: integer;

begin

   x := 1;
   y := 2;
   z := 3;
   write(a:1, ' ', b:1, ' ', c:1, ' ');
   a := 4;
   b := 5;
   c := 6;
   write(c:1, ' ', b:1, ' ', a:1, ' ', z:1, ' ', y:1, ' ', x:1);
   junk7 := 78

end;

procedure junk8(a: integer; b: boolean; c: char; e: enum; es: esub; s: subr;
                r: real; st: string10; ar: arri; rc: rec; rv: recv; stc: cset;
                p: iptr);

var i:  integer;
    ci: char;

begin

   writeln(a:1, ' ', b:5, ' ', c:1, ' ', ord(e):1, ' ', ord(es):1, ' ', s:1, ' ',
           r:15, ' ', st);
   for i := 1 to 10 do write(ar[i]:1, ' '); writeln;
   writeln(rc.i:1, ' ', rc.b:5, ' ', rc.c:1, ' ', ord(rc.e):1, ' ', ord(rc.es):1,
           ' ', rc.s:1, ' ', rc.r:15, ' ', rc.st);
   for i := 1 to 10 do write(rc.a[i]:1, ' '); writeln;
   writeln(rc.rc.a:1, ' ', rc.rc.b:1);
   for ci := 'a' to 'j' do if ci in rc.stc then write(ci) else write('_');
   writeln;
   writeln(rc.p^:1);
   writeln(rv.a:1, ' ', rv.b:1, ' ', rv.c:5);
   if rv.c then writeln(ord(rv.e):1) else writeln(rv.d);
   for ci := 'a' to 'j' do if ci in stc then write(ci) else write('_');
   writeln;
   writeln(p^:1)

end;

procedure junk9(procedure junk9(junk9, b: integer; c: char);
                function y(a: integer): integer);

begin

   junk9(9834, 8383, 'j');
   write(' ', y(743):1);

end;

procedure junk10(x, y: integer; junk10: char);

begin

   write(x:1, ' ', y:1, ' ', junk10:1)

end;

function junk11(x: integer): integer;

begin

   junk11 := succ(x)

end;

procedure junk12(procedure xq(function yq(z: integer): integer);
                 function q(n: integer): integer);

begin

   xq(q)

end;

procedure junk13(function xz(z: integer): integer);

begin

   write(xz(941):1)

end;

procedure junk14;

var i, x: integer;

procedure junk15;

begin

   write(i:1, ' ', x:1)

end;

begin

   i := 62;
   x := 76;
   junk15

end;

procedure junk16; begin end;

procedure junk17(procedure x; i: integer);

procedure junk18;

begin

 write(i:1)

end;

begin

   x;
   if i=52 then junk17(junk18, 83)

end;

{ test preference of pointer bonding to current scope }

procedure junk19;

type pt = ^intalias;
     intalias = char;

var p: pt;

begin

   new(p);
   p^ := 'a';
   write(p^);
   dispose(p)

end;

{ test ability to assign function result to nested function }

function junk20: integer;

var i: integer;

function inner: integer;

begin

   inner := 12;
   junk20 := 37

end;

begin

   i := inner

end;

function frp: iptr;

begin

   frp := pti2
   
end;
 
function random (low, hi : integer) : integer;

const a = 16807;
      m = 2147483647;

var gamma: integer;

begin
  gamma := a*(rndseq mod (m div a))-(m mod a)*(rndseq div (m div a));
  if gamma > 0 then rndseq := gamma else rndseq := gamma+m;
  random := rndseq div (maxint div (hi-low+1))+low
end {of random};

function junk21: integer;

var
  true:    1..10;
  false:   1..10;
  real:    1..10;
  boolean: 1..10;
  text:    1..10;
  abs:     1..10;
  sqr:     1..10;
  sqrt:    1..10;
  sin:     1..10;
  cos:     1..10;
  arctan:  1..10;
  ln:      1..10;
  exp:     1..10;
  trunc:   1..10;
  round:   1..10;
  ord:     1..10;
  chr:     1..10;
  succ:    1..10;
  pred:    1..10;
  odd:     1..10;
  eoln:    1..10;
  eof:     1..10;
  read:    1..10;
  readln:  1..10;
  write:   1..10;
  writeln: 1..10;
  rewrite: 1..10;
  reset:   1..10;
  put:     1..10;
  get:     1..10;
  page:    1..10;
  new:     1..10;
  dispose: 1..10;
  pack:    1..10;
  unpack:  1..10;

begin

  true    := 1;    
  false   := 1;   
  real    := 1;  
  boolean := 1; 
  text    := 1;    
  abs     := 1;     
  sqr     := 1;     
  sqrt    := 1;    
  sin     := 1;     
  cos     := 1;     
  arctan  := 1;  
  ln      := 1;      
  exp     := 1;     
  trunc   := 1;   
  round   := 1;   
  ord     := 1;     
  chr     := 1;     
  succ    := 1;    
  pred    := 1;    
  odd     := 1;     
  eoln    := 1;    
  eof     := 1;     
  read    := 1;    
  readln  := 1;  
  write   := 1;   
  writeln := 1; 
  rewrite := 1; 
  reset   := 1;   
  put     := 1;     
  get     := 1;     
  page    := 1;    
  new     := 1;     
  dispose := 1; 
  pack    := 1;    
  unpack  := 1;  
  
  junk21 := true+false+real+boolean+text+abs+sqr+sqrt+sin+cos+arctan+ln+
            exp+trunc+round+ord+chr+succ+pred+odd+eoln+eof+read+readln+write+
            writeln+rewrite+reset+put+get+page+new+dispose+pack+unpack 

end;

procedure junk22(n: subr);

var subr: integer;

begin

   subr := n;
   write(subr:1)
   
end;

{ measure length of first line in text file }

function linelength(var f: text): integer;

var c: char;
    t: integer;
    
begin

   reset(f);
   t := 0;
   while not eoln(f) do begin
   
      read(f, c);
      t := t+1
      
   end;
   
   linelength := t
   
end;

function expchar(var f: text): char;

var c, e: char;

begin

   reset(f);
   e := ' ';
   while not eoln(f) do begin
   
      read(f, c);
      if (c = 'e') or (c = 'E') then e := c;
      
   end;
   
   expchar := e
   
end;

procedure digitreal(var f: text; var man, exp: integer);

var expseen: boolean;
    c:       char;

begin

   man := 0;
   exp := 0;
   expseen := false;
   reset(f);
   while not eoln(f) do begin
   
      read(f, c);
      if (c = 'e') or (c = 'E') then expseen := true;
      if c in ['0'..'9'] then
         if expseen then exp := exp+1
         else man := man+1
      
   end
   
end;

procedure caseboolean(var f: text; var u, l: boolean);

var c: char;

begin

   u := false;
   l := false;
   reset(f);
   while not eoln(f) do begin
   
      read(f, c);
      if c in ['a'..'z'] then l := true;
      if c in ['A'..'Z'] then u := true;
      
   end
   
end;

begin

   write('****************************************************************');
   writeln('***************');
   writeln;
   writeln('                 TEST SUITE FOR ISO 7185 PASCAL');
   writeln;
   write('                 Copyright (C) 1995 S. A. FRANCO - All rights ');
   writeln('reserved');
   writeln;
   write('****************************************************************');
   writeln('***************');
   writeln;

{******************************************************************************

                          Reference dangling defines

******************************************************************************}

{ unused declarations are always a problem, because it is always concievable
  that there is a compiler test that will reveal they are not used. We use
  assign to references here because a simple read of a variable could fault
  on an undefined reference. Its also possible that a never used fault could
  occur (written, but never used), in which case the code would have to be
  more complex. The best solution, of course, is to write a real test that
  uses the variables. }

   a[1] :=  1;
   esia[two] := 1;
   pesia[two] := 1;
   rewrite(fes);
   rewrite(pfes);
   rewrite(fs);
   rewrite(pfs);
   rewrite(fr);
   rewrite(pfr);
   rewrite(fst);
   rewrite(pfst);
   rewrite(fa);
   rewrite(pfa);
   rewrite(frc);
   rewrite(pfrc);
   rewrite(fstc);
   rewrite(pfstc);
   rewrite(fp);
   rewrite(pfp);
   rcastt := 1;
   rcast.rcastt := true;
   intaliasv := 1;
   iso7185pat := 1;

{******************************************************************************

                                 Metering/implentation defined

******************************************************************************}

   writeln('The following are implementation defined characteristics');
   writeln;
   
   { integer}
   
   writeln('Maxint: ', maxint:1);
   i := maxint;
   x := 0;
   while i > 0 do begin i := i div 2;  x := x+1 end;
   writeln('Bit length of integer without sign bit appears to be: ', x:1);
   writeln('With sign bit: ', x+1:1);
   i := 42;
   writeln('Integer default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln(i);
   writeln(-i);
   rewrite(ft);
   writeln(ft, i);
   width := linelength(ft);
   writeln('The length of default value of TotalWidth for integer-type is: ', 
           width:1); 
   writeln('Leaving ', width-1:1, ' for digits plus a sign');
   
   { real }
   
   { Note the implementation will round this to its native precision, unless
     N- lengtgh reals are implemented. }
   ra := 1.234567890123456789012345678901234567890123456789012345678901234567890;
   writeln('Real default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln(ra);
           
   rewrite(ft);
   writeln(ft, ra);
   width := linelength(ft);
   writeln('The length of default value of TotalWidth for real-type is: ', 
           width:1);
   ca := expchar(ft);
   if ca = ' ' then writeln('*** The exponent character is invalid')
   else begin
   
      write('The exponent character is: "', ca, '", ');
      if ca = 'e' then writeln('lower case') else writeln('upper case')
      
   end;
   digitreal(ft, mandig, expdig);
   writeln('There are ', mandig:1, ' digits in the mantissa and ', expdig:1, 
           ' in the exponent');
           
   { boolean }
   writeln('Boolean default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln(false);
   writeln(true);
   ba := true;
   rewrite(ft);
   writeln(ft, ba);
   blt := linelength(ft);
   ba := false;
   rewrite(ft);
   writeln(ft, ba);
   blf := linelength(ft);
   if blt <> blf then 
      writeln('*** Lengths of default fields for true and false do not match')
   else writeln('TotalWidth for boolean-type is: ', blt:1);
   caseboolean(ft, bcu, bcl);
   if bcu and bcl then writeln('Boolean case is mixed')
   else if bcu then writeln('Boolean is upper case')
   else if bcl then writeln('Booleal is lower case')
   else writeln('*** boolean values are invalid');
   writeln('Note that the upper or lower case state of the characters in');
   writeln('''true'' and ''false'' are implementation defined');
   
   { char }
   writeln('Char default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln('a');
   if (ord('a') = 97) and (ord('(') = 40) and (ord('^') = 94) then
      writeln('Appears to be ASCII')
   else
      writeln('Appears to not be ASCII');
      
   { page }
   rewrite(ft);
   page(ft);
   width := linelength(ft);
   writeln('The page() procedure appears to result in ', width:1, 
           ' characters being output to the file');
   writeln('The characters in the page() sequence are (in decimal):');
   reset(ft);
   while not eoln(ft) do begin
   
      read(ft, ca);
      write(ord(ca):1);
      if not eoln(ft) then write(', ')
      
   end;
   writeln;
   writeln('Note that the page() procedure may perform its function with');
   writeln('out of band control sequences');

{******************************************************************************

                           Control structures

******************************************************************************}

   writeln;
   writeln('******************* Control structures tests *******************');
   writeln;
   write('Control1: ');
   for i := 1 to 10 do write(i:1, ' ');
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control2: ');
   for i := 10 downto 1 do write(i:1, ' ');
   writeln('s/b 10 9 8 7 6 5 4 3 2 1');
   write('Control3: ');
   i := 1;
   while i <=10{comment}do begin write(i:1, ' '); i := i + 1 end;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control4: ');
   i := 1; repeat write(i:1, ' '); i := i + 1 until i > 10;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control5: ');
   i := 1;{comment*)
   0: write(i:1, ' '); i := i + 1; if i <= 10 then goto 0;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control6: ');(*comment}
   if true then write('yes') else{comment}write('no');
   writeln(' s/b yes');
   write('Control7: ');
   if false then write('no') else write('yes');
   writeln(' s/b yes');
   write('Control8: ');
   if true then write('yes '); write('stop');
   writeln(' s/b yes stop');
   write('Control9: ');
   if false then write('no '); write('stop');
   writeln(' s/b stop');(*)comment*)
   write('Control10: ');
   for i := 1 to 10 do
      case i of
         1:     write('one ');
         2:     write('two ');
         3:     write('three ');
         4:     write('four ');
         5:     write('five ');
         6:     write('six ');
         7:     write('seven ');
         8:     write('eight ');
         9, 10: write('nine-ten ')

      end;
   writeln;
   write('Control10: s/b ');
   write('one two three four five ');
   writeln('six seven eight nine-ten nine-ten');
   write('Control11: start ');
   junk6;
   write('!! BAD !!');
   9999: writeln('stop s/b start stop');
   write('Control12: start ');
   goto 003;
   write('!! BAD !!');
   3: writeln('stop s/b start stop');
   write('Control13: start ');
   { self defined fors }
   i := 10;
   for i := 1 to i do write(i:3);
   writeln(' s/b start  1  2  3  4  5  6  7  8  9 10');
   write('Control14: start ');
   { self defined fors }
   i := 10;
   for i := i downto 1 do write(i:3);
   writeln(' s/b start 10  9  8  7  6  5  4  3  2  1');
   write('Control15: start ');
   { for against 0 }
   for i := 0 to 9 do write(i:2);
   writeln(' s/b start 0 1 2 3 4 5 6 7 8 9');
   write('Control16: start ');
   { for against 0 }
   for i := 9 downto 0 do write(i:2);
   writeln(' s/b start 9 8 7 6 5 4 3 2 1 0');
   { wide spread of case statements }
   write('Control17: start ');
   i := 10000;
   case i of{comment{comment}
      1: write('*** bad ***');
      10000: write('good')
   end;
   writeln(' start s/b start good');
   write('Control18: start ');
   repeat(*comment(*comment*)
      goto 004;
      write('!! BAD !!');
      4: writeln('stop s/b start stop');
      i := 0;
      if i <> 0 then goto 04;
   until true;

{******************************************************************************

                            Integers

******************************************************************************}

   writeln;
   writeln('******************* Integers *******************');
   writeln;

   { integer variables }
   x := 43; y := 78; z := y;
   writeln('Integer1:   ', x + y:1, ' s/b 121');
   writeln('Integer2:   ', y - x:1, ' s/b 35');
   writeln('Integer3:   ', x * y:1, ' s/b 3354');
   writeln('Integer4:   ', y div x:1, ' s/b 1');
   writeln('Integer5:   ', y mod x:1, ' s/b 35');
   writeln('Integer6:   ', succ(x):1, ' s/b 44');
   writeln('Integer7:   ', pred(x):1, ' s/b 42');
   writeln('Integer8:   ', sqr(x):1, ' s/b 1849');
   writeln('Integer9:   ', chr(y), ' s/b N');
   writeln('Integer10:  ', ord(chr(x)):1, ' s/b 43');
   writeln('Integer11:  ', odd(x):5, ' s/b true');
   writeln('Integer12:  ', odd(y):5, ' s/b false');
   writeln('Integer13:  ', z = y:5, ' s/b true');
   writeln('Integer14:  ', x = y:5, ' s/b false');
   writeln('Integer15:  ', x < y:5, ' s/b true');
   writeln('Integer16:  ', y < x:5, ' s/b false');
   writeln('Integer17:  ', y > x:5, ' s/b true');
   writeln('Integer18:  ', x > y:5, ' s/b false');
   writeln('Integer19:  ', x <> y:5, ' s/b true');
   writeln('Integer20:  ', y <> z:5, ' s/b false');
   writeln('Integer21:  ', x <= y:5, ' s/b true');
   writeln('Integer22:  ', z <= y:5, ' s/b true');
   writeln('Integer23:  ', y <= x:5, ' s/b false');
   writeln('Integer24:  ', y >= x:5, ' s/b true');
   writeln('Integer25:  ', y >= z:5, ' s/b true');
   writeln('Integer26:  ', x >= y:5, ' s/b false');

   { unsigned integer constants }
   write('Integer27:  '); i := 546; writeln(i:1, ' s/b 546');
   writeln('Integer28:  ', 56 + 34:1, ' s/b 90');
   writeln('Integer29:  ', 56 - 34:1, ' s/b 22');
   writeln('Integer30:  ', 056 * 34:1, ' s/b 1904');
   writeln('Integer31:  ', 56 div 34:1, ' s/b 1');
   writeln('Integer32:  ', 00000056 mod 34:1, ' s/b 22');
   writeln('Integer33:  ', succ(5):1, ' s/b 6');
   writeln('Integer34:  ', pred(5):1, ' s/b 4');
   writeln('Integer35:  ', sqr(7):1, ' s/b 49');
   writeln('Integer36:  ', chr(65), ' s/b A');
   writeln('Integer37:  ', ord(chr(65)):1, ' s/b 65');
   writeln('Integer38:  ', tcnst:1, ' s/b 768');
   writeln('Integer39:  ', odd(5):5, ' s/b true');
   writeln('Integer40:  ', odd(8):5, ' s/b false');
   writeln('Integer41:  ', 56 = 56:5, ' s/b true');
   writeln('Integer42:  ', 56 = 57:5, ' s/b false');
   writeln('Integer43:  ', 56 < 57:5, ' s/b true');
   writeln('Integer44:  ', 57 < 56:5, ' s/b false');
   writeln('Integer45:  ', 57 > 56:5, ' s/b true');
   writeln('Integer46:  ', 56 > 57:5, ' s/b false');
   writeln('Integer47:  ', 56 <> 57:5, ' s/b true');
   writeln('Integer48:  ', 56 <> 56:5, ' s/b false');
   writeln('Integer49:  ', 55 <= 500:5, ' s/b true');
   writeln('Integer50:  ', 67 <= 67:5, ' s/b true');
   writeln('Integer51:  ', 56 <= 33:5, ' s/b false');
   writeln('Integer52:  ', 645 >= 4:5, ' s/b true');
   writeln('Integer53:  ', 23 >= 23:5, ' s/b true');
   writeln('Integer54:  ', 45 >= 123:5, ' s/b false');

   { signed integer variables }
   as := -14;
   bs := -32;
   cs := -14;
   ds := 20;
   es := -15;
   gs := maxint;
   hs := mmaxint;
   vnum := -maxint;
   writeln('Integer55:  ', as + ds:1, ' s/b 6');
   writeln('Integer56:  ', ds + as:1, ' s/b 6');
   writeln('Integer57:  ', bs + ds:1, ' s/b -12');
   writeln('Integer58:  ', as + bs:1, ' s/b -46');
   writeln('Integer59:  ', ds - as:1, ' s/b 34');
   writeln('Integer60:  ', bs - ds:1, ' s/b -52');
   writeln('Integer61:  ', bs - as:1, ' s/b -18');
   writeln('Integer62:  ', ds * as:1, ' s/b -280');
   writeln('Integer63:  ', as * ds:1, ' s/b -280');
   writeln('Integer64:  ', as * bs:1, ' s/b 448');
   writeln('Integer65:  ', ds div as:1, ' s/b -1');
   writeln('Integer66:  ', bs div ds:1, ' s/b -1');
   writeln('Integer67:  ', bs div as:1, ' s/b 2');
   writeln('Integer68:  ', succ(as):1, ' s/b -13');
   writeln('Integer69:  ', pred(bs):1, ' s/b -33');
   writeln('Integer70: ', sqr(as):1, ' s/b 196');
   writeln('Integer71:  ', odd(as):5, ' s/b false');
   writeln('Integer72:  ', odd(es):5, ' s/b true');
   writeln('Integer73:  ', as = cs:5, ' s/b true');
   writeln('Integer74:  ', as = bs:5, ' s/b false');
   writeln('Integer75:  ', as <> bs:5, ' s/b true');
   writeln('Integer76:  ', as <> cs:5, ' s/b false');
   writeln('Integer77:  ', as < ds:5, ' s/b true');
   writeln('Integer78:  ', bs < as:5, ' s/b true');
   writeln('Integer79:  ', ds < as:5, ' s/b false');
   writeln('Integer80:  ', as < bs:5, ' s/b false');
   writeln('Integer81:  ', ds > as:5, ' s/b true');
   writeln('Integer82:  ', as > bs:5, ' s/b true');
   writeln('Integer83:  ', as > ds:5, ' s/b false');
   writeln('Integer84:  ', bs > as:5, ' s/b false');
   writeln('Integer85:  ', as <= ds:5, ' s/b true');
   writeln('Integer86:  ', bs <= as:5, ' s/b true');
   writeln('Integer87:  ', as <= cs:5, ' s/b true');
   writeln('Integer88:  ', ds <= as:5, ' s/b false');
   writeln('Integer89:  ', as <= bs:5, ' s/b false');
   writeln('Integer90:  ', ds >= as:5, ' s/b true');
   writeln('Integer91:  ', as >= bs:5, ' s/b true');
   writeln('Integer92:  ', as >= cs:5, ' s/b true');
   writeln('Integer93:  ', as >= ds:5, ' s/b false');
   writeln('Integer94:  ', bs >= as:5, ' s/b false');
   writeln('Integer95:  ', abs(as):1, ' s/b 14');
   writeln('Integer96:  ', gs+hs:1, ' s/b 0');
   writeln('Integer97:  ', gs-maxint:1, ' s/b 0');
   writeln('Integer98:  ', gs+vnum:1, ' s/b 0');

   { signed integer constants }
   writeln('Integer99:  ', 45 + (-30):1, ' s/b 15');
   writeln('Integer100:  ', -25 + 70:1, ' s/b 45');
   writeln('Integer101: ', -62 + 23:1, ' s/b -39');
   writeln('Integer102: ', -20 + (-15):1, ' s/b -35');
   writeln('Integer103: ', 20 - (-14):1, ' s/b 34');
   writeln('Integer104: ', -34 - 14:1, ' s/b -48');
   writeln('Integer105: ', -56 - (-12):1, ' s/b -44');
   writeln('Integer106: ', 5 * (-4):1, ' s/b -20');
   writeln('Integer107: ', (-18) * 7:1, ' s/b -126');
   writeln('Integer108: ', (-40) * (-13):1, ' s/b 520');
   writeln('Integer109: ', 30 div (-5):1, ' s/b -6');
   writeln('Integer110: ', (-50) div 2:1, ' s/b -25');
   writeln('Integer111: ', (-20) div (-4):1, ' s/b 5');
   writeln('Integer112: ', succ(-10):1, ' s/b -9');
   writeln('Integer113: ', succ(-1):1, ' s/b 0');
   writeln('Integer114: ', pred(-1):1, ' s/b -2');
   writeln('Integer115: ', sqr(-8):1, ' s/b 64');
   writeln('Integer116: ', pred(-54):1, ' s/b -55');
   writeln('Integer117: ', odd(-20):5, ' s/b false');
   writeln('Integer118: ', odd(-15):5, ' s/b true');
   writeln('Integer119: ', -5 = -5:5, ' s/b true');
   writeln('Integer120: ', -5 = 5:5, ' s/b false');
   writeln('Integer121: ', -21 <> -40:5, ' s/b true');
   writeln('Integer122: ', -21 <> -21:5, ' s/b false');
   writeln('Integer123: ', -3 < 5:5, ' s/b true');
   writeln('Integer124: ', -32 < -20:5, ' s/b true');
   writeln('Integer125: ', 20 < -20:5, ' s/b false');
   writeln('Integer126: ', -15 < -40:5, ' s/b false');
   writeln('Integer127: ', 70 > -4:5, ' s/b true');
   writeln('Integer128: ', -23 > -34:5, ' s/b true');
   writeln('Integer129: ', -5 > 5:5, ' s/b false');
   writeln('Integer130: ', -60 > -59:5, ' s/b false');
   writeln('Integer131: ', -12 <= 4:5, ' s/b true');
   writeln('Integer132: ', -14 <= -5:5, ' s/b true');
   writeln('Integer133: ', -7 <= -7:5, ' s/b true');
   writeln('Integer134: ', 5 <= -5:5, ' s/b false');
   writeln('Integer135: ', -10 <= -20:5, ' s/b false');
   writeln('Integer136: ', 9 >= -3:5, ' s/b true');
   writeln('Integer137: ', -4 >= -10:5, ' s/b true');
   writeln('Integer138: ', -13 >= -13:5, ' s/b true');
   writeln('Integer139: ', -6 >= 6:5, ' s/b false');
   writeln('Integer140: ', -20 >= -10:5, ' s/b false');
   writeln('Integer141: ', abs(-6):1, ' s/b 6');
   writeln('Integer142: ', tsncst:1, ' s/b -52');
   writeln('Integer143: ', -tsncst:1, ' s/b 52');
   writeln('Integer144: ', tsncst2:1, ' s/b -768');
   writeln('Integer145: ', tsncst3:1, ' s/b 52');
   writeln('Integer146: ', maxint+mmaxint:1, ' s/b 0');
   
   { other integer }
   myowninteger := 42;
   writeln('Integer147: ', mYowNintegeR:1, ' s/b 42');
   myvar := 1;
   myvarmyvar := 2;
   myvarmyvarmyvar := 3;
   myvarmyvarmyvarmyvar := 4;
   myvarmyvarmyvarmyvarmyvar := 5;
   myvarmyvarmyvarmyvarmyvarmyvar := 6;
   myvarmyvarmyvarmyvarmyvarmyvarmyvar := 7;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 8;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 9;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 10;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 11;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 12;
   myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar := 13;
   writeln('Integer148: ', 
      myvar:1, ' ',
      myvarmyvar:1, ' ',
      myvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1, ' ',
      myvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvarmyvar:1,
      ' s/b 1 2 3 4 5 6 7 8 9 10 11 12 13');

{******************************************************************************

                            Subranges

******************************************************************************}

   writeln;
   writeln('******************* Subranges *******************');
   writeln;

   { subrange unsigned variables }
   srx := 43; sry := 78; srz := sry;
   writeln('Subrange1:   ', srx + sry:1, ' s/b 121');
   writeln('Subrange2:   ', sry - srx:1, ' s/b 35');
   writeln('Subrange3:   ', srx * sry:1, ' s/b 3354');
   writeln('Subrange4:   ', sry div srx:1, ' s/b 1');
   writeln('Subrange5:   ', sry mod srx:1, ' s/b 35');
   writeln('Subrange6:   ', succ(srx):1, ' s/b 44');
   writeln('Subrange7:   ', pred(srx):1, ' s/b 42');
   writeln('Subrange8:   ', chr(sry), ' s/b N');
   writeln('Subrange9:   ', ord(chr(srx)):1, ' s/b 43');
   writeln('Subrange10:  ', odd(srx):5, ' s/b true');
   writeln('Subrange11:  ', odd(sry):5, ' s/b false');
   writeln('Subrange12:  ', srz = sry:5, ' s/b true');
   writeln('Subrange13:  ', srx = sry:5, ' s/b false');
   writeln('Subrange14:  ', srx < sry:5, ' s/b true');
   writeln('Subrange15:  ', sry < srx:5, ' s/b false');
   writeln('Subrange16:  ', sry > srx:5, ' s/b true');
   writeln('Subrange17:  ', srx > sry:5, ' s/b false');
   writeln('Subrange18:  ', srx <> sry:5, ' s/b true');
   writeln('Subrange19:  ', sry <> srz:5, ' s/b false');
   writeln('Subrange20:  ', srx <= sry:5, ' s/b true');
   writeln('Subrange21:  ', srz <= sry:5, ' s/b true');
   writeln('Subrange22:  ', sry <= srx:5, ' s/b false');
   writeln('Subrange23:  ', sry >= srx:5, ' s/b true');
   writeln('Subrange24:  ', sry >= srz:5, ' s/b true');
   writeln('Subrange25:  ', srx >= sry:5, ' s/b false');

   { signed subrange variables }
   sras := -14;
   srbs := -32;
   srcs := -14;
   srds := 20;
   sres := -15;
   writeln('Subrange26:  ', sras + srds:1, ' s/b 6');
   writeln('Subrange27:  ', srds + sras:1, ' s/b 6');
   writeln('Subrange28:  ', srbs + srds:1, ' s/b -12');
   writeln('Subrange29:  ', sras + srbs:1, ' s/b -46');
   writeln('Subrange30:  ', srds - sras:1, ' s/b 34');
   writeln('Subrange31:  ', srbs - srds:1, ' s/b -52');
   writeln('Subrange32:  ', srbs - sras:1, ' s/b -18');
   writeln('Subrange33:  ', srds * sras:1, ' s/b -280');
   writeln('Subrange34:  ', sras * srds:1, ' s/b -280');
   writeln('Subrange35:  ', sras * srbs:1, ' s/b 448');
   writeln('Subrange36:  ', srds div sras:1, ' s/b -1');
   writeln('Subrange37:  ', srbs div srds:1, ' s/b -1');
   writeln('Subrange38:  ', srbs div sras:1, ' s/b 2');
   writeln('Subrange39:  ', succ(sras):1, ' s/b -13');
   writeln('Subrange40:  ', pred(srbs):1, ' s/b -33');
   writeln('Subrange41:  ', odd(sras):5, ' s/b false');
   writeln('Subrange42:  ', odd(sres):5, ' s/b true');
   writeln('Subrange43:  ', sras = srcs:5, ' s/b true');
   writeln('Subrange44:  ', sras = srbs:5, ' s/b false');
   writeln('Subrange45:  ', sras <> srbs:5, ' s/b true');
   writeln('Subrange46:  ', sras <> srcs:5, ' s/b false');
   writeln('Subrange47:  ', sras < srds:5, ' s/b true');
   writeln('Subrange48:  ', srbs < sras:5, ' s/b true');
   writeln('Subrange49:  ', srds < sras:5, ' s/b false');
   writeln('Subrange50:  ', sras < srbs:5, ' s/b false');
   writeln('Subrange51:  ', srds > sras:5, ' s/b true');
   writeln('Subrange52:  ', sras > srbs:5, ' s/b true');
   writeln('Subrange53:  ', sras > srds:5, ' s/b false');
   writeln('Subrange54:  ', srbs > sras:5, ' s/b false');
   writeln('Subrange55:  ', sras <= srds:5, ' s/b true');
   writeln('Subrange56:  ', srbs <= sras:5, ' s/b true');
   writeln('Subrange57:  ', sras <= srcs:5, ' s/b true');
   writeln('Subrange58:  ', srds <= sras:5, ' s/b false');
   writeln('Subrange59:  ', sras <= srbs:5, ' s/b false');
   writeln('Subrange60:  ', srds >= sras:5, ' s/b true');
   writeln('Subrange61:  ', sras >= srbs:5, ' s/b true');
   writeln('Subrange62:  ', sras >= srcs:5, ' s/b true');
   writeln('Subrange63:  ', sras >= srds:5, ' s/b false');
   writeln('Subrange64:  ', srbs >= sras:5, ' s/b false');
   writeln('Subrange65:  ', abs(sras):1, ' s/b 14');

{******************************************************************************

                         Characters

******************************************************************************}

   writeln;
   writeln('******************* Characters*******************');
   writeln;

   { character variables }
   ca := 'g'; cb := 'g'; cc := 'u';
   writeln('Character1:   ', ca, ' ', cb, ' ', cc, ' s/b g g u');
   writeln('Character2:   ', succ(ca), ' s/b h');
   writeln('Character3:   ', pred(cb), ' s/b f');
   writeln('Character4:   ', ord(ca):1, ' s/b 103');
   writeln('Character5:   ', chr(ord(cc)), ' s/b u');
   writeln('Character6:   ', ca = cb:5, ' s/b true');
   writeln('Character7:   ', ca = cc:5, ' s/b false');
   writeln('Character8:   ', ca < cc:5, ' s/b true');
   writeln('Character9:   ', cc < ca:5, ' s/b false');
   writeln('Character10:  ', cc > ca:5, ' s/b true');
   writeln('Character11:  ', ca > cc:5, ' s/b false');
   writeln('Character12:  ', ca <> cc:5, ' s/b true');
   writeln('Character13:  ', ca <> cb:5, ' s/b false');
   writeln('Character14:  ', ca <= cc:5, ' s/b true');
   writeln('Character15:  ', ca <= cb:5, ' s/b true');
   writeln('Character16:  ', cc <= ca:5, ' s/b false');
   writeln('Character17:  ', cc >= cb:5, ' s/b true');
   writeln('Character18:  ', cb >= ca:5, ' s/b true');
   writeln('Character19:  ', cb >= cc:5, ' s/b false');
   sa := 'porker    '; sb := 'porker    '; sc := 'parker    ';
   writeln('Character20:  ', sa, sb, sc,
      ' s/b porker    porker    parker');
   writeln('Character21:  ', sa = sb:5, ' s/b true');
   writeln('Character22:  ', sa = sc:5, ' s/b false');
   writeln('Character23:  ', sc < sa:5, ' s/b true');
   writeln('Character24:  ', sa < sc:5, ' s/b false');
   writeln('Character25:  ', sa > sc:5, ' s/b true');
   writeln('Character26:  ', sc > sa:5, ' s/b false');
   writeln('Character27:  ', sa <> sc:5, ' s/b true');
   writeln('Character28:  ', sa <> sb:5, ' s/b false');
   writeln('Character29:  ', sc <= sa:5, ' s/b true');
   writeln('Character30:  ', sa <= sb:5, ' s/b true');
   writeln('Character40:  ', sa <= sc:5, ' s/b false');
   writeln('Character41:  ', sa >= sc:5, ' s/b true');
   writeln('Character42:  ', sa >= sb:5, ' s/b true');
   writeln('Character43:  ', sc >= sa:5, ' s/b false');
   write('Character44:  ');
   for ca := 'a' to 'z' do write(ca);
   writeln(' s/b abcdefghijklmnopqrstuvwxyz');
   write('Character45:  ');
   for ca := 'z' downto 'a' do write(ca);
   writeln(' s/b zyxwvutsrqponmlkjihgfedcba');
   write('Character46:  ');
   x := 0;
   for ca := 'a' to 'z' do begin car[ca] := x; x := x + 1 end;
   for ca := 'z' downto 'a' do write(car[ca]:1, ' ');
   writeln;
   writeln('Character46: s/b 25 24 23 22 21 20 19 18 17 16 15',
      ' 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0');
   r.rc := 'n'; writeln('Character47: ', r.rc, ' s/b n');
   r.rs := 'junky01234'; writeln('Character48: ', r.rs,
                           ' s/b junky01234');
   for i := 1 to 10 do sar[i] := '0123456789';
   sar[1] := 'trash     ';
   sar[2] := 'finnork   ';
   sar[10] := 'crapola   ';
   writeln('Character49:  ');
   for i := 10 downto 1 do writeln(sar[i]);
   writeln('Character49: s/b');
   writeln('crapola');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('finnork');
   writeln('trash');
   writeln('Character50:  ');
   for ca := '0' to '9' do
   begin
     case ca of
       '5': write('five ');
       '3': write('three ');
       '6': write('six ');
       '8': write('eight ');
       '0': write('zero ');
       '9': write('nine ');
       '7': write('seven ');
       '4': write('four ');
       '1': write('one ');
       '2': write('two ');
     end
   end;
   writeln;
   writeln(' s/b zero one two three four five six ',
           'seven eight nine');

   { character constants }
   writeln('Character51:  ', 'a', ' s/b a');
   writeln('Character52:  ', succ('a'), ' s/b b');
   writeln('Character53:  ', pred('z'), ' s/b y');
   writeln('Character54:  ', ord('c'):1, ' s/b 99');
   writeln('Character55:  ', chr(ord('g')), ' s/b g');
   writeln('Character56:  ', 'q' = 'q':5, ' s/b true');
   writeln('Character57:  ', 'r' = 'q':5, ' s/b false');
   writeln('Character58:  ', 'b' < 't':5, ' s/b true');
   writeln('Character59:  ', 'g' < 'c':5, ' s/b false');
   writeln('Character60:  ', 'f' > 'e':5, ' s/b true');
   writeln('Character61:  ', 'f' > 'g':5, ' s/b false');
   writeln('Character62:  ', 'h' <> 'l':5, ' s/b true');
   writeln('Character63:  ', 'i' <> 'i':5, ' s/b false');
   writeln('Character64:  ', 'v' <= 'y':5, ' s/b true');
   writeln('Character65:  ', 'y' <= 'y':5, ' s/b true');
   writeln('Character66:  ', 'z' <= 'y':5, ' s/b false');
   writeln('Character67:  ', 'l' >= 'b':5, ' s/b true');
   writeln('Character68:  ', 'l' >= 'l':5, ' s/b true');
   writeln('Character69:  ', 'l' >= 'm':5, ' s/b false');
   writeln('Character70:  ', 'finnork' = 'finnork':5, ' s/b true');
   writeln('Character71:  ',
      'finoork' = 'finnork':5, ' s/b false');
   writeln('Character72:  ', 'oliab' < 'olibb':5, ' s/b true');
   writeln('Character73:  ', 'olibb' < 'oliab':5, ' s/b false');
   writeln('Character74:  ', 'olibb' > 'oliab':5, ' s/b true');
   writeln('Character75:  ', 'oliab' > 'olibb':5, ' s/b false');
   writeln('Character76:  ', 'fark ' <> 'farks':5, ' s/b true');
   writeln('Character77:  ', 'farks' <> 'farks':5, ' s/b false');
   writeln('Character78:  ', 'farka' <= 'farkz':5, ' s/b true');
   writeln('Character79:  ', 'farks' <= 'farks':5, ' s/b true');
   writeln('Character80:  ', 'farkz' <= 'farks':5, ' s/b false');
   writeln('Character81:  ', 'topnat' >= 'topcat':5, ' s/b true');
   writeln('Character82:  ', 'topcat' >= 'topcat':5, ' s/b true');
   writeln('Character83:  ', 'topcat' >= 'topzat':5, ' s/b false');
   writeln('Character84:  ', scst, ' s/b this is a string');
   writeln('Character85:  ', ccst, ' s/b v');
   writeln('Character86:  ');
   for i := 15 downto 1 do writeln('hello, world': i);
   writeln('s/b:');
   writeln('   hello, world');
   writeln('  hello, world');
   writeln(' hello, world ');
   writeln('hello, world');
   writeln('hello, worl');
   writeln('hello, wor');
   writeln('hello, wo');
   writeln('hello, w');
   writeln('hello, ');
   writeln('hello,');
   writeln('hello');
   writeln('hell');
   writeln('hel');
   writeln('he');
   writeln('h');
   
   { ordering }
   writeln('Character87: ');
   write(succ('0') = '1', ' ');
   write(succ('1') = '2', ' ');
   write(succ('2') = '3', ' ');
   write(succ('3') = '4', ' ');
   write(succ('4') = '5', ' ');
   write(succ('5') = '6', ' ');
   write(succ('6') = '7', ' ');
   write(succ('7') = '8', ' ');
   writeln(succ('8') = '9', ' ');
   writeln('s/b');
   writeln(' true  true  true  true  true  true  true  true  true');
   
   { Note it is possible for only one case to be present, but likely this whole
     test would fail if that were true }
   writeln('Character88:');
   write('a' < 'b', ' ');
   write('b' < 'c', ' ');
   write('c' < 'd', ' ');
   write('d' < 'e', ' ');
   write('e' < 'f', ' ');
   write('f' < 'g', ' ');
   write('g' < 'h', ' ');
   write('h' < 'i', ' ');
   write('i' < 'j', ' ');
   writeln('j' < 'k', ' ');
   write('k' < 'l', ' ');
   write('l' < 'm', ' ');
   write('m' < 'n', ' ');
   write('n' < 'o', ' ');
   write('o' < 'p', ' ');
   write('p' < 'q', ' ');
   write('q' < 'r', ' ');
   write('r' < 's', ' ');
   write('s' < 't', ' ');
   writeln('t' < 'u', ' ');
   write('u' < 'v', ' ');
   write('v' < 'w', ' ');
   write('w' < 'x', ' ');
   write('x' < 'y', ' ');
   writeln('y' < 'z', ' ');
   writeln('s/b');
   writeln(' true  true  true  true  true  true  true  true  true  true');
   writeln(' true  true  true  true  true  true  true  true  true  true');
   writeln(' true  true  true  true  true');
   writeln('Character89:');
   write('A' < 'B', ' ');
   write('B' < 'C', ' ');
   write('C' < 'D', ' ');
   write('D' < 'E', ' ');
   write('E' < 'F', ' ');
   write('F' < 'G', ' ');
   write('G' < 'H', ' ');
   write('H' < 'I', ' ');
   write('I' < 'J', ' ');
   writeln('J' < 'K', ' ');
   write('K' < 'L', ' ');
   write('L' < 'M', ' ');
   write('M' < 'N', ' ');
   write('N' < 'O', ' ');
   write('O' < 'P', ' ');
   write('P' < 'Q', ' ');
   write('Q' < 'R', ' ');
   write('R' < 'S', ' ');
   write('S' < 'T', ' ');
   writeln('T' < 'U', ' ');
   write('U' < 'V', ' ');
   write('V' < 'W', ' ');
   write('W' < 'X', ' ');
   write('X' < 'Y', ' ');
   writeln('Y' < 'Z', ' ');
   writeln('s/b');
   writeln(' true  true  true  true  true  true  true  true  true  true');
   writeln(' true  true  true  true  true  true  true  true  true  true');
   writeln(' true  true  true  true  true');

{******************************************************************************

                            Booleans

******************************************************************************}

   writeln;
   writeln('******************* Booleans *******************');
   writeln;

   { boolean variables }
   ba := true; bb := false; bc := true;
   writeln('Boolean1:   ', ba:5, ' ', bb:5, ' s/b true false');
   writeln('Boolean2:   ', succ(bb):5, ' s/b true');
   writeln('Boolean3:   ', pred(ba):5, ' s/b false');
   writeln('Boolean4:   ', ord(bb):1, ' s/b 0');
   writeln('Boolean5:   ', ord(ba):1, ' s/b 1');
   writeln('Boolean6:   ', ba = bc:5, ' s/b true');
   writeln('Boolean7:   ', bb = bb:5, ' s/b true');
   writeln('Boolean8:   ', ba = bb:5, ' s/b false');
   writeln('Boolean9:   ', bb < ba:5, ' s/b true');
   writeln('Boolean10:  ', ba < bb:5, ' s/b false');
   writeln('Boolean11:  ', ba > bb:5, ' s/b true');
   writeln('Boolean12:  ', bb > ba:5, ' s/b false');
   writeln('Boolean13:  ', ba <> bb:5, ' s/b true');
   writeln('Boolean14:  ', ba <> bc:5, ' s/b false');
   writeln('Boolean15:  ', bb <= ba:5, ' s/b true');
   writeln('Boolean16:  ', ba <= bc:5, ' s/b true');
   writeln('Boolean17:  ', ba <= bb:5, ' s/b false');
   writeln('Boolean18:  ', ba >= bb:5, ' s/b true');
   writeln('Boolean19:  ', bb >= bb:5, ' s/b true');
   writeln('Boolean20:  ', bb >= ba:5, ' s/b false');
   write('Boolean21:  ');
   for ba := false to true do write(ba:5, ' ');
   writeln('s/b false true');
   write('Boolean22:  ');
   for bb := true downto false do write(bb:5, ' ');
   writeln('s/b true false');
   write('Boolean23:  ');
   ba := 1 > 0; writeln(ba:5, ' s/b true');
   write('Boolean24:  ');
   ba := 1 < 0; writeln(ba:5, ' s/b false');

   { boolean constants }
   writeln('Boolean25:  ', true:5, ' ', false:5, ' s/b true false');
   writeln('Boolean26:  ', succ(false):5, ' s/b true');
   writeln('Boolean27:  ', pred(true):5, ' s/b false');
   writeln('Boolean28:  ', ord(false):1, ' s/b 0');
   writeln('Boolean29:  ', ord(true):1, ' s/b 1');
   writeln('Boolean30:  ', true = true:5, ' s/b true');
   writeln('Boolean31:  ', false = false:5, ' s/b true');
   writeln('Boolean32:  ', true = false:5, ' s/b false');
   writeln('Boolean33:  ', false < true:5, ' s/b true');
   writeln('Boolean34:  ', true < false:5, ' s/b false');
   writeln('Boolean35:  ', true > false:5, ' s/b true');
   writeln('Boolean36:  ', false > true:5, ' s/b false');
   writeln('Boolean37:  ', true <> false:5, ' s/b true');
   writeln('Boolean38:  ', true <> true:5, ' s/b false');
   writeln('Boolean39:  ', false <= true:5, ' s/b true');
   writeln('Boolean40:  ', true <= true:5, ' s/b true');
   writeln('Boolean41:  ', true <= false:5, ' s/b false');
   writeln('Boolean42:  ', true >= false:5, ' s/b true');
   writeln('Boolean43:  ', false >= false:5, ' s/b true');
   writeln('Boolean44:  ', false >= true:5, ' s/b false');
   writeln('Boolean45:');
   for i := 10 downto 1 do writeln(false:i);
   writeln('Boolean45: s/b:');
   writeln('     false');
   writeln('    false');
   writeln('   false');
   writeln('  false');
   writeln(' false');
   writeln('false');
   writeln('fals');
   writeln('fal');
   writeln('fa');
   writeln('f');
   writeln('Boolean46:');
   for i := 10 downto 1 do writeln(true:i);
   writeln('Boolean46: s/b:');
   writeln('      true');
   writeln('     true');
   writeln('    true');
   writeln('   true');
   writeln('  true');
   writeln(' true');
   writeln('true');
   writeln('tru');
   writeln('tr');
   writeln('t');


{******************************************************************************

                            Scalar variables

******************************************************************************}

   writeln;
   writeln('******************* Scalar *******************');
   writeln;

   { scalar variables }
   sva := wed; svb := mon; svc := wed;
   writeln('Scalar1:   ', succ(svb) = tue:5, ' s/b true');
   writeln('Scalar2:   ', pred(sva) = tue:5, ' s/b true');
   writeln('Scalar3:   ', ord(svb):1, ' s/b 0');
   writeln('Scalar4:   ', ord(sva):1, ' s/b 2');
   writeln('Scalar5:   ', sva = svc:5, ' s/b true');
   writeln('Scalar6:   ', svb = svb:5, ' s/b true');
   writeln('Scalar7:   ', sva = svb:5, ' s/b false');
   writeln('Scalar8:   ', svb < sva:5, ' s/b true');
   writeln('Scalar9:   ', sva < svb:5, ' s/b false');
   writeln('Scalar10:  ', sva > svb:5, ' s/b true');
   writeln('Scalar11:  ', svb > sva:5, ' s/b false');
   writeln('Scalar12:  ', sva <> svb:5, ' s/b true');
   writeln('Scalar13:  ', sva <> svc:5, ' s/b false');
   writeln('Scalar14:  ', svb <= sva:5, ' s/b true');
   writeln('Scalar15:  ', sva <= svc:5, ' s/b true');
   writeln('Scalar16:  ', sva <= svb:5, ' s/b false');
   writeln('Scalar17:  ', sva >= svb:5, ' s/b true');
   writeln('Scalar18:  ', svb >= svb:5, ' s/b true');
   writeln('Scalar19:  ', svb >= sva:5, ' s/b false');
   write('Scalar20:  ');
   for sva := mon to sun do write(ord(sva):1, ' ');
   writeln('s/b 0 1 2 3 4 5 6');
   write('Scalar21:  ');
   for svb := sun downto mon do write(ord(svb):1, ' ');
   writeln('s/b 6 5 4 3 2 1 0');

   { scalar constants }
   writeln('Scalar20:   ', succ(mon) = tue:5, ' s/b true');
   writeln('Scalar21:   ', pred(fri) = thur:5, ' s/b true');
   writeln('Scalar22:   ', ord(wed):1, ' s/b 2');
   writeln('Scalar23:   ', ord(sun):1, ' s/b 6');
   writeln('Scalar24:   ', thur = thur:5, ' s/b true');
   writeln('Scalar25:   ', fri = fri:5, ' s/b true');
   writeln('Scalar26:   ', tue = wed:5, ' s/b false');
   writeln('Scalar27:   ', mon < wed:5, ' s/b true');
   writeln('Scalar28:   ', fri < fri:5, ' s/b false');
   writeln('Scalar29:  ', sun > sat:5, ' s/b true');
   writeln('Scalar30:  ', fri > sun:5, ' s/b false');
   writeln('Scalar31:  ', thur <> tue:5, ' s/b true');
   writeln('Scalar32:  ', wed <> wed:5, ' s/b false');
   writeln('Scalar33:  ', mon <= fri:5, ' s/b true');
   writeln('Scalar34:  ', fri <= fri:5, ' s/b true');
   writeln('Scalar35:  ', sat <= fri:5, ' s/b false');
   writeln('Scalar36:  ', fri >= tue:5, ' s/b true');
   writeln('Scalar37:  ', tue >= tue:5, ' s/b true');
   writeln('Scalar38:  ', tue >= sat:5, ' s/b false');

{******************************************************************************

                            Reals

******************************************************************************}

   writeln;
   writeln('******************* Reals ******************************');
   writeln;

   { formats, input (compiler) and output }
   writeln('Real1:   ', 1.554:15, ' s/b  1.554000e+00');
   writeln('Real2:   ', 0.00334:15, ' s/b  3.340000e-03');
   writeln('Real3:   ', 0.00334e-21:15, ' s/b  3.340000e-24');
   writeln('Real4:   ', 4e-45:15, ' s/b  4.000000e-45');
   writeln('Real5:   ', -5.565:15, ' s/b -5.565000e+00');
   writeln('Real6:   ', -0.00944:15, ' s/b -9.440000e-03');
   writeln('Real7:   ', -0.006364E32:15, ' s/b -6.364000e+29');
   writeln('Real8:   ', -2e-14:15, ' s/b -2.000000e-14');
   writeln('Real9:');
   writeln('         11111111112222222222333333333344444444445');
   writeln('12345678901234567890123456789012345678901234567890');
   for i := 1 to 20 do writeln(1.23456789012345678901234567890:i);
   writeln('s/b (note precision dropoff at right):');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.23e+000');
   writeln(' 1.234e+000');
   writeln(' 1.2345e+000');
   writeln(' 1.23456e+000');
   writeln(' 1.234567e+000');
   writeln(' 1.2345678e+000');
   writeln(' 1.23456789e+000');
   writeln(' 1.234567890e+000');
   writeln(' 1.2345678901e+000');
   writeln(' 1.23456789012e+000');
   writeln(' 1.234567890123e+000');
   writeln('Real10:');
   writeln('         11111111112222222222333333333344444444445');
   writeln('12345678901234567890123456789012345678901234567890');
   for i := 1 to 20 do writeln(i+0.23456789012345678901234567890:1:i);
   writeln('s/b (note precision dropoff at right):');
   writeln('1.2');
   writeln('2.23');
   writeln('3.234');
   writeln('4.2345');
   writeln('5.23456');
   writeln('6.234567');
   writeln('7.2345678');
   writeln('8.23456789');
   writeln('9.234567890');
   writeln('10.2345678901');
   writeln('11.23456789012');
   writeln('12.234567890123');
   writeln('13.2345678901234');
   writeln('14.23456789012345');
   writeln('15.234567890123456');
   writeln('16.2345678901234567');
   writeln('17.23456789012345678');
   writeln('18.234567890123456789');
   writeln('19.2345678901234567890');
   writeln('20.23456789012345678901');

   { unsigned variables }
   ra := 435.23;
   rb := 983.67;
   rc := rb;
   rd := 0.3443;
   writeln('Real11:  ', ra + rb:15, ' s/b  1.418900e+03');
   writeln('Rea112:  ', rb - ra:15, ' s/b  5.484399e+02');
   writeln('Real13:  ', ra * rb:15, ' s/b  4.281227e+05');
   writeln('Real14:  ', rb / ra:15, ' s/b  2.260115e+00');
   writeln('Real15:  ', rc = rb:5, ' s/b true');
   writeln('Real16:  ', ra = rb:5, ' s/b false');
   writeln('Real17:  ', ra < rb:5, ' s/b true');
   writeln('Real18:  ', rb < ra:5, ' s/b false');
   writeln('Real19:  ', rb > ra:5, ' s/b true');
   writeln('Real20:  ', ra > rb:5, ' s/b false');
   writeln('Real21:  ', ra <> rb:5, ' s/b true');
   writeln('Real22:  ', rb <> rc:5, ' s/b false');
   writeln('Real23:  ', ra <= rb:5, ' s/b true');
   writeln('Real24:  ', rc <= rb:5, ' s/b true');
   writeln('Real25:  ', rb <= ra:5, ' s/b false');
   writeln('Real26:  ', rb >= ra:5, ' s/b true');
   writeln('Real27:  ', rb >= rc:5, ' s/b true');
   writeln('Real28:  ', ra >= rb:5, ' s/b false');
   writeln('Real29:  ', abs(ra):15, ' s/b  4.35230e+02');
   writeln('Real30:  ', sqr(ra):15, ' s/b  1.89425e+05');
   writeln('Real31:  ', sqrt(rb):15, ' s/b  3.13635e+01');
   writeln('Real32:  ', sin(rb):15, ' s/b -3.44290e-01');
   writeln('Real33:  ', arctan(ra):15, ' s/b  1.56850e+00');
   writeln('Real34:  ', exp(rd):15, ' s/b  1.41100e+00');
   writeln('Real35:  ', ln(ra):15, ' s/b  6.07587e+00');
   writeln('Real36:  ', trunc(ra):1, ' s/b 435');
   writeln('Real37:  ', round(rb):1, ' s/b 984');
   writeln('Real38:  ', round(ra):1, ' s/b 435');

   { unsigned constants }
   writeln('Real39:  ', 344.939 + 933.113:15, ' s/b  1.278052e+03');
   writeln('Real40:  ', 883.885 - 644.939:15, ' s/b  2.389460e+02');
   writeln('Real41:  ', 754.74 * 138.75:15, ' s/b  1.047202e+05');
   writeln('Real42:  ', 634.3 / 87373.99:15, ' s/b  7.259598e-03');
   writeln('Real43:  ', 77.44 = 77.44:5, ' s/b true');
   writeln('Real44:  ', 733.9 = 959.2:5, ' s/b false');
   writeln('Real45:  ', 883.22 < 8383.33:5, ' s/b true');
   writeln('Real46:  ', 475.322 < 234.93:5, ' s/b false');
   writeln('Real47:  ', 7374.3 > 6442.34:5, ' s/b true');
   writeln('Real48:  ', 985.562 > 1001.95:5, ' s/b false');
   writeln('Real49:  ', 030.11 <> 0938.44:5, ' s/b true');
   writeln('Real50:  ', 1.233 <> 1.233:5, ' s/b false');
   writeln('Real51:  ', 8484.002 <= 9344.003:5, ' s/b true');
   writeln('Real52:  ', 9.11 <= 9.11:5, ' s/b true');
   writeln('Real53:  ', 93.323 <= 90.323:5, ' s/b false');
   writeln('Real54:  ', 6543.44 >= 5883.33:5, ' s/b true');
   writeln('Real55:  ', 3247.03 >= 3247.03:5, ' s/b true');
   writeln('Real56:  ', 28343.22 >= 30044.45:5, ' s/b false');
   writeln('Real57:  ', abs(34.93):15, ' s/b  3.493000e+01');
   writeln('Real58:  ', sqr(2.34):15, ' s/b  5.475600e+00');
   writeln('Real59:  ', sqrt(9454.32):15, ' s/b  9.723333e+01');
   writeln('Real60:  ', sin(34.22):15, ' s/b  3.311461e-01');
   writeln('Real61:  ', arctan(343.2):15, ' s/b  1.567883e+00');
   writeln('Real62:  ', exp(0.332):15, ' s/b  1.393753e+00');
   writeln('Real63:  ', ln(83.22):15, ' s/b  4.421488e+00');
   writeln('Real64:  ', trunc(24.344):1, ' s/b 24');
   writeln('Real65:  ', round(74.56):1, ' s/b 75');
   writeln('Real66:  ', round(83.24):1, ' s/b 83');
   writeln('Real67:  ', rcnst:15, ' s/b  4.333000e+01');

   { signed variables }
   ra := -734.2;
   rb := -7634.52;
   rc := ra;
   rd := 1034.54;
   re := -0.38483;
   writeln('Real68:  ', ra + rd:15, ' s/b  3.003400e+02');
   writeln('Real69:  ', rd + ra:15, ' s/b  3.003400e+02');
   writeln('Real70:  ', rb + rd:15, ' s/b -6.599980e+03');
   writeln('Real71:  ', ra + rb:15, ' s/b -8.368720e+03');
   writeln('Real72:  ', rd - ra:15, ' s/b  1.768740e+03');
   writeln('Real73:  ', rb - rd:15, ' s/b -8.669061e+03');
   writeln('Real74:  ', rb - ra:15, ' s/b -6.900320e+03');
   writeln('Real75:  ', rd * ra:15, ' s/b -7.595593e+05');
   writeln('Real76:  ', ra * rd:15, ' s/b -7.595593e+05');
   writeln('Real77:  ', ra * rb:15, ' s/b  5.605265e+06');
   writeln('Real78:  ', rd / ra:15, ' s/b -1.409071e+00');
   writeln('Real79:  ', rb / rd:15, ' s/b -7.379627e+00');
   writeln('Real80:  ', rb / ra:15, ' s/b  1.039842e+01');
   writeln('Real81:  ', ra = rc:5, ' s/b true');
   writeln('Real82:  ', ra = rb:5, ' s/b false');
   writeln('Real83:  ', ra <> rb:5, ' s/b true');
   writeln('Real84:  ', ra <> rc:5, ' s/b false');
   writeln('Real85:  ', ra < rd:5, ' s/b true');
   writeln('Real86:  ', rb < ra:5, ' s/b true');
   writeln('Real87:  ', rd < ra:5, ' s/b false');
   writeln('Real88:  ', ra < rb:5, ' s/b false');
   writeln('Real89:  ', rd > ra:5, ' s/b true');
   writeln('Real90:  ', ra > rb:5, ' s/b true');
   writeln('Real91:  ', ra > rd:5, ' s/b false');
   writeln('Real92:  ', rb > ra:5, ' s/b false');
   writeln('Real93:  ', ra <= rd:5, ' s/b true');
   writeln('Real94:  ', rb <= ra:5, ' s/b true');
   writeln('Real95:  ', ra <= rc:5, ' s/b true');
   writeln('Real96:  ', rd <= ra:5, ' s/b false');
   writeln('Real97:  ', ra <= rb:5, ' s/b false');
   writeln('Real98:  ', rd >= ra:5, ' s/b true');
   writeln('Real99:  ', ra >= rb:5, ' s/b true');
   writeln('Real100: ', ra >= rc:5, ' s/b true');
   writeln('Real101: ', ra >= rd:5, ' s/b false');
   writeln('Real102: ', rb >= ra:5, ' s/b false');
   writeln('Real103: ', abs(ra):15, ' s/b  7.34200e+02');
   writeln('Real104: ', sqr(ra):15, ' s/b  5.39050e+05');
   writeln('Real105: ', sin(rb):15, ' s/b -4.34850e-01');
   writeln('Real106: ', arctan(ra):15, ' s/b -1.56943e+00');
   writeln('Real107: ', exp(re):15, ' s/b  6.80566e-01');
   writeln('Real108: ', trunc(ra):15, ' s/b -734');
   writeln('Real109: ', round(rb):15, ' s/b -7635');
   writeln('Real110: ', round(ra):15, ' s/b -734');

   { signed constants }
   writeln('Real111: ', 45.934 + (-30.834):15, ' s/b  1.510000e+01');
   writeln('Real112: ', -25.737 + 70.87:15, ' s/b  4.513300e+01');
   writeln('Real113: ', -62.63 + 23.99:15, ' s/b -3.864000e+01');
   writeln('Real114: ', -20.733 + (-15.848):15, ' s/b -3.658100e+01');
   writeln('Real115: ', 20.774 - (-14.774):15, ' s/b  3.554800e+01');
   writeln('Real116: ', -34.523 - 14.8754:15, ' s/b -4.939840e+01');
   writeln('Real117: ', -56.664 - (-12.663):15, ' s/b -4.400100e+01');
   writeln('Real118: ', 5.663 * (-4.664):15, ' s/b -2.641223e+01');
   writeln('Real119: ', (-18.62) * 7.997:15, ' s/b -1.489041e+02');
   writeln('Real120: ', (-40.552) * (-13.774):15, ' s/b  5.585632e+02');
   writeln('Real121: ', 30.6632 / (-5.874):15, ' s/b -5.220157e+00');
   writeln('Real122: ', (-50.636) / 2.8573:15, ' s/b -1.772163e+01');
   writeln('Real123: ', (-20.7631) / (-4.85734):15, ' s/b  4.274582e+00');
   writeln('Real124: ', -5.775 = -5.775:5, ' s/b true');
   writeln('Real125: ', -5.6364 = 5.8575:5, ' s/b false');
   writeln('Real126: ', -21.6385 <> -40.764:5, ' s/b true');
   writeln('Real127: ', -21.772 <> -21.772:5, ' s/b false');
   writeln('Real128: ', -3.512 < 5.8467:5, ' s/b true');
   writeln('Real129: ', -32.644 < -20.9074:5, ' s/b true');
   writeln('Real130: ', 20.763 < -20.743:5, ' s/b false');
   writeln('Real131: ', -15.663 < -40.784:5, ' s/b false');
   writeln('Real132: ', 70.766 > -4.974:5, ' s/b true');
   writeln('Real133: ', -23.6532 > -34.774:5, ' s/b true');
   writeln('Real134: ', -5.773 > 5.9874:5, ' s/b false');
   writeln('Real135: ', -60.663 > -59.78:5, ' s/b false');
   writeln('Real136: ', -12.542 <= 4.0848:5, ' s/b true');
   writeln('Real137: ', -14.8763 <= -5.0847:5, ' s/b true');
   writeln('Real138: ', -7.8373 <= -7.8373:5, ' s/b true');
   writeln('Real139: ', 5.4564 <= -5.4564:5, ' s/b false');
   writeln('Real140: ', -10.72633 <= -20.984:5, ' s/b false');
   writeln('Real141: ', 9.834 >= -3.9383:5, ' s/b true');
   writeln('Real142: ', -4.562 >= -10.74:5, ' s/b true');
   writeln('Real143: ', -13.63 >= -13.63:5, ' s/b true');
   writeln('Real144: ', -6.74 >= 6.74:5, ' s/b false');
   writeln('Real145: ', -20.7623 >= -10.574:5, ' s/b false');
   writeln('Real146: ', abs(-6.823):15, ' s/b  6.823000e+00');
   writeln('Real147  ', sqr(-348.22):15, ' s/b  1.212572e+05');
   writeln('Real148: ', sin(-733.22):15, ' s/b  9.421146e-01');
   writeln('Real149: ', arctan(-8387.22):15, ' s/b -1.570677e+00');
   writeln('Real150: ', exp(-0.8743):15, ' s/b  4.171539e-01');
   writeln('Real151: ', trunc(-33.422):1, ' s/b -33');
   writeln('Real152: ', round(-843.22):1, ' s/b -843');
   writeln('Real153: ', round(-6243.76):1, ' s/b -6244');
   writeln('Real154: ', rscst:15, ' s/b -8.422000e+01');
   writeln('Real155: ', -rscst:15, ' s/b  8.422000e+01');
   writeln('Real156:  ', rscst2:15, ' s/b -4.333000e+01');
   writeln('Real157: ', rscst3:15, ' s/b  8.422000e+01');

{******************************************************************************

                            Sets

******************************************************************************}

   writeln;
   writeln('******************* sets ******************************');
   writeln;

   { sets of integers }
   write('Set1:  ');
   sta := [];
   for i := 1 to 10 do if odd(i) then sta := sta+[i, i+10];
   for i := 1 to 20 do if i in sta then write('1') else write('0');
   write(' s/b ');
   writeln('10101010101010101010');
   write('Set2:  ');
   sta := [1, 4, 5];
   stb := [2, 6, 10];
   for i := 1 to 10 do if i in sta+stb then write('1') else write('0');
   write(' s/b ');
   writeln('1101110001');
   write('Set3:  ');
   sta := [1, 2, 6, 5, 7];
   stb := [2, 6, 10];
   for i := 1 to 10 do if i in sta*stb then write('1') else write('0');
   write(' s/b ');
   writeln('0100010000');
   write('Set4:  ');
   sta := [2, 4, 7, 8];
   stb := [1, 3, 4, 8, 10];
   for i := 1 to 10 do if i in sta-stb then write('1') else write('0');
   write(' s/b ');
   writeln('0100001000');
   sta := [4, 6, 8, 9];
   stb := [1, 4, 5, 9];
   stc := [4, 6, 8, 9];
   writeln('Set5:  ', sta = stb:5, ' s/b false');
   writeln('Set6:  ', sta = stc:5, ' s/b true');
   writeln('Set7:  ', sta <> stb:5, ' s/b true');
   writeln('Set8:  ', sta <> stc:5, ' s/b false');
   sta := [1, 2, 5, 7, 10];
   stb := [1, 5, 10];
   stc := [1, 5, 10, 6];
   std := [1, 2, 5, 7, 10];
   writeln('Set9:  ', stb <= sta:5, ' s/b true');
   writeln('Set10: ', stb <= std:5, ' s/b true');
   writeln('Set11: ', stc <= sta:5, ' s/b false');
   writeln('Set12: ', sta >= stb:5, ' s/b true');
   writeln('Set13: ', std >= stb:5, ' s/b true');
   writeln('Set14: ', sta >= stc:5, ' s/b false');
   write('Set15: ');
   i := 2;
   x := 4;
   sta := [i, x, i+x];
   for i := 1 to 10 do if i in sta then write('1') else write('0');
   write(' s/b ');
   writeln('0101010000');
   { these are just compile time tests }
   ste := std;
   stf := [1, 2, 5, 7];
   stg := stf;
   i := 10;
   writeln('Set16: ', 5 in [cone..i], ' s/b true');

   { sets of characters }
   write('Set17: ');
   csta := [];
   for ci := 'a' to 'j' do
      if odd(ord(ci)) then csta := csta+[ci, chr(ord(ci)+10)];
   for ci := 'a' to 't' do if ci in csta then write(ci) else write('_');
   write(' s/b ');
   writeln('a_c_e_g_i_k_m_o_q_s_');
   write('Set18: ');
   csta := ['a', 'c', 'f'];
   cstb := ['c', 'd', 'g'];
   for ci := 'a' to 'j' do if ci in csta+cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('a_cd_fg___');
   write('Set19: ');
   csta := ['d', 'f', 'h', 'a'];
   cstb := ['a', 'b', 'i', 'h'];
   for ci := 'a' to 'j' do if ci in csta*cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('a______h__');
   write('Set20: ');
   csta := ['b', 'd', 'i', 'j'];
   cstb := ['i', 'h', 'd', 'e'];
   for ci := 'a' to 'j' do if ci in csta-cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('_b_______j');
   csta := ['b', 'd', 'h', 'j'];
   cstb := ['a', 'd', 'h', 'c'];
   cstc := ['b', 'd', 'h', 'j'];
   writeln('Set21: ', csta = cstb:5, ' s/b false');
   writeln('Set22: ', csta = cstc:5, ' s/b true');
   writeln('Set23: ', csta <> cstb:5, ' s/b true');
   writeln('Set24: ', csta <> cstc:5, ' s/b false');
   csta := ['a', 'b', 'f', 'g', 'j'];
   cstb := ['a', 'f', 'g'];
   cstc := ['a', 'f', 'g', 'h'];
   cstd := ['a', 'b', 'f', 'g', 'j'];
   writeln('Set25: ', cstb <= csta:5, ' s/b true');
   writeln('Set26: ', cstb <= cstd:5, ' s/b true');
   writeln('Set27: ', cstc <= csta:5, ' s/b false');
   writeln('Set28: ', csta >= cstb:5, ' s/b true');
   writeln('Set29: ', cstd >= cstb:5, ' s/b true');
   writeln('Set30: ', csta >= cstc:5, ' s/b false');
   write('Set31: ');
   ci := 'a';
   i := 4;
   csta := [ci, chr(ord(ci)+i)];
   for ci := 'a' to 'j' do if ci in csta then write(ci) else write('_');
   write(' s/b ');
   writeln('a___e_____');
   { these are just compile time tests }
   cste := cstd;
   cstf := ['a', 'b', 'e', 'f'];
   cstg := cstf;

   { sets of enumerated }
   write('Set32: ');
   sena := [];
   for ei := one to ten do if odd(ord(ei)) then sena := sena+[ei];
   for ei := one to ten do if ei in sena then write('1') else write('0');
   write(' s/b ');
   writeln('0101010101');
   write('Set33: ');
   sena := [one, four, five];
   senb := [two, six, ten];
   for ei := one to ten do if ei in sena+senb then write('1') else write('0');
   write(' s/b ');
   writeln('1101110001');
   write('Set34: ');
   sena := [one, two, six, five, seven];
   senb := [two, six, ten];
   for ei := one to ten do if ei in sena*senb then write('1') else write('0');
   write(' s/b ');
   writeln('0100010000');
   write('Set35: ');
   sena := [two, four, seven, eight];
   senb := [one, three, four, eight, ten];
   for ei := one to ten do if ei in sena-senb then write('1') else write('0');
   write(' s/b ');
   writeln('0100001000');
   sena := [four, six, eight, nine];
   senb := [one, four, five, nine];
   senc := [four, six, eight, nine];
   writeln('Set36: ', sena = senb:5, ' s/b false');
   writeln('Set37: ', sena = senc:5, ' s/b true');
   writeln('Set38: ', sena <> senb:5, ' s/b true');
   writeln('Set39: ', sena <> senc:5, ' s/b false');
   sena := [one, two, five, seven, ten];
   senb := [one, five, ten];
   senc := [one, five, ten, six];
   send := [one, two, five, seven, ten];
   writeln('Set40: ', senb <= sena:5, ' s/b true');
   writeln('Set41: ', senb <= send:5, ' s/b true');
   writeln('Set42: ', senc <= sena:5, ' s/b false');
   writeln('Set43: ', sena >= senb:5, ' s/b true');
   writeln('Set44: ', send >= senb:5, ' s/b true');
   writeln('Set45: ', sena >= senc:5, ' s/b false');
   write('Set46: ');
   ei := two;
   sena := [ei, succ(ei)];
   for ei := one to ten do if ei in sena then write('1') else write('0');
   write(' s/b ');
   writeln('0110000000');
   { these are just compile time tests }
   send := [one, two, five];
   sene := send;
   senf := [one, two, five, seven];
   seng := senf;

   { sets of boolean }
   write('Set47: ');
   sba := [];
   for ba := false to true do if odd(ord(ba)) then sba := sba+[ba];
   for ba := false to true do if ba in sba then write('1') else write('0');
   write(' s/b ');
   writeln('01');
   write('Set48: ');
   sba := [false];
   sbb := [true];
   for ba := false to true do if ba in sba+sbb then write('1') else write('0');
   write(' s/b ');
   writeln('11');
   write('Set49: ');
   sba := [false, true];
   sbb := [false];
   for ba := false to true do if ba in sba*sbb then write('1') else write('0');
   write(' s/b ');
   writeln('10');
   write('Set50: ');
   sba := [true, false];
   sbb := [true];
   for ba := false to true do if ba in sba-sbb then write('1') else write('0');
   write(' s/b ');
   writeln('10');
   sba := [true];
   sbb := [false];
   sbc := [true];
   writeln('Set51: ', sba = sbb:5, ' s/b false');
   writeln('Set52: ', sba = sbc:5, ' s/b true');
   writeln('Set53: ', sba <> sbb:5, ' s/b true');
   writeln('Set54: ', sba <> sbc:5, ' s/b false');
   sba := [true, false];
   sbb := [false];
   sbc := [true];
   sbd := [false];
   writeln('Set55: ', sbb <= sba:5, ' s/b true');
   writeln('Set56: ', sbb <= sbd:5, ' s/b true');
   writeln('Set57: ', sbc <= sbb:5, ' s/b false');
   writeln('Set58: ', sba >= sbb:5, ' s/b true');
   writeln('Set59: ', sbd >= sbb:5, ' s/b true');
   writeln('Set60: ', sbb >= sbc:5, ' s/b false');
   write('Set61: ');
   ba := false;
   sba := [ba, succ(ba)];
   for ba := false to true do if ba in sba then write('1') else write('0');
   write(' s/b ');
   writeln('11');
   { these are just compile time tests }
   sbe := sbd;
   sbf := [true];
   sbg := sbf;
   write('set62: ');
   new(pi1);
   new(pi2);
   pi1^ := 3;
   pi2^ := 5;
   write([pi1^..pi2^] = [3..5]:5);
   writeln(' s/b true');
   write('set63: ');
   srx := 1;
   sry := 10;
   for i := 1 to 10 do if i in [srx,sry] then write('1') else write('0');
   writeln(' s/b 1000000001');

{******************************************************************************

                            Pointers

******************************************************************************}

   writeln;
   writeln('******************* Pointers ******************************');
   writeln;

   { pointers to types }
   write('Pointer1:   ');
   new(pti);
   pti^ := 4594;
   writeln(pti^:1, ' s/b 4594');
   write('Pointer2:   ');
   new(ptb);
   ptb^ := true;
   writeln(ptb^:5, ' s/b  true');
   write('Pointer3:   ');
   new(ptb);
   ptb^ := false;
   writeln(ptb^:5, ' s/b false');
   write('Pointer4:   ');
   new(ptc);
   ptc^ := 'p';
   writeln(ptc^, ' s/b p');
   write('Pointer5:   ');
   new(pte);
   pte^ := six;
   writeln(ord(pte^):1, ' s/b 5');
   write('Pointer6:   ');
   new(ptes);
   ptes^ := four;
   writeln(ord(ptes^):1, ' s/b 3');
   write('Pointer7:   ');
   new(pts);
   pts^ := 17;
   writeln(pts^:1, ' s/b 17');
   write('Pointer8:   ');
   new(ptr);
   ptr^ := 1234.5678;
   writeln(ptr^:1:4, ' s/b 1234.5678');
   write('Pointer9:   ');
   new(ptst);
   ptst^ := 'my word is';
   writeln(ptst^, ' s/b my word is');
   write('Pointer10:  ');
   new(pta);
   for i := 1 to 10 do pta^[i] := i+10;
   for i := 10 downto 1 do write(pta^[i]:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Pointer11:   ');
   new(ptrc);
   ptrc^.a := 7234;
   ptrc^.b := 'y';
   writeln(ptrc^.a:1, ' ', ptrc^.b, ' s/b 7234 y');
   write('Pointer12:   ');
   new(ptstc);
   ptstc^ := ['b', 'd', 'i'..'j'];
   for ci := 'a' to 'j' do if ci in ptstc^ then write(ci) else write('_');
   writeln(' s/b _b_d____ij');
   write('Pointer13:  ');
   new(ptp);
   new(ptp^);
   ptp^^ := 3732;
   writeln(ptp^^:1, ' s/b 3732');

   { equality/inequality, nil }
   write('Pointer14:  ');
   pti := nil;
   writeln(pti = nil:5, ' s/b  true');
   write('Pointer15:  ');
   new(pti);
   writeln(pti = nil:5, ' s/b false');
   write('Pointer16:  ');
   pti1 := pti;
   writeln(pti = pti1:5, ' s/b true');
   write('Pointer17:  ');
   pti1 := pti;
   writeln(pti <> pti1:5, ' s/b false');
   write('Pointer18:  ');
   new(pti1);
   writeln(pti = pti1:5, ' s/b false');
   write('Pointer19:  ');
   writeln(pti <> pti1:5, ' s/b  true');
   
   { test dispose takes expression (this one does not print) }
   new(pti2);
   dispose(frp);

   { dynamic allocation stress tests }

   { allocate top to bottom, then free from top to bottom }
   write('Pointer20:  ');
   new(ipa);
   new(ipb);
   new(ipc);
   dispose(ipa);
   dispose(ipb);
   dispose(ipc);
   writeln('done s/b done');

   { allocate top to bottom, then free from bottom to top }

   write('Pointer21:  ');
   new(ipa);
   new(ipb);
   new(ipc);
   dispose(ipc);
   dispose(ipb);
   dispose(ipa);

   { free 2 middle blocks to test coalesce }

   write('Pointer22:  ');
   new(ipa);
   new(ipb);
   new(ipc);
   new(ipd);
   dispose(ipb);
   dispose(ipc);
   dispose(ipa);
   dispose(ipd);
   writeln('done s/b done');

   { free 3 middle blocks to test coalesce }
   write('Pointer23:  ');
   new(ipa);
   new(ipb);
   new(ipc);
   new(ipd);
   new(ipe);
   dispose(ipb);
   dispose(ipd);
   dispose(ipc);
   dispose(ipa);
   dispose(ipe);
   writeln('done s/b done');

   if doptrtortst then begin
   
      { linear torture test }
      writeln('Pointer24:  ');
      for cnt := 1 to 100 do begin

         write(cnt:3, ' '); if (cnt mod 10) = 0 then writeln;
         for i := 1 to 100 do iap[i] := nil;
         for i := 1 to 100 do begin new(iap[i]); iap[i]^ := i end;
         for i := 1 to 100 do if iap[i] = nil then
            writeln('*** bad allocation of block');
         for i := 100 downto 1 do if iap[i]^ <> i then
            writeln('*** bad block content');
         for i := 1 to 100 do begin

            dispose(iap[i]);
            iap[i] := nil;
            for x := 1 to 100 do if iap[x] <> nil then
               if iap[x]^ <> x then
                  writeln('*** bad block content')

         end;

         for i := 1 to 100 do iap[i] := nil;
         for i := 1 to 100 do begin new(iap[i]); iap[i]^ := i end;
         for i := 1 to 100 do if iap[i] = nil then
            writeln('*** bad allocation of block');
         for i := 100 downto 1 do if iap[i]^ <> i then
            writeln('*** bad block content');
         for i := 100 downto 1 do begin

            dispose(iap[i]);
            iap[i] := nil;
            for x := 1 to 100 do if iap[x] <> nil then
               if iap[x]^ <> x then
                  writeln('*** bad block content')

         end

      end;
      writeln;
      writeln('s/b');
      writeln;
      writeln('  1   2   3   4   5   6   7   8   9  10');
      writeln(' 11  12  13  14  15  16  17  18  19  20');
      writeln(' 21  22  23  24  25  26  27  28  29  30');
      writeln(' 31  32  33  34  35  36  37  38  39  40');
      writeln(' 41  42  43  44  45  46  47  48  49  50');
      writeln(' 51  52  53  54  55  56  57  58  59  60');
      writeln(' 61  62  63  64  65  66  67  68  69  70');
      writeln(' 71  72  73  74  75  76  77  78  79  80');
      writeln(' 81  82  83  84  85  86  87  88  89  90');
      writeln(' 91  92  93  94  95  96  97  98  99  100');
   
   end else begin
   
      { keep listing equal for compare }
      writeln('Pointer24:  ');
      writeln('  1   2   3   4   5   6   7   8   9  10 ');
      writeln(' 11  12  13  14  15  16  17  18  19  20 ');
      writeln(' 21  22  23  24  25  26  27  28  29  30 ');
      writeln(' 31  32  33  34  35  36  37  38  39  40 ');
      writeln(' 41  42  43  44  45  46  47  48  49  50 ');
      writeln(' 51  52  53  54  55  56  57  58  59  60 ');
      writeln(' 61  62  63  64  65  66  67  68  69  70 ');
      writeln(' 71  72  73  74  75  76  77  78  79  80 ');
      writeln(' 81  82  83  84  85  86  87  88  89  90 ');
      writeln(' 91  92  93  94  95  96  97  98  99 100 ');
      writeln;
      writeln('s/b');
      writeln;
      writeln('  1   2   3   4   5   6   7   8   9  10');
      writeln(' 11  12  13  14  15  16  17  18  19  20');
      writeln(' 21  22  23  24  25  26  27  28  29  30');
      writeln(' 31  32  33  34  35  36  37  38  39  40');
      writeln(' 41  42  43  44  45  46  47  48  49  50');
      writeln(' 51  52  53  54  55  56  57  58  59  60');
      writeln(' 61  62  63  64  65  66  67  68  69  70');
      writeln(' 71  72  73  74  75  76  77  78  79  80');
      writeln(' 81  82  83  84  85  86  87  88  89  90');
      writeln(' 91  92  93  94  95  96  97  98  99  100');
   
   end;

   if doptrtortst then begin
   
      rndseq := 1;

      { random block torture test }
      writeln('Pointer25:  ');
      for i := 1 to 100 do iap[i] := nil;
      for cnt2 := 1 to 100 do begin

         write(cnt2:3, ' '); if (cnt2 mod 10) = 0 then writeln;
         for cnt := 1 to 100 do begin

            { allocate random }
            rn := random(1, 100); { choose random pointer }
            new(iap[rn]); { allocate }
            iap[rn]^ := rn; { set number }
            for i := 1 to 100 do if iap[i] <> nil then
               if iap[i]^ <> i then
                  writeln('*** bad block content');

            { deallocate random }
            rn := random(1, 100); { choose random pointer }
            if iap[rn] <> nil then dispose(iap[rn]); { deallocate }
            iap[rn] := nil;
            for i := 1 to 100 do if iap[i] <> nil then
               if iap[i]^ <> i then
                  writeln('*** bad block content');

         end

      end;
      writeln;
      writeln('s/b');
      writeln;
      writeln('  1   2   3   4   5   6   7   8   9  10');
      writeln(' 11  12  13  14  15  16  17  18  19  20');
      writeln(' 21  22  23  24  25  26  27  28  29  30');
      writeln(' 31  32  33  34  35  36  37  38  39  40');
      writeln(' 41  42  43  44  45  46  47  48  49  50');
      writeln(' 51  52  53  54  55  56  57  58  59  60');
      writeln(' 61  62  63  64  65  66  67  68  69  70');
      writeln(' 71  72  73  74  75  76  77  78  79  80');
      writeln(' 81  82  83  84  85  86  87  88  89  90');
      writeln(' 91  92  93  94  95  96  97  98  99  100');
      
   end else begin
   
      { keep listing equal for comparision }
      writeln('Pointer25:  ');
      writeln('  1   2   3   4   5   6   7   8   9  10 ');
      writeln(' 11  12  13  14  15  16  17  18  19  20 ');
      writeln(' 21  22  23  24  25  26  27  28  29  30 ');
      writeln(' 31  32  33  34  35  36  37  38  39  40 ');
      writeln(' 41  42  43  44  45  46  47  48  49  50 ');
      writeln(' 51  52  53  54  55  56  57  58  59  60 ');
      writeln(' 61  62  63  64  65  66  67  68  69  70 ');
      writeln(' 71  72  73  74  75  76  77  78  79  80 ');
      writeln(' 81  82  83  84  85  86  87  88  89  90 ');
      writeln(' 91  92  93  94  95  96  97  98  99 100 ');      
      writeln;
      writeln('s/b');
      writeln;
      writeln('  1   2   3   4   5   6   7   8   9  10');
      writeln(' 11  12  13  14  15  16  17  18  19  20');
      writeln(' 21  22  23  24  25  26  27  28  29  30');
      writeln(' 31  32  33  34  35  36  37  38  39  40');
      writeln(' 41  42  43  44  45  46  47  48  49  50');
      writeln(' 51  52  53  54  55  56  57  58  59  60');
      writeln(' 61  62  63  64  65  66  67  68  69  70');
      writeln(' 71  72  73  74  75  76  77  78  79  80');
      writeln(' 81  82  83  84  85  86  87  88  89  90');
      writeln(' 91  92  93  94  95  96  97  98  99  100');      
   
   end;

{******************************************************************************

                            Arrays

******************************************************************************}

   writeln;
   writeln('******************* arrays ******************************');
   writeln;

   { single demension, integer index }
   write('Array1:   ');
   for i := 1 to 10 do avi[i] := i+10;
   for i := 10 downto 1 do write(avi[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array2:   ');
   for i := 1 to 10 do pavi[i] := i+10;
   for i := 10 downto 1 do write(pavi[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array3:   ');
   for i := 1 to 10 do avis[i] := i+10;
   for i := 10 downto 1 do write(avis[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array4:   ');
   for i := 1 to 10 do pavis[i] := i+10;
   for i := 10 downto 1 do write(pavis[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array5:   ');
   for i := 1 to 10 do avb[i] := odd(i);
   for i := 10 downto 1 do write(avb[i]:5, ' ');
   writeln;
   writeln('    s/b:   false  true false  true false  true false  true false',
           '  true');
   write('Array6:   ');
   for i := 1 to 10 do pavb[i] := odd(i);
   for i := 10 downto 1 do write(pavb[i]:5, ' ');
   writeln;
   writeln('    s/b:   false  true false  true false  true false  true false',
           '  true');
   write('Array7:   ');
   for i := 1 to 10 do avr[i] := i+10+0.12;
   for i := 10 downto 1 do write(avr[i]:1:2, ' ');
   writeln;
   writeln('    s/b:   20.12 19.12 18.12 17.12 16.12 15.12 14.12 ',
           '13.12 12.12 11.12');
   write('Array8:   ');
   for i := 1 to 10 do pavr[i] := i+10+0.12;
   for i := 10 downto 1 do write(pavr[i]:1:2, ' ');
   writeln;
   writeln('    s/b:   20.12 19.12 18.12 17.12 16.12 15.12 14.12 ',
           '13.12 12.12 11.12');
   write('Array9:   ');
   for i := 1 to 10 do avc[i] := chr(i+ord('a'));
   for i := 10 downto 1 do write(avc[i]:1, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array10:  ');
   for i := 1 to 10 do pavc[i] := chr(i+ord('a'));
   for i := 10 downto 1 do write(pavc[i]:1, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array11:  ');
   for i := 1 to 10 do avcs[i] := chr(i+ord('f'));
   for i := 10 downto 1 do write(avcs[i]:1, ' ');
   writeln('s/b p o n m l k j i h g');
   write('Array12:  ');
   for i := 1 to 10 do pavcs[i] := chr(i+ord('f'));
   for i := 10 downto 1 do write(pavcs[i]:1, ' ');
   writeln('s/b p o n m l k j i h g');
   write('Array13:  ');
   for ei := one to ten do ave[ord(ei)+1] := ei;
   for ei := ten downto one do write(ord(ave[ord(ei)+1]):1, ' ');
   writeln('s/b 9 8 7 6 5 4 3 2 1 0');
   write('Array14:  ');
   for ei := one to ten do pave[ord(ei)+1] := ei;
   for ei := ten downto one do write(ord(ave[ord(ei)+1]):1, ' ');
   writeln('s/b 9 8 7 6 5 4 3 2 1 0');
   write('Array15:  ');
   for ei := three to six do aves[ord(ei)+1] := ei;
   for ei := six downto three do write(ord(aves[ord(ei)+1]):1, ' ');
   writeln('s/b 5 4 3 2');
   write('Array16:  ');
   for ei := three to six do paves[ord(ei)+1] := ei;
   for ei := six downto three do write(ord(paves[ord(ei)+1]):1, ' ');
   writeln('s/b 5 4 3 2');
   write('Array17:  ');
   for i := 1 to 10 do avs[i] := [chr(i+ord('a'))];
   for i := 10 downto 1 do
      for ci := 'a' to 'z' do if ci in avs[i] then write(ci, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array18:  ');
   for i := 1 to 10 do pavs[i] := [chr(i+ord('a'))];
   for i := 10 downto 1 do
      for ci := 'a' to 'z' do if ci in pavs[i] then write(ci, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array19:  ');
   for i := 1 to 10 do
      begin avrc[i].a := i+10; avrc[i].b := chr(i+ord('a')) end;
   for i := 10 downto 1 do write(avrc[i].a:1, ' ', avrc[i].b, ' ');
   writeln;
   writeln('     s/b:  20 k 19 j 18 i 17 h 16 g 15 f 14 e 13 d 12 c 11 b');
   write('Array20:  ');
   for i := 1 to 10 do
      begin pavrc[i].a := i+10; pavrc[i].b := chr(i+ord('a')) end;
   for i := 10 downto 1 do write(pavrc[i].a:1, ' ', pavrc[i].b, ' ');
   writeln;
   writeln('     s/b:  20 k 19 j 18 i 17 h 16 g 15 f 14 e 13 d 12 c 11 b');
   write('Array21:  ');
   for i := 1 to 10 do begin rewrite(avf[i]); writeln(avf[i], i+10) end;
   for i := 10 downto 1 do
      begin reset(avf[i]); readln(avf[i], x); write(x:1, ' ') end;
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array22:  ');
   for i := 1 to 10 do begin rewrite(pavf[i]); writeln(pavf[i], i+10) end;
   for i := 10 downto 1 do
      begin reset(pavf[i]); readln(pavf[i], x); write(x:1, ' ') end;
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array23:  ');
   for i := 1 to 10 do begin new(avp[i]); avp[i]^ := i+10 end;
   for i := 10 downto 1 do write(avp[i]^:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array24:  ');
   for i := 1 to 10 do begin new(pavp[i]); pavp[i]^ := i+10 end;
   for i := 10 downto 1 do write(pavp[i]^:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');

   { indexing tests }
   write('Array25:  ');
   for ba := false to true do bia[ba] := ord(ba)+10;
   for ba := true downto false do write(bia[ba]:1, ' ');
   writeln(' s/b 11 10');
   write('Array26:  ');
   for ba := false to true do pbia[ba] := ord(ba)+10;
   for ba := true downto false do write(pbia[ba]:1, ' ');
   writeln(' s/b 11 10');
   write('Array27:  ');
   for ci := 'a' to 'j' do cia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(cia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array28:  ');
   for ci := 'a' to 'j' do pcia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(pcia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array29:  ');
   for ci := 'a' to 'j' do csia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(csia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array30:  ');
   for ci := 'a' to 'j' do pcsia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(pcsia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array31:  ');
   for ei := one to ten do eia[ei] := ord(ei);
   for ei := ten downto one do write(eia[ei]:1, ' ');
   writeln(' s/b  9 8 7 6 5 4 3 2 1 0');
   write('Array32:  ');
   for ei := one to ten do peia[ei] := ord(ei);
   for ei := ten downto one do write(peia[ei]:1, ' ');
   writeln(' s/b  9 8 7 6 5 4 3 2 1 0');
   write('Array33:  ');
   for ei := two to six do eia[ei] := ord(ei);
   for ei := six downto two do write(eia[ei]:1, ' ');
   writeln(' s/b  5 4 3 2 1');
   write('Array34:  ');
   for ei := two to six do peia[ei] := ord(ei);
   for ei := six downto two do write(peia[ei]:1, ' ');
   writeln(' s/b  5 4 3 2 1');

   { multidementional arrays }
   writeln('Array35:');
   z := 0;
   for x := 1 to 10 do
      for y := 1 to 10 do begin da[y, x] := z; z := z + 1 end;
   for x := 1 to 10 do
   begin
      for y := 1 to 10 do write(da[x][y]:2, ' ');
      writeln;
   end;
   writeln('s/b');
   writeln('0 10 20 30 40 50 60 70 80 90');
   writeln('1 11 21 31 41 51 61 71 81 91');
   writeln('2 12 22 32 42 52 62 72 82 92');
   writeln('3 13 23 33 43 53 63 73 83 93');
   writeln('4 14 24 34 44 54 64 74 84 94');
   writeln('5 15 25 35 45 55 65 75 85 95');
   writeln('6 16 26 36 46 56 66 76 86 96');
   writeln('7 17 27 37 47 57 67 77 87 97');
   writeln('8 18 28 38 48 58 68 78 88 98');
   writeln('9 19 29 39 49 59 69 79 89 99');
   writeln('Array36: ');
   t := 0;
   for i := 1 to 2 do
      for x := 1 to 2 do
         for y := 1 to 2 do
            for z := 1 to 2 do
               for q := 1 to 2 do
                  for n := 1 to 2 do
                     begin mdar[i][x, y, z][q][n] := t; t := t+1 end;
   for i := 2 downto 1 do
      for x := 2 downto 1 do
         for y := 2 downto 1 do begin

            for z := 2 downto 1 do
               for q := 2 downto 1 do
                  for n := 2 downto 1 do write(mdar[i, x][y, z][q][n]:2, ' ');
            writeln;

         end;
   writeln('s/b:');
   writeln('63 62 61 60 59 58 57 56');
   writeln('55 54 53 52 51 50 49 48');
   writeln('47 46 45 44 43 42 41 40');
   writeln('39 38 37 36 35 34 33 32');
   writeln('31 30 29 28 27 26 25 24');
   writeln('23 22 21 20 19 18 17 16');
   writeln('15 14 13 12 11 10  9  8');
   writeln(' 7  6  5  4  3  2  1  0');

   { assignments }
   writeln('Array37: ');
   pavc := 'hello, guy';
   writeln(pavc, ' s/b hello, guy');
   writeln('Array38: ');
   for i := 1 to 10 do avi[i] := i+10;
   avi2 := avi;
   for i := 10 downto 1 do write(avi2[i]:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   writeln('Array39: ');
   t := 0;
   for i := 1 to 2 do
      for x := 1 to 2 do
         for y := 1 to 2 do
            for z := 1 to 2 do
               for q := 1 to 2 do
                  for n := 1 to 2 do
                     begin mdar[i][x, y, z][q][n] := t; t := t+1 end;
   mdar2 := mdar;
   for i := 2 downto 1 do
      for x := 2 downto 1 do
         for y := 2 downto 1 do begin

            for z := 2 downto 1 do
               for q := 2 downto 1 do
                  for n := 2 downto 1 do write(mdar2[i, x][y, z][q][n]:2, ' ');
            writeln;

         end;
   writeln('s/b:');
   writeln('63 62 61 60 59 58 57 56');
   writeln('55 54 53 52 51 50 49 48');
   writeln('47 46 45 44 43 42 41 40');
   writeln('39 38 37 36 35 34 33 32');
   writeln('31 30 29 28 27 26 25 24');
   writeln('23 22 21 20 19 18 17 16');
   writeln('15 14 13 12 11 10  9  8');
   writeln(' 7  6  5  4  3  2  1  0');

   { transfer procedures }
   writeln('Array40: ');
   for i := 1 to 10 do pavi[i] := i+10;
   unpack(pavi, avi, 1);
   for i := 10 downto 1 do write(avi[i]:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   writeln('Array41: ');
   for i := 1 to 10 do avi[i] := i+20;
   pack(avi, 1, pavi);
   for i := 10 downto 1 do write(pavi[i]:1, ' ');
   writeln('s/b 30 29 28 27 26 25 24 23 22 21');
   writeln('Array42: ');
   for i := 1 to 10 do pavi[i] := i+30;
   unpack(pavi, cia, 'g');
   for ci := 'p' downto 'g' do write(cia[ci]:1, ' ');
   writeln('s/b 40 39 38 37 36 35 34 33 32 31');
   writeln('Array43: ');
   x := 1;
   for ci := 'a' to 'z' do begin cia[ci] := x; x := x+1 end;
   pack(cia, 'm', pavi);
   for i := 10 downto 1 do write(pavi[i]:1, ' ');
   writeln('s/b 22 21 20 19 18 17 16 15 14 13');
   write('Array44: ');
   la1[maxint] := 876;
   write(la1[maxint]:1);
   writeln(' s/b 876');
   write('Array45: ');
   la2[-maxint] := 724;
   write(la2[-maxint]:1);
   writeln(' s/b 724');

{******************************************************************************

                            Records

******************************************************************************}

   writeln;
   writeln('******************* records ******************************');
   writeln;

   { types in records }
   writeln('Record1:   ');
   arec.i := 64;
   arec.b := false;
   arec.c := 'j';
   arec.e := two;
   arec.es := four;
   arec.s := 12;
   arec.r := 4545.12e-32;
   arec.st := 'what ? who';
   for i := 1 to 10 do arec.a[i] := i+20;
   arec.rc.a := 2324;
   arec.rc.b := 'y';
   arec.stc := ['b'..'e', 'i'];
   new(arec.p);
   arec.p^ := 8454;
   writeln(arec.i:1, ' ', arec.b:5, ' ', arec.c:1, ' ', ord(arec.e):1, ' ',
           ord(arec.es):1,
           ' ', arec.s:1, ' ', arec.r:15, ' ', arec.st);
   for i := 1 to 10 do write(arec.a[i]:1, ' '); writeln;
   writeln(arec.rc.a:1, ' ', arec.rc.b:1);
   for ci := 'a' to 'j' do if ci in arec.stc then write(ci) else write('_');
   writeln;
   writeln(arec.p^:1);
   writeln('s/b:');
   writeln('64 false j 1 3 12  4.54512000e-29 what ? who');
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');
   writeln('Record2:   ');
   parec.i := 64;
   parec.b := false;
   parec.c := 'j';
   parec.e := two;
   parec.es := four;
   parec.s := 12;
   parec.r := 4545.12e-32;
   parec.st := 'what ? who';
   for i := 1 to 10 do parec.a[i] := i+20;
   parec.rc.a := 2324;
   parec.rc.b := 'y';
   parec.stc := ['b'..'e', 'i'];
   new(parec.p);
   parec.p^ := 8454;
   writeln(parec.i:1, ' ', parec.b:5, ' ', parec.c:1, ' ', ord(parec.e):1, ' ',
           ord(parec.es):1,
           ' ', parec.s:1, ' ', parec.r:15, ' ', parec.st);
   for i := 1 to 10 do write(parec.a[i]:1, ' '); writeln;
   writeln(parec.rc.a:1, ' ', parec.rc.b:1);
   for ci := 'a' to 'j' do if ci in parec.stc then write(ci) else write('_');
   writeln;
   writeln(parec.p^:1);
   writeln('s/b:');
   writeln('64 false j 1 3 12  4.54512000e-29 what ? who');
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');

   { types in variants, and border clipping }
   write('Record3:   ');
   vra.i := 873;
   vra.vt := vti;
   vra.a := 427;
   vra.vdi := 235;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdi:1, ' ', vra.a:1);
   writeln(' s/b 873 0 235 427');
   write('Record4:   ');
   vra.i := 873;
   vra.vt := vtb;
   vra.b := 427;
   vra.vdb := true;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdb:5, ' ', vra.b:1);
   writeln(' s/b 873 1  true 427');
   write('Record5:   ');
   vra.i := 873;
   vra.vt := vtc;
   vra.c := 427;
   vra.vdc := 'f';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdc, ' ', vra.c:1);
   writeln(' s/b 873 2 f 427');
   write('Record6:   ');
   vra.i := 873;
   vra.vt := vte;
   vra.d := 427;
   vra.vde := nine;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', ord(vra.vde):1, ' ', vra.d:1);
   writeln(' s/b 873 3 8 427');
   write('Record7:   ');
   vra.i := 873;
   vra.vt := vtes;
   vra.e := 427;
   vra.vdes := four;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', ord(vra.vdes):1, ' ', vra.e:1);
   writeln(' s/b 873 4 3 427');
   write('Record8:   ');
   vra.i := 873;
   vra.vt := vts;
   vra.f := 427;
   vra.vds := 12;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vds:1, ' ', vra.f:1);
   writeln(' s/b 873 5 12 427');
   write('Record9:   ');
   vra.i := 873;
   vra.vt := vtr;
   vra.g := 427;
   vra.vdr := 8734.8389;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdr:1:4, ' ', vra.g:1);
   writeln(' s/b 873 6 8734.8389 427');
   write('Record10:  ');
   vra.i := 873;
   vra.vt := vtst;
   vra.h := 427;
   vra.vdst := 'this one ?';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdst, ' ', vra.h:1);
   writeln(' s/b 873 7 this one ? 427');
   write('Record11:  ');
   vra.i := 873;
   vra.vt := vta;
   vra.j := 427;
   for i := 1 to 10 do vra.vda[i] := i+10;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ');
   for i := 10 downto 1 do write(vra.vda[i]:1, ' ');
   writeln(vra.j:1);
   writeln('      s/b:  873 8 20 19 18 17 16 15 14 13 12 11 427');
   write('Record12:  ');
   vra.i := 873;
   vra.vt := vtrc;
   vra.k := 427;
   vra.vdrc.a := 2387;
   vra.vdrc.b := 't';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdrc.a:1, ' ', vra.vdrc.b, ' ',
         vra.k:1);
   writeln(' s/b:  873 9 2387 t 427');
   write('Record13:  ');
   vra.i := 873;
   vra.vt := vtstc;
   vra.l := 427;
   vra.vdstc := ['b'..'g', 'i'];
   write(vra.i:1, ' ', ord(vra.vt):1, ' ');
   for ci := 'j' downto 'a' do if ci in vra.vdstc then write(ci) else write('_');
   writeln(' ', vra.l:1);
   writeln('      s/b:  873 10 _i_gfedcb_ 427');
   write('Record14:  ');
   vra.i := 873;
   vra.vt := vtp;
   vra.m := 427;
   new(vra.vdp);
   vra.vdp^ := 2394;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdp^:1, ' ', vra.m:1);
   writeln(' s/b 873 11 2394 427');

   { types of variant tags }
   write('Record15:  ');
   vvrs.vt := 10;
   vvrs.vi := 2343;
   write(vvrs.vt:1, ' ', vvrs.vi:1);
   writeln(' s/b 10 2343');
   write('Record16:  ');
   vvrs.vt := 19;
   vvrs.vb := true;
   write(vvrs.vt:1, ' ', vvrs.vb:5);
   writeln(' s/b 19  true');
   write('Record17:  ');
   vvrb.vt := true;
   vvrb.vi := 2343;
   write(vvrb.vt:5, ' ', vvrb.vi:1);
   writeln(' s/b  true 2343');
   write('Record18:  ');
   vvrb.vt := false;
   vvrb.vb := true;
   write(vvrb.vt:5, ' ', vvrb.vb:5);
   writeln(' s/b false  true');
   write('Record19:  ');
   vvre.vt := three;
   vvre.vi := 2343;
   write(ord(vvre.vt):1, ' ', vvre.vi:1);
   writeln(' s/b 2 2343');
   write('Record20:  ');
   vvre.vt := eight;
   vvre.vb := true;
   write(ord(vvre.vt):1, ' ', vvre.vb:5);
   writeln(' s/b 7  true');
   write('Record21:  ');
   vvres.vt := four;
   vvres.vi := 2343;
   write(ord(vvres.vt):1, ' ', vvres.vi:1);
   writeln(' s/b 3 2343');
   write('Record22:  ');
   vvres.vt := five;
   vvres.vb := true;
   write(ord(vvres.vt):1, ' ', vvres.vb:5);
   writeln(' s/b 4  true');
   { change to another tag constant in same variant }
   write('Record23:  ');
   vvrs.vt := 10;
   vvrs.vi := 42;
   i := vvrs.vi;
   vvrs.vt := 11;
   i := vvrs.vi;
   writeln(i:1, ' s/b 42');

   { nested records }
   write('Record24:  ');
   nvr.i := 1;
   nvr.r.i := 2;
   nvr.r.r.i := 3;
   nvr.r.r.r.i := 4;
   nvr.r.r.r.r.i := 5;
   nvr.r.r.r.r.r.i := 6;
   nvr.r.r.r.r.r.r.i := 7;
   nvr.r.r.r.r.r.r.r.i := 8;
   nvr.r.r.r.r.r.r.r.r.i := 9;
   nvr.r.r.r.r.r.r.r.r.r.i := 10;
   writeln(nvr.i:1, ' ',
           nvr.r.i:1, ' ',
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ',
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 1 2 3 4 5 6 7 8 9 10');

   { 'with' statements }
   write('Record25:  ');
   with nvr do begin

      i := 10;
      with r do begin

         i := 9;
         with r do begin

            i := 8;
            with r do begin

               i := 7;
               with r do begin

                  i := 6;
                  with r do begin

                     i := 5;
                     with r do begin

                        i := 4;
                        with r do begin

                           i := 3;
                           with r do begin

                              i := 2;
                              with r do begin

                                 i := 2;
                                 with r do begin

                                    i := 1

                                 end

                              end

                           end

                        end

                     end

                  end

               end

            end

         end

      end

   end;
   writeln(nvr.i:1, ' ',
           nvr.r.i:1, ' ',
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ',
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 10 9 8 7 6 5 4 3 2 1');
   write('Record26:  ');
   with nvr, r, r, r, r, r, r, r, r, r do i := 76;
   writeln(nvr.i:1, ' ',
           nvr.r.i:1, ' ',
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ',
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 10 9 8 7 6 5 4 3 2 76');
   write('Record27:  ');
   new(rpa);
   with rpa^ do begin

      i := 1;
      with rc do b := 'g'

   end;
   writeln(rpa^.i:1, ' ', rpa^.rc.b, ' s/b 1 g');
   write('Record28:  ');
   for i := 1 to 10 do with ara[i] do a := i+10;
   for i := 10 downto 1 do with ara[i] do write(a:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Record29: ');
   new(rpb, false, true);
   rpb^.i := 42;
   rpb^.b := false;
   rpb^.q := true;
   rpb^.r := 12.34;
   write(rpb^.i:1, ' ', rpb^.b, ' ', rpb^.q, ' ', rpb^.r);
   writeln(' s/b 42 False True 1.234000000000000e+01');
   dispose(rpb, false, true);
   write('Record30: ');
   new(rpc, 10);
   rpc^.vt := 10;
   rpc^.vi := 185;
   rpc^.vt := 14;
   write(rpc^.vi:1);
   writeln(' s/b 185');
   dispose(rpc, 15);
   write('Record31: ');
   new(rpd, false);
   rpd^.b := false;
   rpd^.q.z := true;
   rpd^.n := 42;
   write(rpd^.n:1);
   dispose(rpd, false);
   writeln(' s/b 42');

{******************************************************************************

                            Files

******************************************************************************}

if testfile then begin

   writeln;
   writeln('******************* files ******************************');
   writeln;

   { file base types }
   write('File1:   ');
   rewrite(fi);
   for i := 1 to 10 do write(fi, i+10);
   reset(fi);
   for i := 1 to 10 do begin read(fi, x); write(x:1, ' ') end;
   writeln('s/b 11 12 13 14 15 16 17 18 19 20');
   write('File2:   ');
   rewrite(pfi);
   for i := 1 to 10 do write(pfi, i+10);
   reset(pfi);
   for i := 1 to 10 do begin read(pfi, x); write(x:1, ' ') end;
   writeln('s/b 11 12 13 14 15 16 17 18 19 20');
   write('File3:   ');
   rewrite(fb);
   for i := 1 to 10 do write(fb, odd(i));
   reset(fb);
   for i := 1 to 10 do begin read(fb, ba); write(ba:5, ' ') end;
   writeln;
   writeln('   s/b:    true false  true false  true false  true false  true ',
           'false');
   write('File4:   ');
   rewrite(pfb);
   for i := 1 to 10 do write(pfb, odd(i));
   reset(pfb);
   for i := 1 to 10 do begin read(pfb, ba); write(ba:5, ' ') end;
   writeln;
   writeln('   s/b:    true false  true false  true false  true false  true ',
           'false');
   write('File5:   ');
   rewrite(fc);
   for ci := 'a' to 'j' do write(fc, ci);
   reset(fc);
   for ci := 'a' to 'j' do begin read(fc, ca); write(ca, ' ') end;
   writeln('s/b a b c d e f g h i j');
   write('File6:   ');
   rewrite(pfc);
   for ci := 'a' to 'j' do write(pfc, ci);
   reset(pfc);
   for ci := 'a' to 'j' do begin read(pfc, ca); write(ca, ' ') end;
   writeln('s/b a b c d e f g h i j');
   write('File7:   ');
   rewrite(fe);
   for ei := one to ten do write(fe, ei);
   reset(fe);
   for ei := one to ten do begin read(fe, ea); write(ord(ea):1, ' ') end;
   writeln('s/b 0 1 2 3 4 5 6 7 8 9');
   write('File8:   ');
   rewrite(pfe);
   for ei := one to ten do write(pfe, ei);
   reset(pfe);
   for ei := one to ten do begin read(pfe, ea); write(ord(ea):1, ' ') end;
   writeln('s/b 0 1 2 3 4 5 6 7 8 9');

   { types written to text }
   writeln('File9:');
   rewrite(ft);
   x := 7384;
   writeln(ft, x:1);
   writeln(ft, 8342:1);
   ba := true;
   writeln(ft, ba:5);
   writeln(ft, false:5);
   ca := 'm';
   writeln(ft, ca);
   writeln(ft, 'q');
   ra := 1234.5678e-3;
   writeln(ft, ra:15);
   writeln(ft, ra:1:7);
   writeln(ft, 5689.4321e-2:15);
   writeln(ft, 9383.7632e-4:1:8);
   s := 'hi there !';
   writeln(ft, s);
   writeln(ft, s:5);
   writeln(ft, s:15);
   reset(ft); get(ft); cc := ft^; reset(ft);
   while not eof(ft) do begin

      if eoln(ft) then begin

         readln(ft);
         writeln

      end else begin

         read(ft, ci);
         write(ci)

      end

   end;
   writeln('s/b:');
   writeln('7384');
   writeln('8342');
   writeln(' true');
   writeln('false');
   writeln('m');
   writeln('q');
   writeln(' 1.2345678000e+00');
   writeln('1.2345678');
   writeln(' 5.6894321000e+01');
   writeln('0.93837632');
   writeln('hi there !');
   writeln('hi th');
   writeln('     hi there !');

   { types read from text }
   writeln('file10:');
   reset(ft);
   readln(ft, y);
   writeln(y:1);
   readln(ft, y);
   writeln(y:1);
   readln(ft);
   readln(ft);
   readln(ft, ci);
   writeln(ci);
   readln(ft, ci);
   writeln(ci);
   readln(ft, rb);
   writeln(rb:15);
   readln(ft, rb);
   writeln(rb:15);
   readln(ft, rb);
   writeln(rb:15);
   readln(ft, rb);
   writeln(rb:15);
   writeln('s/b:');
   writeln('7384');
   writeln('8342');
   writeln('m');
   writeln('q');
   writeln(' 1.2345678000e+00');
   writeln(' 1.2345678000e+00');
   writeln(' 5.6894321000e+01');
   writeln(' 9.3837632000e-01');

   { line and file endings in text }
   writeln('file11:');
   rewrite(ft);
   writeln(ft, 'how now');
   writeln(ft, 'brown cow');
   reset(ft);
   write('''');
   while not eof(ft) do begin

      if eoln(ft) then write('<eoln>');
      read(ft, ca);
      write(ca)

   end;
   write('''');
   writeln(' s/b ''how now<eoln> brown cow<eoln> ''');
   writeln('file12:');
   rewrite(ft);
   writeln(ft, 'too much');
   write(ft, 'too soon');
   reset(ft);
   write('''');
   while not eof(ft) do begin

      if eoln(ft) then write('<eoln>');
      read(ft, ca);
      write(ca)

   end;
   write('''');
   writeln(' s/b ''too much<eoln> too soon<eoln> ''');

   { get/put and buffer variables }
   write('File13:   ');
   rewrite(fi);
   for i := 1 to 10 do begin fi^ := i+10; put(fi) end;
   reset(fi);
   for i := 1 to 10 do begin x := fi^; get(fi); write(x:1, ' ') end;
   writeln('s/b 11 12 13 14 15 16 17 18 19 20');
   write('File14:   ');
   rewrite(pfi);
   for i := 1 to 10 do begin pfi^ := i+10; put(pfi) end;
   reset(pfi);
   for i := 1 to 10 do begin x := pfi^; get(pfi); write(x:1, ' ') end;
   writeln('s/b 11 12 13 14 15 16 17 18 19 20');
   write('File15:   ');
   rewrite(fb);
   for i := 1 to 10 do begin fb^ := odd(i); put(fb) end;
   reset(fb);
   for i := 1 to 10 do begin ba := fb^; get(fb); write(ba:5, ' ') end;
   writeln;
   writeln('   s/b:    true false  true false  true false  true false  true ',
           'false');
   write('File16:   ');
   rewrite(pfb);
   for i := 1 to 10 do begin pfb^ := odd(i); put(pfb) end;
   reset(pfb);
   for i := 1 to 10 do begin ba := pfb^; get(pfb); write(ba:5, ' ') end;
   writeln;
   writeln('   s/b:    true false  true false  true false  true false  true ',
           'false');
   write('File17:   ');
   rewrite(fc);
   for ci := 'a' to 'j' do begin fc^ := ci; put(fc) end;
   reset(fc);
   for ci := 'a' to 'j' do begin ca := fc^; get(fc); write(ca, ' ') end;
   writeln('s/b a b c d e f g h i j');
   write('File18:   ');
   rewrite(pfc);
   for ci := 'a' to 'j' do begin pfc^ := ci; put(pfc) end;
   reset(pfc);
   for ci := 'a' to 'j' do begin ca := pfc^; get(pfc); write(ca, ' ') end;
   writeln('s/b a b c d e f g h i j');
   write('File19:   ');
   rewrite(fe);
   for ei := one to ten do begin fe^ := ei; put(fe) end;
   reset(fe);
   for ei := one to ten do begin ea := fe^; get(fe); write(ord(ea):1, ' ') end;
   writeln('s/b 0 1 2 3 4 5 6 7 8 9');
   write('File20:   ');
   rewrite(pfe);
   for ei := one to ten do begin pfe^ := ei; put(pfe) end;
   reset(pfe);
   for ei := one to ten do begin ea := pfe^; get(pfe); write(ord(ea):1, ' ') end;
   writeln('s/b 0 1 2 3 4 5 6 7 8 9');
   write('File21:   ');
   rewrite(ft);
   writeln(ft, '50');
   reset(ft);
   read(ft, srx);
   write(srx:1);
   writeln(' s/b ', 50:1);
   write('File22:   ');
   rewrite(ft);
   writeln(eof(ft), ' s/b true');
   writeln(output, 'File23:');
   for i := 1 to 3 do begin
   
      while not eoln do begin
    
         read(ca);
         write(output, ca)
      
      end;
      readln;
      writeln(output)
      
   end;
   readln(i);
   writeln(i:1);
   readln(ra);
   writeln(ra);
   writeln('s/b');
   writeln('Test line 1');
   writeln('Test line 2');
   writeln('Test line 3');
   writeln('4567');
   writeln('-1.234567890000000e-10');
   write('File24: ');
   rewrite(ft);
   writeln(ft, 42);
   writeln(ft, 7645);
   writeln(ft, 945);
   reset(ft);
   read(ft, x, y, z);
   write(x:1, ' ', y:1, ' ', z:1);
   writeln(' s/b 42 7645 945');
   writeln('File25: ');
   rewrite(ft);
   writeln(ft, 1.2);
   writeln(ft, 849.23e-6);
   writeln(ft, 134.99e10);
   reset(ft);
   read(ft, ra, rb, rc);
   writeln(ra);
   writeln(rb);
   writeln(rc);
   writeln('s/b');
   writeln(' 1.200000000000000e+00');
   writeln(' 8.492300000000000e-04');
   writeln(' 1.349900000000000e+12');
   
end;

{******************************************************************************

                         Procedures and functions

******************************************************************************}

   writeln;
   writeln('************ Procedures and functions ******************');
   writeln;
   write('ProcedureFunction1:   ');
   x := 45; y := 89;
   junk1(x, y);
   writeln(' s/b 45 89');
   write('ProcedureFunction2:   ');
   x := 45; junk2(x);
   writeln(x:1, ' s/b 46');
   write('ProcedureFunction3:   ');
   s := 'total junk';
   junk3(s);
   writeln(' s/b total junk');
   write('ProcedureFunction4:   ');
   s := 'total junk';
   junk4(s);
   writeln(' s/b tota? junk');
   writeln('                      ', s, ' s/b total junk');
   write('ProcedureFunction5:   ');
   writeln(junk5(34):1, ' s/b 35');
   write('ProcedureFunction6:   ');
   i := junk7(10, 9, 8);
   writeln(' ', i:1);
   writeln('s/b:   10 9 8 6 5 4 3 2 1 78');
   writeln('ProcedureFunction7:');
   for i := 1 to 10 do ai[i] := i+10;
   arec.i := 64;
   arec.b := false;
   arec.c := 'j';
   arec.e := two;
   arec.es := four;
   arec.s := 12;
   arec.r := 4545.12e-32;
   arec.st := 'what ? who';
   for i := 1 to 10 do arec.a[i] := i+20;
   arec.rc.a := 2324;
   arec.rc.b := 'y';
   arec.stc := ['b'..'e', 'i'];
   new(arec.p);
   arec.p^ := 8454;
   vrec.a := 23487;
   vrec.b := 'n';
   vrec.c := false;
   vrec.d := 'help me123';
   new(ip);
   ip^ := 734;
   junk8(93, true, 'k', eight, five, 10, 3.1414, 'hello, guy', ai, arec, vrec,
         ['a'..'d', 'h'], ip);
   writeln('s/b:');
   writeln('93  true k 7 4 10  3.14140000e+00 hello, guy');
   writeln('11 12 13 14 15 16 17 18 19 20');
   writeln('64 false j 1 3 12  4.54512000e-29 what ? who');
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');
   writeln('23487 n false');
   writeln('help me123');
   writeln('abcd___h__');
   writeln('734');
   write('ProcedureFunction8:   ');
   junk9(junk10, junk11);
   writeln(' s/b 9834 8383 j 744');
   write('ProcedureFunction9:   ');
   junk12(junk13, junk11);
   writeln(' s/b 942');
   write('ProcedureFunction10:   ');
   junk14;
   writeln(' s/b 62 76');
   write('ProcedureFunction11:   ');
   junk17(junk16, 52);
   writeln(' s/b 52');
   write('ProcedureFunction12:   ');
   junk19;
   writeln(' s/b a');
   write('ProcedureFunction13:   ');
   writeln(junk20:1, ' s/b 37');
   write('ProcedureFunction14:   ');
   writeln(junk21:1, ' s/b 35');
   write('ProcedureFunction15:   ');
   junk22(15);
   writeln(' s/b 15');

{******************************************************************************

                        Integer file buffer variables

******************************************************************************}

   writeln;
   writeln('**************** Integer file buffer variables ****************');
   writeln;

   { integer variables }
   fia^ := 43; fib^ := 78; fic^ := fib^;
   writeln('intfb1:   ', fia^ + fib^:1, ' s/b 121');
   writeln('intfb2:   ', fib^ - fia^:1, ' s/b 35');
   writeln('intfb3:   ', fia^ * fib^:1, ' s/b 3354');
   writeln('intfb4:   ', fib^ div fia^:1, ' s/b 1');
   writeln('intfb5:   ', fib^ mod fia^:1, ' s/b 35');
   writeln('intfb6:   ', succ(fia^):1, ' s/b 44');
   writeln('intfb7:   ', pred(fia^):1, ' s/b 42');
   writeln('intfb8:   ', sqr(fia^):1, ' s/b 1849');
   writeln('intfb9:   ', chr(fib^), ' s/b N');
   writeln('intfb10:  ', ord(chr(fia^)):1, ' s/b 43');
   writeln('intfb11:  ', odd(fia^):5, ' s/b true');
   writeln('intfb12:  ', odd(fib^):5, ' s/b false');
   writeln('intfb13:  ', fic^ = fib^:5, ' s/b true');
   writeln('intfb14:  ', fia^ = fib^:5, ' s/b false');
   writeln('intfb15:  ', fia^ < fib^:5, ' s/b true');
   writeln('intfb16:  ', fib^ < fia^:5, ' s/b false');
   writeln('intfb17:  ', fib^ > fia^:5, ' s/b true');
   writeln('intfb18:  ', fia^ > fib^:5, ' s/b false');
   writeln('intfb19:  ', fia^ <> fib^:5, ' s/b true');
   writeln('intfb20:  ', fib^ <> fic^:5, ' s/b false');
   writeln('intfb21:  ', fia^ <= fib^:5, ' s/b true');
   writeln('intfb22:  ', fic^ <= fib^:5, ' s/b true');
   writeln('intfb23:  ', fib^ <= fia^:5, ' s/b false');
   writeln('intfb24:  ', fib^ >= fia^:5, ' s/b true');
   writeln('intfb25:  ', fib^ >= fic^:5, ' s/b true');
   writeln('intfb26:  ', fia^ >= fib^:5, ' s/b false');

   { signed integer variables }
   fia^ := -14;
   fib^ := -32;
   fic^ := -14;
   fid^ := 20;
   fie^ := -15;
   fig^ := maxint;
   fih^ := mmaxint;
   vnum := -maxint;
   writeln('intfb55:  ', fia^ + fid^:1, ' s/b 6');
   writeln('intfb56:  ', fid^ + fia^:1, ' s/b 6');
   writeln('intfb57:  ', fib^ + fid^:1, ' s/b -12');
   writeln('intfb58:  ', fia^ + fib^:1, ' s/b -46');
   writeln('intfb59:  ', fid^ - fia^:1, ' s/b 34');
   writeln('intfb60:  ', fib^ - fid^:1, ' s/b -52');
   writeln('intfb61:  ', fib^ - fia^:1, ' s/b -18');
   writeln('intfb62:  ', fid^ * fia^:1, ' s/b -280');
   writeln('intfb63:  ', fia^ * fid^:1, ' s/b -280');
   writeln('intfb64:  ', fia^ * fib^:1, ' s/b 448');
   writeln('intfb65:  ', fid^ div fia^:1, ' s/b -1');
   writeln('intfb66:  ', fib^ div fid^:1, ' s/b -1');
   writeln('intfb67:  ', fib^ div fia^:1, ' s/b 2');
   writeln('intfb68:  ', succ(fia^):1, ' s/b -13');
   writeln('intfb69:  ', pred(fib^):1, ' s/b -33');
   writeln('intfb70: ', sqr(fia^):1, ' s/b 196');
   writeln('intfb71:  ', odd(fia^):5, ' s/b false');
   writeln('intfb72:  ', odd(fie^):5, ' s/b true');
   writeln('intfb73:  ', fia^ = fic^:5, ' s/b true');
   writeln('intfb74:  ', fia^ = fib^:5, ' s/b false');
   writeln('intfb75:  ', fia^ <> fib^:5, ' s/b true');
   writeln('intfb76:  ', fia^ <> fic^:5, ' s/b false');
   writeln('intfb77:  ', fia^ < fid^:5, ' s/b true');
   writeln('intfb78:  ', fib^ < fia^:5, ' s/b true');
   writeln('intfb79:  ', fid^ < fia^:5, ' s/b false');
   writeln('intfb80:  ', fia^ < fib^:5, ' s/b false');
   writeln('intfb81:  ', fid^ > fia^:5, ' s/b true');
   writeln('intfb82:  ', fia^ > fib^:5, ' s/b true');
   writeln('intfb83:  ', fia^ > fid^:5, ' s/b false');
   writeln('intfb84:  ', fib^ > fia^:5, ' s/b false');
   writeln('intfb85:  ', fia^ <= fid^:5, ' s/b true');
   writeln('intfb86:  ', fib^ <= fia^:5, ' s/b true');
   writeln('intfb87:  ', fia^ <= fic^:5, ' s/b true');
   writeln('intfb88:  ', fid^ <= fia^:5, ' s/b false');
   writeln('intfb89:  ', fia^ <= fib^:5, ' s/b false');
   writeln('intfb90:  ', fid^ >= fia^:5, ' s/b true');
   writeln('intfb91:  ', fia^ >= fib^:5, ' s/b true');
   writeln('intfb92:  ', fia^ >= fic^:5, ' s/b true');
   writeln('intfb93:  ', fia^ >= fid^:5, ' s/b false');
   writeln('intfb94:  ', fib^ >= fia^:5, ' s/b false');
   writeln('intfb95:  ', abs(fia^):1, ' s/b 14');
   writeln('intfb96:  ', fig^+fih^:1, ' s/b 0');
   writeln('intfb97:  ', fig^-maxint:1, ' s/b 0');
   writeln('intfb98:  ', fig^+vnum:1, ' s/b 0');

end.
