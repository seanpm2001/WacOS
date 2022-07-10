(*$c+,t-,d-,l+*)
(*Assembler and interpreter of Pascal code*)
(*K. Jensen, N. Wirth, Ch. Jacobi, ETH May 76*)

program pcode(input,output,prd,prr);

(* Note for the implementation.
   ===========================
This interpreter is written for the case where all the fundamental types
take one storage unit.
In an actual implementation, the handling of the sp pointer has to take
into account the fact that the types may have lengths different from one:
in push and pop operations the sp has to be increased and decreased not
by 1, but by a number depending on the type concerned.
However, where the number of units of storage has been computed by the
compiler, the value must not be corrected, since the lengths of the types
involved have already been taken into account.
                                                                 *)

(*********************************************************************

Adaption comments

Scott A. Moore samiam@moorecad.com

The purpose of my changes is to upgrade the code to ISO 7185.
Note that none of the changes are nonstandard, and in fact make the
code more, not less, standard.

- I detabbed it, at 8th tabs. Not everyone uses the same tab stops.
Use spaces please.

- In the "store" array, added "undef" case to record definition as
required by the standard.

- I removed unused variables in "assemble", "callsp", "load".

- I restored the number of spaces in various error strings to 25,
the length of beta. It looks like the program got tab expanded
incorrectly.

- Several opcodes that indicated different types were combined in the
original code. They worked because many of the basic variable types
were interchangable, such as integers and characters. This is not
true on other machines, and these needed to be broken out according
to type.

- Per the last item, 'ord' and 'chr' are no longer no-ops, so they
were added to both assemble and main (interpret).

The program originally came from this site:

http://www.cwi.nl/~steven/pascal.html

And they mention several changes made from the book form.

**********************************************************************)

label 1;
const codemax     =  50000(*8650*); (* increased this for testing [sam] *)
      pcmax       =  200000(*17500*);
      maxstk      =   30000(*13650*); (* size of variable store *)
      overi       =   40000(*13655*); (* size of integer constant table = 5 *)
      (* increased for testing [sam] *)
      overr       =   50000(*13660*); (* size of real constant table = 5 *)
      overs       =   60000(*13730*); (* size of set constant table = 70 *)
      overb       =   70000(*13820*);
      overm       = 400000(*18000*); (* increased this for testing [sam] *)
      maxstr      = 400001(*18001*); (* increased this for testing [sam] *)
      largeint    = 26144;
      begincode   = 3;
      inputadr    = 5;
      outputadr   = 6;
      prdadr      = 7;
      prradr      = 8;
      duminst     = 62;
      (* Parameterized length of strings in intermediate. It was already a 
        comment, just changed to a real number. [sam] *)
      stringlgth      = 100(*16*);
                
type  bit4        = 0..15;
      bit6        = 0..127;
      bit20       = -1080000..1080000(*-26143..26143*); (* increased for testing [sam] *)
      datatype    = (undef,int,reel,bool,sett,adr,mark,car);
      address     = -1..maxstr;
      beta        = packed array[1..25] of char; (*error message*)
      (* these types were added for CDC types [sam] *)
      settype     = set of 0..47;
      alfa        = packed array[1..10] of char;

var   code        : array[0..codemax] of   (* the program *)
                      packed record  op1    :bit6;
                                     p1     :bit4;
                                     q1     :bit20;
                                     op2    :bit6;
                                     p2     :bit4;
                                     q2     :bit20
                             end;
      pc           : 0..pcmax;   (*program address register*)
      op : bit6; p : bit4; q : bit20;  (*instruction register*)

      store        : array [0..overm] of
                       packed record case datatype of
                                (* added null case [sam] *)
                                undef:  ();
                                int     :(vi :integer);
                                reel    :(vr :real);
                                bool    :(vb :boolean);
                                (* changed settype to global type [sam] *)
                                sett    :(vs :settype);
                                car     :(vc :char);
                                adr     :(va :address);
                                          (*address in store*)
                                mark    :(vm :integer)
                        end;
       mp,sp,np,ep : address;  (* address registers *)
       (*mp  points to beginning of a data segment
         sp  points to top of the stack
         ep  points to the maximum extent of the stack
         np  points to top of the dynamically allocated area*)

       interpreting: boolean;
       {prd,prr     : text;}(*prd for read only, prr for write only *)

       instr       : array[bit6] of alfa; (* mnemonic instruction codes *)
       cop         : array[bit6] of integer;
       sptable     : array[0..20] of alfa; (*standard functions and procedures*)

      (*locally used for interpreting one instruction*)
       ad,ad1      : address;
       b           : boolean;
       i,j,i1,i2   : integer;
       c           : char;
       i3          : integer; (* [sam] for needed its own index *)

(*--------------------------------------------------------------------*)

procedure load;
   const maxlabel = 1850;
   type  labelst  = (entered,defined); (*label situation*)
         labelrg  = 0..maxlabel;       (*label range*)
         labelrec = record
                          val: address;
                           st: labelst
                    end;
   (* unused variables removed [sam] *)
   var  icp,rcp,scp,bcp,mcp  : address;  (*pointers to next free position*)
        word : array[1..10] of char; (*i  : integer;*) ch  : char;
        labeltab: array[labelrg] of labelrec;
        labelvalue: address;

   procedure init;
      var i: integer;
   begin instr[ 0]:='lod       ';       instr[ 1]:='ldo       ';
         instr[ 2]:='str       ';       instr[ 3]:='sro       ';
         instr[ 4]:='lda       ';       instr[ 5]:='lao       ';
         instr[ 6]:='sto       ';       instr[ 7]:='ldc       ';
         instr[ 8]:='...       ';       instr[ 9]:='ind       ';
         instr[10]:='inc       ';       instr[11]:='mst       ';
         instr[12]:='cup       ';       instr[13]:='ent       ';
         instr[14]:='ret       ';       instr[15]:='csp       ';
         instr[16]:='ixa       ';       instr[17]:='equ       ';
         instr[18]:='neq       ';       instr[19]:='geq       ';
         instr[20]:='grt       ';       instr[21]:='leq       ';
         instr[22]:='les       ';       instr[23]:='ujp       ';
         instr[24]:='fjp       ';       instr[25]:='xjp       ';
         instr[26]:='chk       ';       instr[27]:='eof       ';
         instr[28]:='adi       ';       instr[29]:='adr       ';
         instr[30]:='sbi       ';       instr[31]:='sbr       ';
         instr[32]:='sgs       ';       instr[33]:='flt       ';
         instr[34]:='flo       ';       instr[35]:='trc       ';
         instr[36]:='ngi       ';       instr[37]:='ngr       ';
         instr[38]:='sqi       ';       instr[39]:='sqr       ';
         instr[40]:='abi       ';       instr[41]:='abr       ';
         instr[42]:='not       ';       instr[43]:='and       ';
         instr[44]:='ior       ';       instr[45]:='dif       ';
         instr[46]:='int       ';       instr[47]:='uni       ';
         instr[48]:='inn       ';       instr[49]:='mod       ';
         instr[50]:='odd       ';       instr[51]:='mpi       ';
         instr[52]:='mpr       ';       instr[53]:='dvi       ';
         instr[54]:='dvr       ';       instr[55]:='mov       ';
         instr[56]:='lca       ';       instr[57]:='dec       ';
         instr[58]:='stp       ';       instr[59]:='ord       ';
         instr[60]:='chr       ';       instr[61]:='ujc       ';

         sptable[ 0]:='get       ';     sptable[ 1]:='put       ';
         sptable[ 2]:='rst       ';     sptable[ 3]:='rln       ';
         sptable[ 4]:='new       ';     sptable[ 5]:='wln       ';
         sptable[ 6]:='wrs       ';     sptable[ 7]:='eln       ';
         sptable[ 8]:='wri       ';     sptable[ 9]:='wrr       ';
         sptable[10]:='wrc       ';     sptable[11]:='rdi       ';
         sptable[12]:='rdr       ';     sptable[13]:='rdc       ';
         sptable[14]:='sin       ';     sptable[15]:='cos       ';
         sptable[16]:='exp       ';     sptable[17]:='log       ';
         sptable[18]:='sqt       ';     sptable[19]:='atn       ';
         sptable[20]:='sav       ';

         cop[ 0] := 105;  cop[ 1] :=  65;
         cop[ 2] :=  70;  cop[ 3] :=  75;
         cop[ 6] :=  80;  cop[ 9] :=  85;
         cop[10] :=  90;  cop[26] :=  95;
         cop[57] := 100;

         pc  := begincode;
         icp := maxstk + 1;
         rcp := overi + 1;
         scp := overr + 1;
         bcp := overs + 2;
         mcp := overb + 1;
         for i:= 1 to 10 do word[i]:= ' ';
         for i:= 0 to maxlabel do
             with labeltab[i] do begin val:=-1; st:= entered end;
         {reset(prd);}
   end;(*init*)

   procedure errorl(string: beta); (*error in loading*)
   begin writeln;
      write(string);
      (*halt*) goto 1 { P5 no halt procedure }
   end; (*errorl*)

   procedure update(x: labelrg); (*when a label definition lx is found*)
      var curr,succ: -1..pcmax;  (*resp. current element and successor element
                                   of a list of future references*)
          endlist: boolean;
   begin
      if labeltab[x].st=defined then errorl(' duplicated label        ')
      else begin
             if labeltab[x].val<>-1 then (*forward reference(s)*)
             begin curr:= labeltab[x].val; endlist:= false;
                while not endlist do
                      with code[curr div 2] do
                      begin
                         if odd(curr) then begin succ:= q2;
                                                 q2:= labelvalue
                                           end
                                      else begin succ:= q1;
                                                 q1:= labelvalue
                                           end;
                         if succ=-1 then endlist:= true
                                    else curr:= succ
                      end;
              end;
              labeltab[x].st := defined;
              labeltab[x].val:= labelvalue;
           end
   end;(*update*)

   procedure assemble; forward;

   procedure generate;(*generate segment of code*)
      var x: integer; (* label number *)
          again: boolean;
   begin
      again := true;
      while again do
            begin read(prd,ch);(* first character of line*)
                  case ch of
                       'i': readln(prd);
                       'l': begin read(prd,x);
                                  if not eoln(prd) then read(prd,ch);
                                  if ch='=' then read(prd,labelvalue)
                                            else labelvalue:= pc;
                                  update(x); readln(prd);
                            end;
                       'q': begin again := false; readln(prd) end;
                       ' ': begin read(prd,ch); assemble end
                  end;
            end
   end; (*generate*)

   (* removed unused variables [sam] *)
   procedure assemble; (*translate symbolic code into machine code and store*)
      (* this label no longer needed *)
      (*label 1;*)   (*goto 1 for instructions without code generation*)
      var name :alfa;  (*b :boolean;*)  r :real;  s :settype;
          (*c1 :char;*)  i,s1,lb,ub :integer;

      procedure lookup(x: labelrg); (* search in label table*)
      begin case labeltab[x].st of
                entered: begin q := labeltab[x].val;
                           labeltab[x].val := pc
                         end;
                defined: q:= labeltab[x].val
            end(*case label..*)
      end;(*lookup*)

      procedure labelsearch;
         var x: labelrg;
      begin while (ch<>'l') and not eoln(prd) do read(prd,ch);
            read(prd,x); lookup(x)
      end;(*labelsearch*)

      procedure getname;
      begin  word[1] := ch; 
         read(prd,word[2],word[3]);
         if not eoln(prd) then read(prd,ch) (*next character*);
         pack(word,1,name) 
      end; (*getname*)

      procedure typesymbol;
        var i: integer;
      begin
        if ch <> 'i' then
          begin
            case ch of
              'a': i := 0;
              'r': i := 1;
              's': i := 2;
              'b': i := 3;
              'c': i := 4;
            end;
            op := cop[op]+i;
          end;
      end (*typesymbol*) ;

   begin  p := 0;  q := 0;  op := 0;
      getname;
      instr[duminst] := name;
      while instr[op]<>name do op := op+1;
      if op = duminst then errorl(' illegal instruction     ');

(*;writeln('assemble: op: ', name, ' fc: ', ch, ' (', op:1, ')');*)
      case op of  (* get parameters p,q *)

          (*equ,neq,geq,grt,leq,les*)
          17,18,19,
          20,21,22: begin case ch of
                              'a': ; (*p = 0*)
                              'i': p := 1;
                              'r': p := 2;
                              'b': p := 3;
                              's': p := 4;
                              'c': p := 6;
                              'm': begin p := 5;
                                     read(prd,q)
                                   end
                          end
                    end;

          (*lod,str*)
          0,2: begin typesymbol; read(prd,p,q)
               end;

          4  (*lda*): read(prd,p,q);

          12 (*cup*): begin read(prd,p); labelsearch end;

          11 (*mst*): read(prd,p);

          14 (*ret*): case ch of
                            'p': p:=0;
                            'i': p:=1;
                            'r': p:=2;
                            'c': p:=3;
                            'b': p:=4;
                            'a': p:=5
                      end;

          (*lao,ixa,mov*)
          5,16,55: read(prd,q);

          (*ldo,sro,ind,inc,dec*)
          1,3,9,10,57: begin typesymbol; read(prd,q)
                       end;

          (*ujp,fjp,xjp*)
          23,24,25: labelsearch;

          13 (*ent*): begin read(prd,p); labelsearch end;

          15 (*csp*): begin for i:=1 to 9 do read(prd,ch); getname;
                           while name<>sptable[q] do  q := q+1
                      end;

          7 (*ldc*): begin case ch of  (*get q*)
                           'i': begin  p := 1;  read(prd,i);
                                   if abs(i)>=largeint then
                                   begin  op := 8;
                                      store[icp].vi := i;  q := maxstk;
                                      repeat  q := q+1  until store[q].vi=i;
                                      if q=icp then
                                      begin  icp := icp+1;
                                        if icp=overi then
                                          errorl(' integer table overflow  ');
                                      end
                                   end  else q := i
                                end;

                           'r': begin  op := 8; p := 2;
                                   read(prd,r);
                                   store[rcp].vr := r;  q := overi;
                                   repeat  q := q+1  until store[q].vr=r;
                                   if q=rcp then
                                   begin  rcp := rcp+1;
                                     if rcp = overr then
                                       errorl(' real table overflow     ');
                                   end
                                end;

                           'n': ; (*p,q = 0*)

                           'b': begin p := 3;  read(prd,q)  end;

                           'c': begin p := 6;
                                  repeat read(prd,ch); until ch <> ' ';
                                  if ch <> '''' then
                                    errorl(' illegal character       ');
                                  read(prd,ch);  q := ord(ch);
                                  read(prd,ch);
                                  if ch <> '''' then
                                    errorl(' illegal character       ');
                                end;
                           '(': begin  op := 8;  p := 4;
                                   s := [ ];  read(prd,ch);
                                   while ch<>')' do
                                   begin read(prd,s1,ch); s := s + [s1]
                                   end;
                                   store[scp].vs := s;  q := overr;
                                   repeat  q := q+1  until store[q].vs=s;
                                   if q=scp then
                                   begin  scp := scp+1;
                                      if scp=overs then
                                        errorl(' set table overflow      ');
                                   end
                                end
                           end (*case*)
                     end;

           26 (*chk*): begin typesymbol;
                         read(prd,lb,ub);
                         if op = 95 then q := lb
                         else
                         begin
                           store[bcp-1].vi := lb; store[bcp].vi := ub;
                           q := overs;
                           repeat  q := q+2
                           until (store[q-1].vi=lb)and (store[q].vi=ub);
                           if q=bcp then
                           begin  bcp := bcp+2;
                              if bcp=overb then
                                errorl(' boundary table overflow ');
                           end
                         end
                       end;

           56 (*lca*): begin
                         if mcp + stringlgth >= overm then
                           errorl(' multiple table overflow ');
                         mcp := mcp+stringlgth;
                         q := mcp;
                         (* changed to use global constant [sam] *)
                         for i := 0 to stringlgth-1 (*stringlgth*) do
                         begin read(prd,ch);
                           store[q+i].vc := ch
                         end;
                       end;

          6 (*sto*): typesymbol;

          (* added ord and chr back as operators [sam] *)

          27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
          48,49,50,51,52,53,54,58, 60:  ; (*added chr*)

          59 (*ord*): begin case ch of
                              'i': p := 1;
                              'r': p := 2;
                              'b': p := 3;
                              's': p := 4;
                              'c': p := 6;
                          end
                    end;

          (* removed [sam] *)
          (*ord,chr*)
          (*59,60: goto 1;*)

          61 (*ujc*): ; (*must have same length as ujp*)

      end; (*case*)

      (* store instruction *)
      with code[pc div 2] do
         if odd(pc) then
         begin  op2 := op; p2 := p; q2 := q
         end  else
         begin  op1 := op; p1 := p; q1 := q
         end;
      pc := pc+1;
      (*1:*) readln(prd); (* removed this label [sam] *)
   end; (*assemble*)

begin (*load*)
   init;
   generate;
   pc := 0;
   generate;
end; (*load*)

(*------------------------------------------------------------------------*)

procedure pmd;
   var s :integer; i: integer;

   procedure pt;
   begin  write(s:6);
      if abs(store[s].vi) < maxint then write(store[s].vi)
      else write('too big ');
      s := s - 1;
      i := i + 1;
      if i = 4 then
         begin writeln(output); i := 0 end;
   end; (*pt*)

begin
   write(' pc =',pc-1:5,' op =',op:3,'  sp =',sp:5,'  mp =',mp:5,
        '  np =',np:5);
   writeln; writeln('--------------------------------------');

   s := sp; i := 0;
   while s>=0 do pt;
   s := maxstk;
   while s>=np do pt;
end; (*pmd*)

procedure errori(string: beta);
begin writeln; writeln(string);
      pmd; goto 1
end;(*errori*)

function base(ld :integer):address;
   var ad :address;
begin  ad := mp;
   while ld>0 do
   begin  ad := store[ad+1].vm;  ld := ld-1
   end;
   base := ad
end; (*base*)

procedure compare;
(*comparing is only correct if result by comparing integers will be*)
begin
  i1 := store[sp].va;
  i2 := store[sp+1].va;
  i := 0; b := true;
  while b and (i<>q) do
    if store[i1+i].vi = store[i2+i].vi then i := i+1
    else b := false
end; (*compare*)

(* unused variables removed [sam] *)
procedure callsp;
   var line: boolean; (*adptr(*,adelnt: address;*)
       (*i: integer;*)

   procedure readi(var f:text);
      var ad: address;
   begin ad:= store[sp-1].va;
         read(f,store[ad].vi);
         store[store[sp].va].vc := f^;
         sp:= sp-2
   end;(*readi*)

   procedure readr(var f: text);
      var ad: address;
   begin ad:= store[sp-1].va;
         read(f,store[ad].vr);
         store[store[sp].va].vc := f^;
         sp:= sp-2
   end;(*readr*)

   procedure readc(var f: text);
      var c: char; ad: address;
   begin read(f,c);
         ad:= store[sp-1].va;
         store[ad].vc := c;
         store[store[sp].va].vc := f^;
         store[store[sp].va].vi := ord(f^);
         sp:= sp-2
   end;(*readc*)

   procedure writestr(var f: text);
      var i,j,k: integer;
          ad: address;
   begin ad:= store[sp-3].va;
         k := store[sp-2].vi; j := store[sp-1].vi;
         (* j and k are numbers of characters *)
         if k>j then for i:=1 to k-j do write(f,' ')
                else j:= k;
         for i := 0 to j-1 do write(f,store[ad+i].vc);
         sp:= sp-4
   end;(*writestr*)

   procedure getfile(var f: text);
      var ad: address;
   begin ad:=store[sp].va;
         get(f); store[ad].vc := f^;
         sp:=sp-1
   end;(*getfile*)

   procedure putfile(var f: text);
      var ad: address;
   begin ad:= store[sp].va;
         f^:= store[ad].vc; put(f);
         sp:= sp-1;
   end;(*putfile*)

begin (*callsp*)
      case q of
           0 (*get*): case store[sp].va of
                           5: getfile(input);
                           6: errori(' get on output file      ');
                           7: getfile(prd);
                           8: errori(' get on prr file         ')
                      end;
           1 (*put*): case store[sp].va of
                           5: errori(' put on read file        ');
                           6: putfile(output);
                           7: errori(' put on prd file         ');
                           8: putfile(prr)
                      end;
           2 (*rst*): begin
                        (*for testphase*)
                        np := store[sp].va; sp := sp-1
                      end;
           3 (*rln*): begin case store[sp].va of
                                 5: begin readln(input);
                                      store[inputadr].vc := input^
                                    end;
                                 6: errori(' readln on output file   ');
                                 7: begin readln(input);
                                      store[inputadr].vc := input^
                                    end;
                                 8: errori(' readln on prr file      ')
                            end;
                            sp:= sp-1
                      end;
           4 (*new*): begin ad:= np-store[sp].va;
                      (*top of stack gives the length in units of storage *)
                            if ad <= ep then
                              errori(' store overflow          ');
                            np:= ad; ad:= store[sp-1].va;
                            store[ad].va := np;
                            sp:=sp-2
                      end;
           5 (*wln*): begin case store[sp].va of
                                 5: errori(' writeln on input file   ');
                                 6: writeln(output);
                                 7: errori(' writeln on prd file     ');
                                 8: writeln(prr)
                            end;
                            sp:= sp-1
                      end;
           6 (*wrs*): case store[sp].va of
                           5: errori(' write on input file     ');
                           6: writestr(output);
                           7: errori(' write on prd file       ');
                           8: writestr(prr)
                      end;
           7 (*eln*): begin case store[sp].va of
                                 5: line:= eoln(input);
                                 6: errori(' eoln output file        ');
                                 7: line:=eoln(prd);
                                 8: errori(' eoln on prr file        ')
                            end;
                            store[sp].vb := line
                      end;
           8 (*wri*): begin case store[sp].va of
                                 5: errori(' write on input file     ');
                                 6: write(output,
                                      store[sp-2].vi: store[sp-1].vi);
                                 7: errori(' write on prd file       ');
                                 8: write(prr,
                                      store[sp-2].vi: store[sp-1].vi)
                            end;
                            sp:=sp-3
                      end;
           9 (*wrr*): begin case store[sp].va of
                                 5: errori(' write on input file     ');
                                 6: write(output,
                                      store[sp-2].vr: store[sp-1].vi);
                                 7: errori(' write on prd file       ');
                                 8: write(prr,
                                      store[sp-2].vr: store[sp-1].vi)
                            end;
                            sp:=sp-3
                      end;
           10(*wrc*): begin case store[sp].va of
                                 5: errori(' write on input file     ');
                                 6: write(output,store[sp-2].vc:
                                      store[sp-1].vi);
                                 7: errori(' write on prd file       ');
                                 8: write(prr,chr(store[sp-2].vi):
                                      store[sp-1].vi);
                            end;
                            sp:=sp-3
                      end;
           11(*rdi*): case store[sp].va of
                           5: readi(input);
                           6: errori(' read on output file     ');
                           7: readi(prd);
                           8: errori(' read on prr file        ')
                      end;
           12(*rdr*): case store[sp].va of
                           5: readr(input);
                           6: errori(' read on output file     ');
                           7: readr(prd);
                           8: errori(' read on prr file        ')
                      end;
           13(*rdc*): case store[sp].va of
                           5: readc(input);
                           6: errori(' read on output file     ');
                           7: readc(prd);
                           8: errori(' read on prr file        ')
                      end;
           14(*sin*): store[sp].vr:= sin(store[sp].vr);
           15(*cos*): store[sp].vr:= cos(store[sp].vr);
           16(*exp*): store[sp].vr:= exp(store[sp].vr);
           17(*log*): store[sp].vr:= ln(store[sp].vr);
           18(*sqt*): store[sp].vr:= sqrt(store[sp].vr);
           19(*atn*): store[sp].vr:= arctan(store[sp].vr);
           20(*sav*): begin ad:=store[sp].va;
                         store[ad].va := np;
                         sp:= sp-1
                      end;
      end;(*case q*)
end;(*callsp*)

begin (* main *)
  (* Must comment out the next for self compile *)
  (* rewrite(prr); *)
  load; (* assembles and stores code *)
  writeln(output); (* for testing *)
  pc := 0; sp := -1; mp := 0; np := maxstk+1; ep := 5;
  store[inputadr].vc := input^;
  store[prdadr].vc := prd^;
  interpreting := true;

  while interpreting do
  begin
    (*fetch*)
    with code[pc div 2] do
      if odd(pc) then
      begin op := op2; p := p2; q := q2
      end else
      begin op := op1; p := p1; q := q1
      end;
    pc := pc+1;

    (*execute*)
(*;writeln('Execute: ', op:1, '(', instr[op], ') at: ', pc:1);*)
    case op of

          105,106,107,108,109,
          0 (*lod*): begin  ad := base(p) + q;
                      sp := sp+1;
                      store[sp] := store[ad]
                     end;

          65,66,67,68,69,
          1 (*ldo*): begin
                      sp := sp+1;
                      store[sp] := store[q]
                     end;

          70,71,72,73,74,
          2 (*str*): begin  store[base(p)+q] := store[sp];
                      sp := sp-1
                     end;

          75,76,77,78,79,
          3 (*sro*): begin  store[q] := store[sp];
                      sp := sp-1
                     end;

          4 (*lda*): begin sp := sp+1;
                      store[sp].va := base(p) + q
                     end;

          5 (*lao*): begin sp := sp+1;
                      store[sp].va := q
                     end;

          80,81,82,83,84,
          6 (*sto*): begin
                      store[store[sp-1].va] := store[sp];
                      sp := sp-2;
                     end;

          7 (*ldc*): begin sp := sp+1;
                      if p=1 then
                      begin store[sp].vi := q;
                      end else
                          if p = 6 then store[sp].vc := chr(q)
                          else
                            if p = 3 then store[sp].vb := q = 1
                            else (* load nil *) store[sp].va := maxstr
                     end;

          8 (*lci*): begin sp := sp+1;
                      store[sp] := store[q]
                     end;

          85,86,87,88,89,
          9 (*ind*): begin ad := store[sp].va + q;
                      (* q is a number of storage units *)
                      store[sp] := store[ad]
                     end;

          (* These were broken out according to type *)
          90,91,92,
          10 (*inc*): store[sp].vi := store[sp].vi+q;
          93 (*incb*): store[sp].vb := succ(store[sp].vb);
          94 (*incc*): store[sp].vc := chr(ord(store[sp].vc)+q);

          11 (*mst*): begin (*p=level of calling procedure minus level of called
                              procedure + 1;  set dl and sl, increment sp*)
                       (* then length of this element is
                          max(intsize,realsize,boolsize,charsize,ptrsize *)
                       store[sp+2].vm := base(p);
                       (* the length of this element is ptrsize *)
                       store[sp+3].vm := mp;
                       (* idem *)
                       store[sp+4].vm := ep;
                       (* idem *)
                       sp := sp+5
                      end;

          12 (*cup*): begin (*p=no of locations for parameters, q=entry point*)
                       mp := sp-(p+4);
                       store[mp+4].vm := pc;
                       pc := q
                      end;

          13 (*ent*): if p = 1 then
                        begin sp := mp + q; (*q = length of dataseg*)
                          if sp > np then errori(' store overflow          ');
                        end
                      else
                        begin ep := sp+q;
                          if ep > np then errori(' store overflow          ');
                        end;
                        (*q = max space required on stack*)

          14 (*ret*): begin case p of
                                 0: sp:= mp-1;
                                 1,2,3,4,5: sp:= mp
                            end;
                            pc := store[mp+4].vm;
                            ep := store[mp+3].vm;
                            mp:= store[mp+2].vm;
                      end;

          15 (*csp*): callsp;

          16 (*ixa*): begin
                       i := store[sp].vi;
                       sp := sp-1;
                       store[sp].va := q*i+store[sp].va;
                      end;

          17 (*equ*): begin  sp := sp-1;
                       case p of
                         1: store[sp].vb := store[sp].vi = store[sp+1].vi;
                         0: store[sp].vb := store[sp].va = store[sp+1].va;
                         6: store[sp].vb := store[sp].vc = store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr = store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb = store[sp+1].vb;
                         4: store[sp].vb := store[sp].vs = store[sp+1].vs;
                         5: begin  compare;
                               store[sp].vb := b;
                            end;
                       end; (*case p*)
                      end;

          18 (*neq*): begin  sp := sp-1;
                       case p of
                         0: store[sp].vb := store[sp].va <> store[sp+1].va;
                         1: store[sp].vb := store[sp].vi <> store[sp+1].vi;
                         6: store[sp].vb := store[sp].vc <> store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr <> store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb <> store[sp+1].vb;
                         4: store[sp].vb := store[sp].vs <> store[sp+1].vs;
                         5: begin  compare;
                               store[sp].vb := not b;
                            end
                       end; (*case p*)
                      end;

          19 (*geq*): begin  sp := sp-1;
                       case p of
                         0: errori(' <,<=,>,>= for address   ');
                         1: store[sp].vb := store[sp].vi >= store[sp+1].vi;
                         6: store[sp].vb := store[sp].vc >= store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr >= store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb >= store[sp+1].vb;
                         4: store[sp].vb := store[sp].vs >= store[sp+1].vs;
                         5: begin compare;
                              store[sp].vb := b or
                                (store[i1+i].vi >= store[i2+i].vi)
                            end
                       end; (*case p*)
                      end;

          20 (*grt*): begin  sp := sp-1;
                       case p of
                         0: errori(' <,<=,>,>= for address   ');
                         1: store[sp].vb := store[sp].vi > store[sp+1].vi;
                         6: store[sp].vb := store[sp].vc > store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr > store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb > store[sp+1].vb;
                         4: errori(' set inclusion           ');
                         5: begin  compare;
                              store[sp].vb := not b and
                                (store[i1+i].vi > store[i2+i].vi)
                            end
                       end; (*case p*)
                      end;

          21 (*leq*): begin  sp := sp-1;
                       case p of
                         0: errori(' <,<=,>,>= for address   ');
                         1: store[sp].vb := store[sp].vi <= store[sp+1].vi;
                         6: store[sp].vb := store[sp].vc <= store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr <= store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb <= store[sp+1].vb;
                         4: store[sp].vb := store[sp].vs <= store[sp+1].vs;
                         5: begin  compare;
                              store[sp].vb := b or
                                (store[i1+i].vi <= store[i2+i].vi)
                            end;
                       end; (*case p*)
                      end;

          22 (*les*): begin  sp := sp-1;
                       case p of
                         0: errori(' <,<=,>,>= for address   ');
                         1: store[sp].vb := store[sp].vi < store[sp+1].vi;
                         6: store[sp].vb := store[sp].vc < store[sp+1].vc;
                         2: store[sp].vb := store[sp].vr < store[sp+1].vr;
                         3: store[sp].vb := store[sp].vb < store[sp+1].vb;
                         5: begin  compare;
                              store[sp].vb := not b and
                                (store[i1+i].vi < store[i2+i].vi)
                            end
                       end; (*case p*)
                      end;

          23 (*ujp*): pc := q;

          24 (*fjp*): begin  if not store[sp].vb then pc := q;
                       sp := sp-1
                      end;

          25 (*xjp*): begin
                       pc := store[sp].vi + q;
                       sp := sp-1
                      end;

          95 (*chka*): if (store[sp].va < np) or
                          (store[sp].va > (maxstr-q)) then
                         errori(' bad pointer value       ');
          (* expanded these cases per type *)
          96,97,
          26 (*chk*): if (store[sp].vi < store[q-1].vi) or
                         (store[sp].vi > store[q].vi) then
                        errori(' value out of range      ');
          98 (*chkb*): if (store[sp].vb < store[q-1].vb) or
                          (store[sp].vb > store[q].vb) then
                        errori(' value out of range      ');
          99 (*chkc*): if (store[sp].vc < store[q-1].vc) or
                          (store[sp].vc > store[q].vc) then
                        errori(' value out of range      ');

          27 (*eof*): begin  i := store[sp].vi;
                       if i=inputadr then
                       begin store[sp].vb := eof(input);
                       end else errori(' code in error           ')
                      end;

          28 (*adi*): begin  sp := sp-1;
                       store[sp].vi := store[sp].vi + store[sp+1].vi
                      end;

          29 (*adr*): begin  sp := sp-1;
                       store[sp].vr := store[sp].vr + store[sp+1].vr
                      end;

          30 (*sbi*): begin sp := sp-1;
                       store[sp].vi := store[sp].vi - store[sp+1].vi
                      end;

          31 (*sbr*): begin  sp := sp-1;
                       store[sp].vr := store[sp].vr - store[sp+1].vr
                      end;

          32 (*sgs*): store[sp].vs := [store[sp].vi];

          33 (*flt*): store[sp].vr := store[sp].vi;

          34 (*flo*): store[sp-1].vr := store[sp-1].vi;

          35 (*trc*): store[sp].vi := trunc(store[sp].vr);

          36 (*ngi*): store[sp].vi := -store[sp].vi;

          37 (*ngr*): store[sp].vr := -store[sp].vr;

          38 (*sqi*): store[sp].vi := sqr(store[sp].vi);

          39 (*sqr*): store[sp].vr := sqr(store[sp].vr);

          40 (*abi*): store[sp].vi := abs(store[sp].vi);

          41 (*abr*): store[sp].vr := abs(store[sp].vr);

          42 (*not*): store[sp].vb := not store[sp].vb;

          43 (*and*): begin  sp := sp-1;
                       store[sp].vb := store[sp].vb and store[sp+1].vb
                      end;

          44 (*ior*): begin  sp := sp-1;
                       store[sp].vb := store[sp].vb or store[sp+1].vb
                      end;

          45 (*dif*): begin  sp := sp-1;
                       store[sp].vs := store[sp].vs - store[sp+1].vs
                      end;

          46 (*int*): begin  sp := sp-1;
                       store[sp].vs := store[sp].vs * store[sp+1].vs
                      end;

          47 (*uni*): begin  sp := sp-1;
                       store[sp].vs := store[sp].vs + store[sp+1].vs
                      end;

          48 (*inn*): begin
                       sp := sp - 1; i := store[sp].vi;
                       store[sp].vb := i in store[sp+1].vs;
                      end;

          49 (*mod*): begin  sp := sp-1;
                       store[sp].vi := store[sp].vi mod store[sp+1].vi
                      end;

          50 (*odd*): store[sp].vb := odd(store[sp].vi);

          51 (*mpi*): begin  sp := sp-1;
                       store[sp].vi := store[sp].vi * store[sp+1].vi
                      end;

          52 (*mpr*): begin  sp := sp-1;
                       store[sp].vr := store[sp].vr * store[sp+1].vr
                      end;

          53 (*dvi*): begin  sp := sp-1;
                       store[sp].vi := store[sp].vi div store[sp+1].vi
                      end;

          54 (*dvr*): begin  sp := sp-1;
                       store[sp].vr := store[sp].vr / store[sp+1].vr
                      end;

          55 (*mov*): begin i1 := store[sp-1].va;
                       i2 := store[sp].va; sp := sp-2;
                       for i3 := 0 to q-1 do store[i1+i3] := store[i2+i3]
                       (* q is a number of storage units *)
                      end;

          56 (*lca*): begin  sp := sp+1;
                       store[sp].va := q;
                      end;

          (* these were broken out by type [sam] *)
          100,101,102,
          57 (*dec*): store[sp].vi := store[sp].vi-q;
          103 (*decb*): store[sp].vb := pred(store[sp].vb);
          104 (*decc*): store[sp].vc := chr(ord(store[sp].vc)-q);

          58 (*stp*): interpreting := false;

          59 (*ord*): (*only used to change the tagfield*)
                      (*expanded back to take care of char and boolean cases
                        [sam]*)
                      begin
                         if p = 3 then store[sp].vi := ord(store[sp].vb)
                         else if p = 6 then store[sp].vi := ord(store[sp].vc)
                      end;

          60 (*chr*): store[sp].vc := chr(store[sp].vi); (*added chr back [sam]*)

          61 (*ujc*): errori(' case - error            ');
    end
  end; (*while interpreting*)

1 :
end.
