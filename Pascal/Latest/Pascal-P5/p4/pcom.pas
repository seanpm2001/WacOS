(*$c+,t-,d-,l-*)
 (***********************************************
  *                                             *
  *      Portable Pascal compiler               *
  *      ************************               *
  *                                             *
  *             Pascal P4                       *
  *                                             *
  *     Authors:                                *
  *           Urs Ammann                        *
  *           Kesav Nori                        *
  *           Christian Jacobi                  *
  *     Address:                                *
  *       Institut Fuer Informatik              *
  *       Eidg. Technische Hochschule           *
  *       CH-8096 Zuerich                       *
  *                                             *
  *  This code is fully documented in the book  *
  *        "Pascal Implementation"              *
  *   by Steven Pemberton and Martin Daniels    *
  * published by Ellis Horwood, Chichester, UK  *
  *         ISBN: 0-13-653-0311                 *
  *       (also available in Japanese)          *
  *                                             *
  * Steven Pemberton, CWI/AA,                   *
  * Kruislaan 413, 1098 SJ Amsterdam, NL        *
  * Steven.Pemberton@cwi.nl                     *
  *                                             *
  ***********************************************)

 (***********************************************
  *                                             *
  * Adaption comments                           *
  *                                             *
  * Scott A. Moore samiam@moorecad.com          *
  *                                             *
  * The purpose of my changes is to upgrade the *
  * code to ISO 7185, and to make the           *
  * non-portable features more generic (see     *
  * below).                                     *
  *                                             *
  * Note: you will find my comments with ISO    *
  * 7185 brackets. See my mark [sam].           *
  *                                             *
  * - I detabbed it, at 8th tabs. Not everyone  *
  * uses the same tab stops. Use spaces please. *
  *                                             *
  * - In procedure "printtables", the author    *
  *   uses "ord" to convert pointers to         *
  *   so they can be printed as part of tables. *
  *   "ord" used this way is nonstandard, but   *
  *   any such printout of pointers is bound to *
  *   be. Converted it to tagless record        *
  *   convertion, which is going to work on     *
  *   more processors than the "ord" trick      *
  *   (including mine).                         *
  *                                             *
  * - Increased the size of strglgth from 16    *
  *   to 100. This limits the size of string    *
  *   constants that can be accepted, and 16    *
  *   is just not practical.                    *
  *                                             *
  * - Eliminated the specific set of maxint.    *
  *   this means that maxint gets native        *
  *   sizing.                                   *
  *                                             *
  * - Changed the source input to "source"      *
  *                                             *
  * - Changed the size of set to 0..255.        *
  *                                             *
  * - Added ISO 7185 required header file       *
  *   declarations.                             *
  *                                             *
  * - Added "disxl" local "for" index to        *
  *   searchid, as ISO 7185 requires.           *
  *                                             *
  * - In printtables, P4 was using "ord" to     *
  *   convert pointers to integers and vice     *
  *   versa. While this is a dirty trick on any *
  *   Pascal, I converted it to untagged        *
  *   variant records, which works on most      *
  *   Pascal compilers.                         *
  *                                             *
  * - In body, cstoccmax was increased so we    *
  *   could compile bigger test programs.       *
  *                                             *
  * - In assemble, removed unused variables.    *
  *   This is not required, but nice for        *
  *   compilers that check this.                *
  *                                             *
  * - Increased the number of digits in gen2t.  *
  *                                             *
  * Other notes:                                *
  *                                             *
  * The control statement at the top of the     *
  * program should probably be removed for use  *
  * on a third party compiler. The p4 system    *
  * itself uses them, so they are useful when   *
  * self compiling, but your compiler may have  *
  * conflicting definitions.                    *
  *                                             *
  * On my compiler, the "prr" output file is    *
  * a command line parameter. You may have to   *
  * make other arrangements.                    *
  *                                             *
  * Under Unix and DOS/Windows, using IP        *
  * Pascal,the command line is:                 *
  *                                             *
  * pcom program.pas program.p4                 *
  *                                             *
  * Where "program" is the name of the program, *
  * program.pas is the Pascal source, and       *
  * program.p4 is the portable intermediate.    *
  *                                             *
  **********************************************)

program pascalcompiler(input,output,prr);

const displimit = 20; maxlevel = 10;
   intsize     =      1;
   intal       =      1;
   realsize    =      1;
   realal      =      1;
   charsize    =      1;
   charal      =      1;
   charmax     =      1;
   boolsize    =      1;
   boolal      =      1;
   ptrsize     =      1;
   adral       =      1;
   setsize     =      1;
   setal       =      1;
   stackal     =      1;
   stackelsize =      1;
   strglgth    = 100(*16*); (* This was not a very practical limit [sam] *)
   sethigh     =     255(*47*); (* changed to byte from the old CDC limit [sam] *)
   setlow      =      0;
   ordmaxchar  =     255(*63*); (* standard 8 bit ASCII limits [sam] *)
   ordminchar  =      0;
   maxint      =  2147483647(*32767*); (* Use 32 bit limit [sam] *)
   lcaftermarkstack = 5;
   fileal      = charal;
   (* stackelsize = minimum size for 1 stackelement
                  = k*stackal
      stackal     = scm(all other al-constants)
      charmax     = scm(charsize,charal)
                    scm = smallest common multiple
      lcaftermarkstack >= 4*ptrsize+max(x-size)
                        = k1*stackelsize          *)
   maxstack   =       1;
   parmal     = stackal;
   parmsize   = stackelsize;
   recal      = stackal;
   filebuffer =       4;
   maxaddr    =  maxint;



type                                                        (*describing:*)
                                                            (*************)

     marktype= ^integer;
                                                            (*basic symbols*)
                                                            (***************)

     symbol = (ident,intconst,realconst,stringconst,notsy,mulop,addop,relop,
               lparent,rparent,lbrack,rbrack,comma,semicolon,period,arrow,
               colon,becomes,labelsy,constsy,typesy,varsy,funcsy,progsy,
               procsy,setsy,packedsy,arraysy,recordsy,filesy,forwardsy,
               beginsy,ifsy,casesy,repeatsy,whilesy,forsy,withsy,
               gotosy,endsy,elsesy,untilsy,ofsy,dosy,tosy,downtosy,
               thensy,othersy);
     operator = (mul,rdiv,andop,idiv,imod,plus,minus,orop,ltop,leop,geop,gtop,
                 neop,eqop,inop,noop);
     setofsys = set of symbol;
     chtp = (letter,number,special,illegal,
             chstrquo,chcolon,chperiod,chlt,chgt,chlparen,chspace);

                                                            (*constants*)
                                                            (***********)
     setty = set of setlow..sethigh;
     cstclass = (reel,pset,strg);
     csp = ^ constant;
     constant = record case cclass: cstclass of
                         reel: (rval: packed array [1..strglgth] of char);
                         pset: (pval: setty);
                         strg: (slgth: 0..strglgth;
                                sval: packed array [1..strglgth] of char)
                       end;

     valu = record case {intval:} boolean of  (*intval never set nor tested*)
                     true:  (ival: integer);
                     false: (valp: csp)
                   end;

                                                           (*data structures*)
                                                           (*****************)
     levrange = 0..maxlevel; addrrange = 0..maxaddr;
     structform = (scalar,subrange,pointer,power,arrays,records,files,
                   tagfld,variant);
     declkind = (standard,declared);
     stp = ^ structure; ctp = ^ identifier;

     structure = { packed } record
                   marked: boolean;   (*for test phase only*)
                   size: addrrange;
                   case form: structform of
                     scalar:   (case scalkind: declkind of
                                  declared: (fconst: ctp); standard: ());
                     subrange: (rangetype: stp; min,max: valu);
                     pointer:  (eltype: stp);
                     power:    (elset: stp);
                     arrays:   (aeltype,inxtype: stp);
                     records:  (fstfld: ctp; recvar: stp);
                     files:    (filtype: stp);
                     tagfld:   (tagfieldp: ctp; fstvar: stp);
                     variant:  (nxtvar,subvar: stp; varval: valu)
                   end;

                                                            (*names*)
                                                            (*******)

     idclass = (types,konst,vars,field,proc,func);
     setofids = set of idclass;
     idkind = (actual,formal);
     alpha = packed array [1..8] of char;

     identifier = { packed } record
                   name: alpha; llink, rlink: ctp;
                   idtype: stp; next: ctp;
                   case klass: idclass of
                     types: ();
                     konst: (values: valu);
                     vars:  (vkind: idkind; vlev: levrange; vaddr: addrrange);
                     field: (fldaddr: addrrange);
                     proc, func:  (case pfdeckind: declkind of
                              standard: (key: 1..15);
                              declared: (pflev: levrange; pfname: integer;
                                          case pfkind: idkind of
                                           actual: (forwdecl, externl: boolean);
                                           formal: ()))
                   end;


     disprange = 0..displimit;
     where = (blck,crec,vrec,rec);

                                                            (*expressions*)
                                                            (*************)
     attrkind = (cst,varbl,expr);
     vaccess = (drct,indrct,inxd);

     attr = record typtr: stp;
              case kind: attrkind of
                cst:   (cval: valu);
                varbl: (case access: vaccess of
                          drct: (vlevel: levrange; dplmt: addrrange);
                          indrct: (idplmt: addrrange);
           inxd: ());
      expr: ()
              end;

     testp = ^ testpointer;
     testpointer = packed record
                     elt1,elt2 : stp;
                     lasttestp : testp
                   end;

                                                                 (*labels*)
                                                                 (********)
     lbp = ^ labl;
     labl = record nextlab: lbp; defined: boolean;
                   labval, labname: integer
            end;

     extfilep = ^filerec;
     filerec = record filename:alpha; nextfile:extfilep end;

(*-------------------------------------------------------------------------*)

var
     {prr: text;}
                                    (*returned by source program scanner
                                     insymbol:
                                     **********)

    sy: symbol;                     (*last symbol*)
    op: operator;                   (*classification of last symbol*)
    val: valu;                      (*value of last constant*)
    lgth: integer;                  (*length of last string constant*)
    id: alpha;                      (*last identifier (possibly truncated)*)
    kk: 1..8;                       (*nr of chars in last identifier*)
    ch: char;                       (*last character*)
    eol: boolean;                   (*end of line flag*)


                                    (*counters:*)
                                    (***********)

    chcnt: integer;                 (*character counter*)
    lc,ic: addrrange;               (*data location and instruction counter*)
    linecount: integer;


                                    (*switches:*)
                                    (***********)

    dp,                             (*declaration part*)
    prterr,                         (*to allow forward references in pointer type
                                      declaration by suppressing error message*)
    list,prcode,prtables: boolean;  (*output options for
                                        -- source program listing
                                        -- printing symbolic code
                                        -- displaying ident and struct tables
                                        --> procedure option*)
    debug: boolean;


                                    (*pointers:*)
                                    (***********)
    parmptr,
    intptr,realptr,charptr,
    boolptr,nilptr,textptr: stp;    (*pointers to entries of standard ids*)
    utypptr,ucstptr,uvarptr,
    ufldptr,uprcptr,ufctptr,        (*pointers to entries for undeclared ids*)
    fwptr: ctp;                     (*head of chain of forw decl type ids*)
    fextfilep: extfilep;            (*head of chain of external files*)
    globtestp: testp;               (*last testpointer*)


                                    (*bookkeeping of declaration levels:*)
                                    (************************************)

    level: levrange;                (*current static level*)
    disx,                           (*level of last id searched by searchid*)
    top: disprange;                 (*top of display*)

    display:                        (*where:   means:*)
      array [disprange] of
        packed record               (*=blck:   id is variable id*)
          fname: ctp; flabel: lbp;  (*=crec:   id is field id in record with*)
          case occur: where of      (*   constant address*)
            crec: (clev: levrange;  (*=vrec:   id is field id in record with*)
                  cdspl: addrrange);(*   variable address*)
            vrec: (vdspl: addrrange);
       blck: ();
       rec: ()
          end;                      (* --> procedure withstatement*)


                                    (*error messages:*)
                                    (*****************)

    errinx: 0..10;                  (*nr of errors in current source line*)
    errlist:
      array [1..10] of
        packed record pos: integer;
                      nmr: 1..400
               end;



                                    (*expression compilation:*)
                                    (*************************)

    gattr: attr;                    (*describes the expr currently compiled*)


                                    (*structured constants:*)
                                    (***********************)

    constbegsys,simptypebegsys,typebegsys,blockbegsys,selectsys,facbegsys,
    statbegsys,typedels: setofsys;
    chartp : array[char] of chtp;
    rw:  array [1..35(*nr. of res. words*)] of alpha;
    frw: array [1..9] of 1..36(*nr. of res. words + 1*);
    rsy: array [1..35(*nr. of res. words*)] of symbol;
    ssy: array [char] of symbol;
    rop: array [1..35(*nr. of res. words*)] of operator;
    sop: array [char] of operator;
    na:  array [1..35] of alpha;
    mn:  array [0..60] of packed array [1..4] of char;
    sna: array [1..23] of packed array [1..4] of char;
    cdx: array [0..60] of -4..+4;
    pdx: array [1..23] of -7..+7;
    ordint: array [char] of integer;

    intlabel,mxint10,digmax: integer;
(*-------------------------------------------------------------------------*)
  procedure mark(var p: marktype); begin new(p) (* shut up *) end;
  procedure release(p: marktype); begin dispose(p) (* shut up *) end;

  procedure endofline;
    var lastpos,freepos,currpos,currnmr,f,k: integer;
  begin
    if errinx > 0 then   (*output error messages*)
      begin write(output,linecount:6,' ****  ':9);
        lastpos := 0; freepos := 1;
        for k := 1 to errinx do
          begin
            with errlist[k] do
              begin currpos := pos; currnmr := nmr end;
            if currpos = lastpos then write(output,',')
            else
              begin
                while freepos < currpos do
                  begin write(output,' '); freepos := freepos + 1 end;
                write(output,'^');
                lastpos := currpos
              end;
            if currnmr < 10 then f := 1
            else if currnmr < 100 then f := 2
              else f := 3;
            write(output,currnmr:f);
            freepos := freepos + f + 1
          end;
        writeln(output); errinx := 0
      end;
    linecount := linecount + 1;
    if list and (not eof(input)) then
      begin write(output,linecount:6,'  ':2);
        if dp then write(output,lc:7) else write(output,ic:7);
        write(output,' ')
      end;
    chcnt := 0
  end  (*endofline*) ;

  procedure error(ferrnr: integer);
  begin
    if errinx >= 9 then
      begin errlist[10].nmr := 255; errinx := 10 end
    else
      begin errinx := errinx + 1;
        errlist[errinx].nmr := ferrnr
      end;
    errlist[errinx].pos := chcnt
  end (*error*) ;

  procedure insymbol;
    (*read next basic symbol of source program and return its
    description in the global variables sy, op, id, val and lgth*)
    label 1,2(*,3*);
    var i,k: integer;
        digit: packed array [1..strglgth] of char;
        string: packed array [1..strglgth] of char;
        lvp: csp; test: boolean;

    procedure nextch;
    begin if eol then
      begin if list then writeln(output); endofline
      end;
      if not eof(input) then
       begin eol := eoln(input); read(input,ch);
        if list then write(output,ch);
        chcnt := chcnt + 1
       end
      else
        begin writeln(output,'   *** eof ','encountered');
          test := false
        end
    end;

    procedure options;
    begin
      repeat nextch;
        if ch <> '*' then
          begin
            if ch = 't' then
              begin nextch; prtables := ch = '+' end
            else
              if ch = 'l' then
                begin nextch; list := ch = '+';
                  if not list then writeln(output)
                end
              else
             if ch = 'd' then
               begin nextch; debug := ch = '+' end
             else
                if ch = 'c' then
                  begin nextch; prcode := ch = '+' end;
            nextch
          end
      until ch <> ','
    end (*options*) ;

  begin (*insymbol*)
  1:
    repeat while (ch = ' ') and not eol do nextch;
      test := eol;
      if test then nextch
    until not test;
    if chartp[ch] = illegal then
      begin sy := othersy; op := noop;
        error(399); nextch
      end
    else
    case chartp[ch] of
      letter:
        begin k := 0;
          repeat
            if k < 8 then
             begin k := k + 1; id[k] := ch end ;
            nextch
          until chartp[ch] in [special,illegal,chstrquo,chcolon,
                                chperiod,chlt,chgt,chlparen,chspace];
          if k >= kk then kk := k
          else
            repeat id[kk] := ' '; kk := kk - 1
            until kk = k;
          for i := frw[k] to frw[k+1] - 1 do
            if rw[i] = id then
              begin sy := rsy[i]; op := rop[i]; goto 2 end;
            sy := ident; op := noop;
  2:    end;
      number:
        begin op := noop; i := 0;
          repeat i := i+1; if i<= digmax then digit[i] := ch; nextch
          until chartp[ch] <> number;
          if ((ch = '.') and (input^ <> '.')) or (ch = 'e') then
            begin
                  k := i;
                  if ch = '.' then
                    begin k := k+1; if k <= digmax then digit[k] := ch;
                      nextch; (*if ch = '.' then begin ch := ':'; goto 3 end;*)
                      if chartp[ch] <> number then error(201)
                      else
                        repeat k := k + 1;
                          if k <= digmax then digit[k] := ch; nextch
                        until chartp[ch] <>  number
                    end;
                  if ch = 'e' then
                    begin k := k+1; if k <= digmax then digit[k] := ch;
                      nextch;
                      if (ch = '+') or (ch ='-') then
                        begin k := k+1; if k <= digmax then digit[k] := ch;
                          nextch
                        end;
                      if chartp[ch] <> number then error(201)
                      else
                        repeat k := k+1;
                          if k <= digmax then digit[k] := ch; nextch
                        until chartp[ch] <> number
                     end;
                   new(lvp,reel); sy:= realconst; lvp^.cclass := reel;
                   with lvp^ do
                     begin for i := 1 to strglgth do rval[i] := ' ';
                       if k <= digmax then
                         for i := 2 to k + 1 do rval[i] := digit[i-1]
                       else begin error(203); rval[2] := '0';
                              rval[3] := '.'; rval[4] := '0'
                            end
                     end;
                   val.valp := lvp
            end
          else
  (* 3: *)        begin
              if i > digmax then begin error(203); val.ival := 0 end
              else
                with val do
                  begin ival := 0;
                    for k := 1 to i do
                      begin
                        if ival <= mxint10 then
                          ival := ival*10+ordint[digit[k]]
                        else begin error(203); ival := 0 end
                      end;
                    sy := intconst
                  end
            end
        end;
      chstrquo:
        begin lgth := 0; sy := stringconst;  op := noop;
          repeat
            repeat nextch; lgth := lgth + 1;
                   if lgth <= strglgth then string[lgth] := ch
            until (eol) or (ch = '''');
            if eol then error(202) else nextch
          until ch <> '''';
          lgth := lgth - 1;   (*now lgth = nr of chars in string*)
          if lgth = 0 then error(205) else
          if lgth = 1 then val.ival := ord(string[1])
          else
            begin new(lvp,strg); lvp^.cclass:=strg;
              if lgth > strglgth then
                begin error(399); lgth := strglgth end;
              with lvp^ do
                begin slgth := lgth;
                  for i := 1 to lgth do sval[i] := string[i]
                end;
              val.valp := lvp
            end
        end;
      chcolon:
        begin op := noop; nextch;
          if ch = '=' then
            begin sy := becomes; nextch end
          else sy := colon
        end;
      chperiod:
        begin op := noop; nextch;
          if ch = '.' then
            begin sy := colon; nextch end
          else sy := period
        end;
      chlt:
        begin nextch; sy := relop;
          if ch = '=' then
            begin op := leop; nextch end
          else
            if ch = '>' then
              begin op := neop; nextch end
            else op := ltop
        end;
      chgt:
        begin nextch; sy := relop;
          if ch = '=' then
            begin op := geop; nextch end
          else op := gtop
        end;
      chlparen:
       begin nextch;
         if ch = '*' then
           begin nextch;
             if ch = '$' then options;
             repeat
               while (ch <> '*') and not eof(input) do nextch;
               nextch
             until (ch = ')') or eof(input);
             nextch; goto 1
           end;
         sy := lparent; op := noop
       end;
      special:
        begin sy := ssy[ch]; op := sop[ch];
          nextch
        end;
      chspace: sy := othersy
    end (*case*)
  end (*insymbol*) ;

  procedure enterid(fcp: ctp);
    (*enter id pointed at by fcp into the name-table,
     which on each declaration level is organised as
     an unbalanced binary tree*)
    var nam: alpha; lcp, lcp1: ctp; lleft: boolean;
  begin nam := fcp^.name;
    lcp := display[top].fname;
    if lcp = nil then
      display[top].fname := fcp
    else
      begin
        repeat lcp1 := lcp;
          if lcp^.name = nam then   (*name conflict, follow right link*)
            begin error(101); lcp := lcp^.rlink; lleft := false end
          else
            if lcp^.name < nam then
              begin lcp := lcp^.rlink; lleft := false end
            else begin lcp := lcp^.llink; lleft := true end
        until lcp = nil;
        if lleft then lcp1^.llink := fcp else lcp1^.rlink := fcp
      end;
    fcp^.llink := nil; fcp^.rlink := nil
  end (*enterid*) ;

  procedure searchsection(fcp: ctp; var fcp1: ctp);
    (*to find record fields and forward declared procedure id's
     --> procedure proceduredeclaration
     --> procedure selector*)
     label 1;
  begin
    while fcp <> nil do
      if fcp^.name = id then goto 1
      else if fcp^.name < id then fcp := fcp^.rlink
        else fcp := fcp^.llink;
1:  fcp1 := fcp
  end (*searchsection*) ;

  (* Added to search id, disxl is now used for a local "for" index,
    which matches ISO 7185. Also, depending on the index keeping
    its contents after the containing statement is a violation,
    so the behavior of setting disx to last search id was
    emulated [sam] *)
  procedure searchid(fidcls: setofids; var fcp: ctp);
    label 1;
    var lcp: ctp;
        disxl: disprange;
  begin
    for disxl := top downto 0 do
      begin lcp := display[disxl].fname;
        while lcp <> nil do
          if lcp^.name = id then
            if lcp^.klass in fidcls then begin disx := disxl; goto 1 end
            else
              begin if prterr then error(103);
                lcp := lcp^.rlink
              end
          else
            if lcp^.name < id then
              lcp := lcp^.rlink
            else lcp := lcp^.llink
      end;
      disx := 0;
    (*search not successful; suppress error message in case
     of forward referenced type id in pointer type definition
     --> procedure simpletype*)
    if prterr then
      begin error(104);
        (*to avoid returning nil, reference an entry
         for an undeclared id of appropriate class
         --> procedure enterundecl*)
        if types in fidcls then lcp := utypptr
        else
          if vars in fidcls then lcp := uvarptr
          else
            if field in fidcls then lcp := ufldptr
            else
              if konst in fidcls then lcp := ucstptr
              else
                if proc in fidcls then lcp := uprcptr
                else lcp := ufctptr;
      end;
1:  fcp := lcp
  end (*searchid*) ;

  procedure getbounds(fsp: stp; var fmin,fmax: integer);
    (*get internal bounds of subrange or scalar type*)
    (*assume fsp<>intptr and fsp<>realptr*)
  begin
    fmin := 0; fmax := 0;
    if fsp <> nil then
    with fsp^ do
      if form = subrange then
        begin fmin := min.ival; fmax := max.ival end
      else
          if fsp = charptr then
            begin fmin := ordminchar; fmax := ordmaxchar
            end
          else
            if fconst <> nil then
              fmax := fconst^.values.ival
  end (*getbounds*) ;

  function alignquot(fsp: stp): integer;
  begin
    alignquot := 1;
    if fsp <> nil then
      with fsp^ do
        case form of
          scalar:   if fsp=intptr then alignquot := intal
                    else if fsp=boolptr then alignquot := boolal
                    else if scalkind=declared then alignquot := intal
                    else if fsp=charptr then alignquot := charal
                    else if fsp=realptr then alignquot := realal
                    else (*parmptr*) alignquot := parmal;
          subrange: alignquot := alignquot(rangetype);
          pointer:  alignquot := adral;
          power:    alignquot := setal;
          files:    alignquot := fileal;
          arrays:   alignquot := alignquot(aeltype);
          records:  alignquot := recal;
          variant,tagfld: error(501)
        end
  end (*alignquot*);

  procedure align(fsp: stp; var flc: addrrange);
    var k,l: integer;
  begin
    k := alignquot(fsp);
    l := flc-1;
    flc := l + k  -  (k+l) mod k
  end (*align*);

  procedure printtables(fb: boolean);
    (*print data structure and name table*)
    (* Added these functions to convert pointers to integers.
      Works on any machine where pointers and integers are the same format.
      The original code was for a processor where "ord" would do this, a
      very nonstandard feature [sam] *)
    const intsize = 11; (* size of printed integer *)

    var i, lim: disprange;

    function stptoint(p: stp): integer;
    var r: record case boolean of false: (p: stp); true: (i: integer) end;
    begin r.p := p; stptoint := r.i end;

    function ctptoint(p: ctp): integer;
    var r: record case boolean of false: (p: ctp); true: (i: integer) end;
    begin r.p := p; ctptoint := r.i end;

    procedure marker;
      (*mark data structure entries to avoid multiple printout*)
      var i: integer;

      procedure markctp(fp: ctp); forward;

      procedure markstp(fp: stp);
        (*mark data structures, prevent cycles*)
      begin
        if fp <> nil then
          with fp^ do
            begin marked := true;
              case form of
              scalar:   ;
              subrange: markstp(rangetype);
              pointer:  (*don't mark eltype: cycle possible; will be marked
                        anyway, if fp = true*) ;
              power:    markstp(elset) ;
              arrays:   begin markstp(aeltype); markstp(inxtype) end;
              records:  begin markctp(fstfld); markstp(recvar) end;
              files:    markstp(filtype);
              tagfld:   markstp(fstvar);
              variant:  begin markstp(nxtvar); markstp(subvar) end
              end (*case*)
            end (*with*)
      end (*markstp*);

      procedure markctp;
      begin
        if fp <> nil then
          with fp^ do
            begin markctp(llink); markctp(rlink);
              markstp(idtype)
            end
      end (*markctp*);

    begin (*marker*)
      for i := top downto lim do
        markctp(display[i].fname)
    end (*marker*);

    procedure followctp(fp: ctp); forward;

    procedure followstp(fp: stp);
    begin
      if fp <> nil then
        with fp^ do
          if marked then
            begin marked := false; write(output,' ':4,stptoint(*ord*)(fp):intsize(*6*),size:10);
              case form of
              scalar:   begin write(output,'scalar':10);
                          if scalkind = standard then
                            write(output,'standard':10)
                          else write(output,'declared':10,' ':4,ctptoint(*ord*)(fconst):intsize(*6*));
                          writeln(output)
                        end;
              subrange: begin
                          write(output,'subrange':10,' ':4,stptoint(*ord*)(rangetype):6);
                          if rangetype <> realptr then
                            write(output,min.ival,max.ival)
                          else
                            if (min.valp <> nil) and (max.valp <> nil) then
                              write(output,' ',min.valp^.rval:9,
                                    ' ',max.valp^.rval:9);
                          writeln(output); followstp(rangetype);
                        end;
              pointer:  writeln(output,'pointer':10,' ':4,stptoint(*ord*)(eltype):intsize(*6*));
              power:    begin writeln(output,'set':10,' ':4,stptoint(*ord*)(elset):intsize(*6*));
                          followstp(elset)
                        end;
              arrays:   begin
                          writeln(output,'array':10,' ':4,stptoint(*ord*)(aeltype):intsize(*6*),' ':4,
                            stptoint(*ord*)(inxtype):6);
                          followstp(aeltype); followstp(inxtype)
                        end;
              records:  begin
                          writeln(output,'record':10,' ':4,ctptoint(*ord*)(fstfld):intsize(*6*),' ':4,
                            stptoint(*ord*)(recvar):intsize(*6*)); followctp(fstfld);
                          followstp(recvar)
                        end;
              files:    begin write(output,'file':10,' ':4,stptoint(*ord*)(filtype):intsize(*6*));
                          followstp(filtype)
                        end;
              tagfld:   begin writeln(output,'tagfld':10,' ':4,ctptoint(*ord*)(tagfieldp):intsize(*6*),
                            ' ':4,stptoint(*ord*)(fstvar):intsize(*6*));
                          followstp(fstvar)
                        end;
              variant:  begin writeln(output,'variant':10,' ':4,stptoint(*ord*)(nxtvar):intsize(*6*),
                            ' ':4,stptoint(*ord*)(subvar):intsize(*6*),varval.ival);
                          followstp(nxtvar); followstp(subvar)
                        end
              end (*case*)
            end (*if marked*)
    end (*followstp*);

    procedure followctp;
      var i: integer;
    begin
      if fp <> nil then
        with fp^ do
          begin write(output,' ':4,ctptoint(*ord*)(fp):intsize(*6*),' ',name:9,' ':4,ctptoint(*ord*)(llink):intsize(*6*),
            ' ':4,ctptoint(*ord*)(rlink):intsize(*6*),' ':4,stptoint(*ord*)(idtype):intsize(*6*));
            case klass of
              types: write(output,'type':10);
              konst: begin write(output,'constant':10,' ':4,ctptoint(*ord*)(next):intsize(*6*));
                       if idtype <> nil then
                         if idtype = realptr then
                           begin
                             if values.valp <> nil then
                               write(output,' ',values.valp^.rval:9)
                           end
                         else
                           if idtype^.form = arrays then  (*stringconst*)
                             begin
                               if values.valp <> nil then
                                 begin write(output,' ');
                                   with values.valp^ do
                                     for i := 1 to slgth do
                                       write(output,sval[i])
                                 end
                             end
                           else write(output,values.ival)
                     end;
              vars:  begin write(output,'variable':10);
                       if vkind = actual then write(output,'actual':10)
                       else write(output,'formal':10);
                       write(output,' ':4,ctptoint(*ord*)(next):intsize(*6*),vlev,' ':4,vaddr:6 );
                     end;
              field: write(output,'field':10,' ':4,ctptoint(*ord*)(next):intsize(*6*),' ':4,fldaddr:6);
              proc,
              func:  begin
                       if klass = proc then write(output,'procedure':10)
                       else write(output,'function':10);
                       if pfdeckind = standard then
                         write(output,'standard':10, key:10)
                       else
                         begin write(output,'declared':10,' ':4,ctptoint(*ord*)(next):intsize(*6*));
                           write(output,pflev,' ':4,pfname:6);
                           if pfkind = actual then
                             begin write(output,'actual':10);
                               if forwdecl then write(output,'forward':10)
                               else write(output,'notforward':10);
                               if externl then write(output,'extern':10)
                               else write(output,'not extern':10);
                             end
                           else write(output,'formal':10)
                         end
                     end
            end (*case*);
            writeln(output);
            followctp(llink); followctp(rlink);
            followstp(idtype)
          end (*with*)
    end (*followctp*);

  begin (*printtables*)
    writeln(output); writeln(output); writeln(output);
    if fb then lim := 0
    else begin lim := top; write(output,' local') end;
    writeln(output,' tables '); writeln(output);
    marker;
    for i := top downto lim do
      followctp(display[i].fname);
    writeln(output);
    if not eol then write(output,' ':chcnt+16)
  end (*printtables*);

  procedure genlabel(var nxtlab: integer);
  begin intlabel := intlabel + 1;
    nxtlab := intlabel
  end (*genlabel*);

  procedure block(fsys: setofsys; fsy: symbol; fprocp: ctp);
    var lsy: symbol; test: boolean;

    procedure skip(fsys: setofsys);
      (*skip input string until relevant symbol found*)
    begin
      if not eof(input) then
        begin while not(sy in fsys) and (not eof(input)) do insymbol;
          if not (sy in fsys) then insymbol
        end
    end (*skip*) ;

    procedure constant(fsys: setofsys; var fsp: stp; var fvalu: valu);
      var lsp: stp; lcp: ctp; sign: (none,pos,neg);
          lvp: csp; i: 2..strglgth;
    begin lsp := nil; fvalu.ival := 0;
      if not(sy in constbegsys) then
        begin error(50); skip(fsys+constbegsys) end;
      if sy in constbegsys then
        begin
          if sy = stringconst then
            begin
              if lgth = 1 then lsp := charptr
              else
                begin
                  new(lsp,arrays);
                  with lsp^ do
                    begin aeltype := charptr; inxtype := nil;
                       size := lgth*charsize; form := arrays
                    end
                end;
              fvalu := val; insymbol
            end
          else
            begin
              sign := none;
              if (sy = addop) and (op in [plus,minus]) then
                begin if op = plus then sign := pos else sign := neg;
                  insymbol
                end;
              if sy = ident then
                begin searchid([konst],lcp);
                  with lcp^ do
                    begin lsp := idtype; fvalu := values end;
                  if sign <> none then
                    if lsp = intptr then
                      begin if sign = neg then fvalu.ival := -fvalu.ival end
                    else
                      if lsp = realptr then
                        begin
                          if sign = neg then
                            begin new(lvp,reel);
                              if fvalu.valp^.rval[1] = '-' then
                                lvp^.rval[1] := '+'
                              else lvp^.rval[1] := '-';
                              for i := 2 to strglgth do
                                lvp^.rval[i] := fvalu.valp^.rval[i];
                              fvalu.valp := lvp;
                            end
                          end
                        else error(105);
                  insymbol;
                end
              else
                if sy = intconst then
                  begin if sign = neg then val.ival := -val.ival;
                    lsp := intptr; fvalu := val; insymbol
                  end
                else
                  if sy = realconst then
                    begin if sign = neg then val.valp^.rval[1] := '-';
                      lsp := realptr; fvalu := val; insymbol
                    end
                  else
                    begin error(106); skip(fsys) end
            end;
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
          end;
      fsp := lsp
    end (*constant*) ;

    function equalbounds(fsp1,fsp2: stp): boolean;
      var lmin1,lmin2,lmax1,lmax2: integer;
    begin
      if (fsp1=nil) or (fsp2=nil) then equalbounds := true
      else
        begin
          getbounds(fsp1,lmin1,lmax1);
          getbounds(fsp2,lmin2,lmax2);
          equalbounds := (lmin1=lmin2) and (lmax1=lmax2)
        end
    end (*equalbounds*) ;

    function comptypes(fsp1,fsp2: stp) : boolean;
      (*decide whether structures pointed at by fsp1 and fsp2 are compatible*)
      var nxt1,nxt2: ctp; comp: boolean;
        ltestp1,ltestp2 : testp;
    begin
      if fsp1 = fsp2 then comptypes := true
      else
        if (fsp1 <> nil) and (fsp2 <> nil) then
          if fsp1^.form = fsp2^.form then
            case fsp1^.form of
              scalar:
                comptypes := false;
                (* identical scalars declared on different levels are
                 not recognized to be compatible*)
              subrange:
                comptypes := comptypes(fsp1^.rangetype,fsp2^.rangetype);
              pointer:
                  begin
                    comp := false; ltestp1 := globtestp;
                    ltestp2 := globtestp;
                    while ltestp1 <> nil do
                      with ltestp1^ do
                        begin
                          if (elt1 = fsp1^.eltype) and
                             (elt2 = fsp2^.eltype) then comp := true;
                          ltestp1 := lasttestp
                        end;
                    if not comp then
                      begin new(ltestp1);
                        with ltestp1^ do
                          begin elt1 := fsp1^.eltype;
                            elt2 := fsp2^.eltype;
                            lasttestp := globtestp
                          end;
                        globtestp := ltestp1;
                        comp := comptypes(fsp1^.eltype,fsp2^.eltype)
                      end;
                    comptypes := comp; globtestp := ltestp2
                  end;
              power:
                comptypes := comptypes(fsp1^.elset,fsp2^.elset);
              arrays:
                begin
                  comp := comptypes(fsp1^.aeltype,fsp2^.aeltype)
                      and comptypes(fsp1^.inxtype,fsp2^.inxtype);
                  comptypes := comp and (fsp1^.size = fsp2^.size) and
                      equalbounds(fsp1^.inxtype,fsp2^.inxtype)
                end;
              records:
                begin nxt1 := fsp1^.fstfld; nxt2 := fsp2^.fstfld; comp:=true;
                  while (nxt1 <> nil) and (nxt2 <> nil) do
                    begin comp:=comp and comptypes(nxt1^.idtype,nxt2^.idtype);
                      nxt1 := nxt1^.next; nxt2 := nxt2^.next
                    end;
                  comptypes := comp and (nxt1 = nil) and (nxt2 = nil)
                              and(fsp1^.recvar = nil)and(fsp2^.recvar = nil)
                end;
                (*identical records are recognized to be compatible
                 iff no variants occur*)
              files:
                comptypes := comptypes(fsp1^.filtype,fsp2^.filtype)
            end (*case*)
          else (*fsp1^.form <> fsp2^.form*)
            if fsp1^.form = subrange then
              comptypes := comptypes(fsp1^.rangetype,fsp2)
            else
              if fsp2^.form = subrange then
                comptypes := comptypes(fsp1,fsp2^.rangetype)
              else comptypes := false
        else comptypes := true
    end (*comptypes*) ;

    function string(fsp: stp) : boolean;
    begin string := false;
      if fsp <> nil then
        if fsp^.form = arrays then
          if comptypes(fsp^.aeltype,charptr) then string := true
    end (*string*) ;

    procedure typ(fsys: setofsys; var fsp: stp; var fsize: addrrange);
      var lsp,lsp1,lsp2: stp; oldtop: disprange; lcp: ctp;
          lsize,displ: addrrange; lmin,lmax: integer;

      procedure simpletype(fsys:setofsys; var fsp:stp; var fsize:addrrange);
        var lsp,lsp1: stp; lcp,lcp1: ctp; ttop: disprange;
            lcnt: integer; lvalu: valu;
      begin fsize := 1;
        if not (sy in simptypebegsys) then
          begin error(1); skip(fsys + simptypebegsys) end;
        if sy in simptypebegsys then
          begin
            if sy = lparent then
              begin ttop := top;   (*decl. consts local to innermost block*)
                while display[top].occur <> blck do top := top - 1;
                new(lsp,scalar,declared);
                with lsp^ do
                  begin size := intsize; form := scalar;
                    scalkind := declared
                  end;
                lcp1 := nil; lcnt := 0;
                repeat insymbol;
                  if sy = ident then
                    begin new(lcp,konst);
                      with lcp^ do
                        begin name := id; idtype := lsp; next := lcp1;
                          values.ival := lcnt; klass := konst
                        end;
                      enterid(lcp);
                      lcnt := lcnt + 1;
                      lcp1 := lcp; insymbol
                    end
                  else error(2);
                  if not (sy in fsys + [comma,rparent]) then
                    begin error(6); skip(fsys + [comma,rparent]) end
                until sy <> comma;
                lsp^.fconst := lcp1; top := ttop;
                if sy = rparent then insymbol else error(4)
              end
            else
              begin
                if sy = ident then
                  begin searchid([types,konst],lcp);
                    insymbol;
                    if lcp^.klass = konst then
                      begin new(lsp,subrange);
                        with lsp^, lcp^ do
                          begin rangetype := idtype; form := subrange;
                            if string(rangetype) then
                              begin error(148); rangetype := nil end;
                            min := values; size := intsize
                          end;
                        if sy = colon then insymbol else error(5);
                        constant(fsys,lsp1,lvalu);
                        lsp^.max := lvalu;
                        if lsp^.rangetype <> lsp1 then error(107)
                      end
                    else
                      begin lsp := lcp^.idtype;
                        if lsp <> nil then fsize := lsp^.size
                      end
                  end (*sy = ident*)
                else
                  begin new(lsp,subrange); lsp^.form := subrange;
                    constant(fsys + [colon],lsp1,lvalu);
                    if string(lsp1) then
                      begin error(148); lsp1 := nil end;
                    with lsp^ do
                      begin rangetype:=lsp1; min:=lvalu; size:=intsize end;
                    if sy = colon then insymbol else error(5);
                    constant(fsys,lsp1,lvalu);
                    lsp^.max := lvalu;
                    if lsp^.rangetype <> lsp1 then error(107)
                  end;
                if lsp <> nil then
                  with lsp^ do
                    if form = subrange then
                      if rangetype <> nil then
                        if rangetype = realptr then error(399)
                        else
                          if min.ival > max.ival then error(102)
              end;
            fsp := lsp;
            if not (sy in fsys) then
              begin error(6); skip(fsys) end
          end
            else fsp := nil
      end (*simpletype*) ;

      procedure fieldlist(fsys: setofsys; var frecvar: stp);
        var lcp,lcp1,nxt,nxt1: ctp; lsp,lsp1,lsp2,lsp3,lsp4: stp;
            minsize,maxsize,lsize: addrrange; lvalu: valu;
      begin nxt1 := nil; lsp := nil;
        if not (sy in (fsys+[ident,casesy])) then
          begin error(19); skip(fsys + [ident,casesy]) end;
        while sy = ident do
          begin nxt := nxt1;
            repeat
              if sy = ident then
                begin new(lcp,field);
                  with lcp^ do
                    begin name := id; idtype := nil; next := nxt;
                      klass := field
                    end;
                  nxt := lcp;
                  enterid(lcp);
                  insymbol
                end
              else error(2);
              if not (sy in [comma,colon]) then
                begin error(6); skip(fsys + [comma,colon,semicolon,casesy])
                end;
              test := sy <> comma;
              if not test  then insymbol
            until test;
            if sy = colon then insymbol else error(5);
            typ(fsys + [casesy,semicolon],lsp,lsize);
            while nxt <> nxt1 do
              with nxt^ do
                begin align(lsp,displ);
                  idtype := lsp; fldaddr := displ;
                  nxt := next; displ := displ + lsize
                end;
            nxt1 := lcp;
            while sy = semicolon do
              begin insymbol;
                if not (sy in fsys + [ident,casesy,semicolon]) then
                  begin error(19); skip(fsys + [ident,casesy]) end
              end
          end (*while*);
        nxt := nil;
        while nxt1 <> nil do
          with nxt1^ do
            begin lcp := next; next := nxt; nxt := nxt1; nxt1 := lcp end;
        if sy = casesy then
          begin new(lsp,tagfld);
            with lsp^ do
              begin tagfieldp := nil; fstvar := nil; form:=tagfld end;
            frecvar := lsp;
            insymbol;
            if sy = ident then
              begin new(lcp,field);
                with lcp^ do
                  begin name := id; idtype := nil; klass:=field;
                    next := nil; fldaddr := displ
                  end;
                enterid(lcp);
                insymbol;
                if sy = colon then insymbol else error(5);
                if sy = ident then
                  begin searchid([types],lcp1);
                    lsp1 := lcp1^.idtype;
                    if lsp1 <> nil then
                      begin align(lsp1,displ);
                        lcp^.fldaddr := displ;
                        displ := displ+lsp1^.size;
                        if (lsp1^.form <= subrange) or string(lsp1) then
                          begin if comptypes(realptr,lsp1) then error(109)
                            else if string(lsp1) then error(399);
                            lcp^.idtype := lsp1; lsp^.tagfieldp := lcp;
                          end
                        else error(110);
                      end;
                    insymbol;
                  end
                else begin error(2); skip(fsys + [ofsy,lparent]) end
              end
            else begin error(2); skip(fsys + [ofsy,lparent]) end;
            lsp^.size := displ;
            if sy = ofsy then insymbol else error(8);
            lsp1 := nil; minsize := displ; maxsize := displ;
            repeat lsp2 := nil;
              if not (sy in fsys + [semicolon]) then
              begin
                repeat constant(fsys + [comma,colon,lparent],lsp3,lvalu);
                  if lsp^.tagfieldp <> nil then
                   if not comptypes(lsp^.tagfieldp^.idtype,lsp3)then error(111);
                  new(lsp3,variant);
                  with lsp3^ do
                    begin nxtvar := lsp1; subvar := lsp2; varval := lvalu;
                      form := variant
                    end;
                  lsp4 := lsp1;
                  while lsp4 <> nil do
                    with lsp4^ do
                      begin
                        if varval.ival = lvalu.ival then error(178);
                        lsp4 := nxtvar
                      end;
                  lsp1 := lsp3; lsp2 := lsp3;
                  test := sy <> comma;
                  if not test then insymbol
                until test;
                if sy = colon then insymbol else error(5);
                if sy = lparent then insymbol else error(9);
                fieldlist(fsys + [rparent,semicolon],lsp2);
                if displ > maxsize then maxsize := displ;
                while lsp3 <> nil do
                  begin lsp4 := lsp3^.subvar; lsp3^.subvar := lsp2;
                    lsp3^.size := displ;
                    lsp3 := lsp4
                  end;
                if sy = rparent then
                  begin insymbol;
                    if not (sy in fsys + [semicolon]) then
                      begin error(6); skip(fsys + [semicolon]) end
                  end
                else error(4);
              end;
              test := sy <> semicolon;
              if not test then
                begin displ := minsize;
                      insymbol
                end
            until test;
            displ := maxsize;
            lsp^.fstvar := lsp1;
          end
        else frecvar := nil
      end (*fieldlist*) ;

    begin (*typ*)
      if not (sy in typebegsys) then
         begin error(10); skip(fsys + typebegsys) end;
      if sy in typebegsys then
        begin
          if sy in simptypebegsys then simpletype(fsys,fsp,fsize)
          else
    (*^*)     if sy = arrow then
              begin new(lsp,pointer); fsp := lsp;
                with lsp^ do
                  begin eltype := nil; size := ptrsize; form:=pointer end;
                insymbol;
                if sy = ident then
                  begin prterr := false; (*no error if search not successful*)
                    searchid([types],lcp); prterr := true;
                    if lcp = nil then   (*forward referenced type id*)
                      begin new(lcp,types);
                        with lcp^ do
                          begin name := id; idtype := lsp;
                            next := fwptr; klass := types
                          end;
                        fwptr := lcp
                      end
                    else
                      begin
                        if lcp^.idtype <> nil then
                          if lcp^.idtype^.form = files then error(108)
                          else lsp^.eltype := lcp^.idtype
                      end;
                    insymbol;
                  end
                else error(2);
              end
            else
              begin
                if sy = packedsy then
                  begin insymbol;
                    if not (sy in typedels) then
                      begin
                        error(10); skip(fsys + typedels)
                      end
                  end;
    (*array*)     if sy = arraysy then
                  begin insymbol;
                    if sy = lbrack then insymbol else error(11);
                    lsp1 := nil;
                    repeat new(lsp,arrays);
                      with lsp^ do
                        begin aeltype := lsp1; inxtype := nil; form:=arrays end;
                      lsp1 := lsp;
                      simpletype(fsys + [comma,rbrack,ofsy],lsp2,lsize);
                      lsp1^.size := lsize;
                      if lsp2 <> nil then
                        if lsp2^.form <= subrange then
                          begin
                            if lsp2 = realptr then
                              begin error(109); lsp2 := nil end
                            else
                              if lsp2 = intptr then
                                begin error(149); lsp2 := nil end;
                            lsp^.inxtype := lsp2
                          end
                        else begin error(113); lsp2 := nil end;
                      test := sy <> comma;
                      if not test then insymbol
                    until test;
                    if sy = rbrack then insymbol else error(12);
                    if sy = ofsy then insymbol else error(8);
                    typ(fsys,lsp,lsize);
                    repeat
                      with lsp1^ do
                        begin lsp2 := aeltype; aeltype := lsp;
                          if inxtype <> nil then
                            begin getbounds(inxtype,lmin,lmax);
                              align(lsp,lsize);
                              lsize := lsize*(lmax - lmin + 1);
                              size := lsize
                            end
                        end;
                      lsp := lsp1; lsp1 := lsp2
                    until lsp1 = nil
                  end
                else
    (*record*)      if sy = recordsy then
                    begin insymbol;
                      oldtop := top;
                      if top < displimit then
                        begin top := top + 1;
                          with display[top] do
                            begin fname := nil;
                                  flabel := nil;
                                  occur := rec
                            end
                        end
                      else error(250);
                      displ := 0;
                      fieldlist(fsys-[semicolon]+[endsy],lsp1);
                      new(lsp,records);
                      with lsp^ do
                        begin fstfld := display[top].fname;
                          recvar := lsp1; size := displ; form := records
                        end;
                      top := oldtop;
                      if sy = endsy then insymbol else error(13)
                    end
                  else
    (*set*)        if sy = setsy then
                      begin insymbol;
                        if sy = ofsy then insymbol else error(8);
                        simpletype(fsys,lsp1,lsize);
                        if lsp1 <> nil then
                          if lsp1^.form > subrange then
                            begin error(115); lsp1 := nil end
                          else
                            if lsp1 = realptr then
                              begin error(114); lsp1 := nil end
                            else if lsp1 = intptr then
                              begin error(169); lsp1 := nil end
                            else
                              begin getbounds(lsp1,lmin,lmax);
                                if (lmin < setlow) or (lmax > sethigh)
                                  then error(169);
                              end;
                        new(lsp,power);
                        with lsp^ do
                          begin elset:=lsp1; size:=setsize; form:=power end;
                      end
                    else
    (*file*)        if sy = filesy then
                          begin insymbol;
                            error(399); skip(fsys); lsp := nil
                          end;
                fsp := lsp
              end;
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
        end
      else fsp := nil;
      if fsp = nil then fsize := 1 else fsize := fsp^.size
    end (*typ*) ;

    procedure labeldeclaration;
      var llp: lbp; redef: boolean; lbname: integer;
    begin
      repeat
        if sy = intconst then
          with display[top] do
            begin llp := flabel; redef := false;
              while (llp <> nil) and not redef do
                if llp^.labval <> val.ival then
                  llp := llp^.nextlab
                else begin redef := true; error(166) end;
              if not redef then
                begin new(llp);
                  with llp^ do
                    begin labval := val.ival; genlabel(lbname);
                      defined := false; nextlab := flabel; labname := lbname
                    end;
                  flabel := llp
                end;
              insymbol
            end
        else error(15);
        if not ( sy in fsys + [comma, semicolon] ) then
          begin error(6); skip(fsys+[comma,semicolon]) end;
        test := sy <> comma;
        if not test then insymbol
      until test;
      if sy = semicolon then insymbol else error(14)
    end (* labeldeclaration *) ;

    procedure constdeclaration;
      var lcp: ctp; lsp: stp; lvalu: valu;
    begin
      if sy <> ident then
        begin error(2); skip(fsys + [ident]) end;
      while sy = ident do
        begin new(lcp,konst);
          with lcp^ do
            begin name := id; idtype := nil; next := nil; klass:=konst end;
          insymbol;
          if (sy = relop) and (op = eqop) then insymbol else error(16);
          constant(fsys + [semicolon],lsp,lvalu);
          enterid(lcp);
          lcp^.idtype := lsp; lcp^.values := lvalu;
          if sy = semicolon then
            begin insymbol;
              if not (sy in fsys + [ident]) then
                begin error(6); skip(fsys + [ident]) end
            end
          else error(14)
        end
    end (*constdeclaration*) ;

    procedure typedeclaration;
      var lcp,lcp1,lcp2: ctp; lsp: stp; lsize: addrrange;
    begin
      if sy <> ident then
        begin error(2); skip(fsys + [ident]) end;
      while sy = ident do
        begin new(lcp,types);
          with lcp^ do
            begin name := id; idtype := nil; klass := types end;
          insymbol;
          if (sy = relop) and (op = eqop) then insymbol else error(16);
          typ(fsys + [semicolon],lsp,lsize);
          enterid(lcp);
          lcp^.idtype := lsp;
          (*has any forward reference been satisfied:*)
          lcp1 := fwptr;
          while lcp1 <> nil do
            begin
              if lcp1^.name = lcp^.name then
                begin lcp1^.idtype^.eltype := lcp^.idtype;
                  if lcp1 <> fwptr then
                    lcp2^.next := lcp1^.next
                  else fwptr := lcp1^.next;
                end
              else lcp2 := lcp1;
              lcp1 := lcp1^.next
            end;
          if sy = semicolon then
            begin insymbol;
              if not (sy in fsys + [ident]) then
                begin error(6); skip(fsys + [ident]) end
            end
          else error(14)
        end;
      if fwptr <> nil then
        begin error(117); writeln(output);
          repeat writeln(output,' type-id ',fwptr^.name);
            fwptr := fwptr^.next
          until fwptr = nil;
          if not eol then write(output,' ': chcnt+16)
        end
    end (*typedeclaration*) ;

    procedure vardeclaration;
      var lcp,nxt: ctp; lsp: stp; lsize: addrrange;
    begin nxt := nil;
      repeat
        repeat
          if sy = ident then
            begin new(lcp,vars);
              with lcp^ do
               begin name := id; next := nxt; klass := vars;
                  idtype := nil; vkind := actual; vlev := level
                end;
              enterid(lcp);
              nxt := lcp;
              insymbol;
            end
          else error(2);
          if not (sy in fsys + [comma,colon] + typedels) then
            begin error(6); skip(fsys+[comma,colon,semicolon]+typedels) end;
          test := sy <> comma;
          if not test then insymbol
        until test;
        if sy = colon then insymbol else error(5);
        typ(fsys + [semicolon] + typedels,lsp,lsize);
        while nxt <> nil do
          with  nxt^ do
            begin align(lsp,lc);
              idtype := lsp; vaddr := lc;
              lc := lc + lsize; nxt := next
            end;
        if sy = semicolon then
          begin insymbol;
            if not (sy in fsys + [ident]) then
              begin error(6); skip(fsys + [ident]) end
          end
        else error(14)
      until (sy <> ident) and not (sy in typedels);
      if fwptr <> nil then
        begin error(117); writeln(output);
          repeat writeln(output,' type-id ',fwptr^.name);
            fwptr := fwptr^.next
          until fwptr = nil;
          if not eol then write(output,' ': chcnt+16)
        end
    end (*vardeclaration*) ;

    procedure procdeclaration(fsy: symbol);
      var oldlev: 0..maxlevel; lcp,lcp1: ctp; lsp: stp;
          forw: boolean; oldtop: disprange;
          llc,lcm: addrrange; lbname: integer; markp: marktype;

      procedure parameterlist(fsy: setofsys; var fpar: ctp);
        var lcp,lcp1,lcp2,lcp3: ctp; lsp: stp; lkind: idkind;
          llc,lsize: addrrange; count: integer;
      begin lcp1 := nil;
        if not (sy in fsy + [lparent]) then
          begin error(7); skip(fsys + fsy + [lparent]) end;
        if sy = lparent then
          begin if forw then error(119);
            insymbol;
            if not (sy in [ident,varsy,procsy,funcsy]) then
              begin error(7); skip(fsys + [ident,rparent]) end;
            while sy in [ident,varsy,procsy,funcsy] do
              begin
                if sy = procsy then
                  begin error(399);
                    repeat insymbol;
                      if sy = ident then
                        begin new(lcp,proc,declared,formal);
                          with lcp^ do
                            begin name := id; idtype := nil; next := lcp1;
                              pflev := level (*beware of parameter procedures*);
                              klass:=proc;pfdeckind:=declared;pfkind:=formal
                            end;
                          enterid(lcp);
                          lcp1 := lcp;
                          align(parmptr,lc);
                          (*lc := lc + some size *)
                          insymbol
                        end
                      else error(2);
                      if not (sy in fsys + [comma,semicolon,rparent]) then
                        begin error(7);skip(fsys+[comma,semicolon,rparent])end
                    until sy <> comma
                  end
                else
                  begin
                    if sy = funcsy then
                      begin error(399); lcp2 := nil;
                        repeat insymbol;
                          if sy = ident then
                            begin new(lcp,func,declared,formal);
                              with lcp^ do
                                begin name := id; idtype := nil; next := lcp2;
                                  pflev := level (*beware param funcs*);
                                  klass:=func;pfdeckind:=declared;
                                  pfkind:=formal
                                end;
                              enterid(lcp);
                             lcp2 := lcp;
                             align(parmptr,lc);
                             (*lc := lc + some size*)
                              insymbol;
                            end;
                          if not (sy in [comma,colon] + fsys) then
                            begin error(7);skip(fsys+[comma,semicolon,rparent])
                            end
                        until sy <> comma;
                        if sy = colon then
                          begin insymbol;
                            if sy = ident then
                              begin searchid([types],lcp);
                                lsp := lcp^.idtype;
                                if lsp <> nil then
                                 if not(lsp^.form in[scalar,subrange,pointer])
                                    then begin error(120); lsp := nil end;
                                lcp3 := lcp2;
                                while lcp2 <> nil do
                                  begin lcp2^.idtype := lsp; lcp := lcp2;
                                    lcp2 := lcp2^.next
                                  end;
                                lcp^.next := lcp1; lcp1 := lcp3;
                                insymbol
                              end
                            else error(2);
                            if not (sy in fsys + [semicolon,rparent]) then
                              begin error(7);skip(fsys+[semicolon,rparent])end
                          end
                        else error(5)
                      end
                    else
                      begin
                        if sy = varsy then
                          begin lkind := formal; insymbol end
                        else lkind := actual;
                        lcp2 := nil;
                        count := 0;
                        repeat
                          if sy = ident then
                            begin new(lcp,vars);
                              with lcp^ do
                                begin name:=id; idtype:=nil; klass:=vars;
                                  vkind := lkind; next := lcp2; vlev := level;
                                end;
                              enterid(lcp);
                              lcp2 := lcp; count := count+1;
                              insymbol;
                            end;
                          if not (sy in [comma,colon] + fsys) then
                            begin error(7);skip(fsys+[comma,semicolon,rparent])
                            end;
                          test := sy <> comma;
                          if not test then insymbol
                        until test;
                        if sy = colon then
                          begin insymbol;
                            if sy = ident then
                              begin searchid([types],lcp);
                                lsp := lcp^.idtype;
                                lsize := ptrsize;
                                if lsp <> nil then
                                  if lkind=actual then
                                    if lsp^.form<=power then lsize := lsp^.size
                                    else if lsp^.form=files then error(121);
                                align(parmptr,lsize);
                                lcp3 := lcp2;
                                align(parmptr,lc);
                                lc := lc+count*lsize;
                                llc := lc;
                                while lcp2 <> nil do
                                  begin lcp := lcp2;
                                    with lcp2^ do
                                      begin idtype := lsp;
                                        llc := llc-lsize;
                                        vaddr := llc;
                                      end;
                                    lcp2 := lcp2^.next
                                  end;
                                lcp^.next := lcp1; lcp1 := lcp3;
                                insymbol
                              end
                            else error(2);
                            if not (sy in fsys + [semicolon,rparent]) then
                              begin error(7);skip(fsys+[semicolon,rparent])end
                          end
                        else error(5);
                      end;
                  end;
                if sy = semicolon then
                  begin insymbol;
                    if not (sy in fsys + [ident,varsy,procsy,funcsy]) then
                      begin error(7); skip(fsys + [ident,rparent]) end
                  end
              end (*while*) ;
            if sy = rparent then
              begin insymbol;
                if not (sy in fsy + fsys) then
                  begin error(6); skip(fsy + fsys) end
              end
            else error(4);
            lcp3 := nil;
            (*reverse pointers and reserve local cells for copies of multiple
             values*)
            while lcp1 <> nil do
              with lcp1^ do
                begin lcp2 := next; next := lcp3;
                  if klass = vars then
                    if idtype <> nil then
                      if (vkind=actual)and(idtype^.form>power) then
                        begin align(idtype,lc);
                          vaddr := lc;
                          lc := lc+idtype^.size;
                        end;
                  lcp3 := lcp1; lcp1 := lcp2
                end;
            fpar := lcp3
          end
            else fpar := nil
    end (*parameterlist*) ;

    begin (*procdeclaration*)
      llc := lc; lc := lcaftermarkstack; forw := false;
      if sy = ident then
        begin searchsection(display[top].fname,lcp); (*decide whether forw.*)
          if lcp <> nil then
            begin
              if lcp^.klass = proc then
                forw := lcp^.forwdecl and(fsy=procsy)and(lcp^.pfkind=actual)
              else
                if lcp^.klass = func then
                  forw:=lcp^.forwdecl and(fsy=funcsy)and(lcp^.pfkind=actual)
                else forw := false;
              if not forw then error(160)
            end;
          if not forw then
            begin
              if fsy = procsy then new(lcp,proc,declared,actual)
              else new(lcp,func,declared,actual);
              with lcp^ do
                begin name := id; idtype := nil;
                  externl := false; pflev := level; genlabel(lbname);
                  pfdeckind := declared; pfkind := actual; pfname := lbname;
                  if fsy = procsy then klass := proc
                  else klass := func
                end;
              enterid(lcp)
            end
          else
            begin lcp1 := lcp^.next;
              while lcp1 <> nil do
                begin
                  with lcp1^ do
                    if klass = vars then
                      if idtype <> nil then
                        begin lcm := vaddr + idtype^.size;
                          if lcm > lc then lc := lcm
                        end;
                  lcp1 := lcp1^.next
                end
            end;
          insymbol
        end
      else
        begin error(2); lcp := ufctptr end;
      oldlev := level; oldtop := top;
      if level < maxlevel then level := level + 1 else error(251);
      if top < displimit then
        begin top := top + 1;
          with display[top] do
            begin
              if forw then fname := lcp^.next
              else fname := nil;
              flabel := nil;
              occur := blck
            end
        end
      else error(250);
      if fsy = procsy then
        begin parameterlist([semicolon],lcp1);
          if not forw then lcp^.next := lcp1
        end
      else
        begin parameterlist([semicolon,colon],lcp1);
          if not forw then lcp^.next := lcp1;
          if sy = colon then
            begin insymbol;
              if sy = ident then
                begin if forw then error(122);
                  searchid([types],lcp1);
                  lsp := lcp1^.idtype;
                  lcp^.idtype := lsp;
                  if lsp <> nil then
                    if not (lsp^.form in [scalar,subrange,pointer]) then
                      begin error(120); lcp^.idtype := nil end;
                  insymbol
                end
              else begin error(2); skip(fsys + [semicolon]) end
            end
          else
            if not forw then error(123)
        end;
      if sy = semicolon then insymbol else error(14);
      if sy = forwardsy then
        begin
          if forw then error(161)
          else lcp^.forwdecl := true;
          insymbol;
          if sy = semicolon then insymbol else error(14);
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
        end
      else
        begin lcp^.forwdecl := false; mark(markp);
          repeat block(fsys,semicolon,lcp);
            if sy = semicolon then
              begin if prtables then printtables(false); insymbol;
                if not (sy in [beginsy,procsy,funcsy]) then
                  begin error(6); skip(fsys) end
              end
            else error(14)
          until (sy in [beginsy,procsy,funcsy]) or eof(input);
          release(markp); (* return local entries on runtime heap *)
        end;
      level := oldlev; top := oldtop; lc := llc;
    end (*procdeclaration*) ;

    procedure body(fsys: setofsys);
      const cstoccmax=4000(*65*); cixmax=1000; (* cstoccmax was too small [sam] *)
      type oprange = 0..63;
      var
          llcp:ctp; saveid:alpha;
          cstptr: array [1..cstoccmax] of csp;
          cstptrix: 0..cstoccmax;
          (*allows referencing of noninteger constants by an index
           (instead of a pointer), which can be stored in the p2-field
           of the instruction record until writeout.
           --> procedure load, procedure writeout*)
          entname, segsize: integer;
          stacktop, topnew, topmax: integer;
          lcmax,llc1: addrrange; lcp: ctp;
          llp: lbp;


      procedure mes(i: integer);
      begin topnew := topnew + cdx[i]*maxstack;
        if topnew > topmax then topmax := topnew
      end;

      procedure putic;
      begin if ic mod 10 = 0 then writeln(prr,'i',ic:5) end;

      procedure gen0(fop: oprange);
      begin
        if prcode then begin putic; writeln(prr,mn[fop]:4) end;
        ic := ic + 1; mes(fop)
      end (*gen0*) ;

      procedure gen1(fop: oprange; fp2: integer);
        var k: integer;
      begin
        if prcode then
          begin putic; write(prr,mn[fop]:4);
            if fop = 30 then
              begin writeln(prr,sna[fp2]:12);
                topnew := topnew + pdx[fp2]*maxstack;
                if topnew > topmax then topmax := topnew
              end
            else
              begin
                if fop = 38 then
                   begin write(prr,'''');
                     with cstptr[fp2]^ do
                     begin
                       for k := 1 to slgth do write(prr,sval[k]:1);
                       for k := slgth+1 to strglgth do write(prr,' ');
                     end;
                     writeln(prr,'''')
                   end
                else if fop = 42 then writeln(prr,chr(fp2))
                     else writeln(prr,fp2:12);
                mes(fop)
              end
          end;
        ic := ic + 1
      end (*gen1*) ;

      procedure gen2(fop: oprange; fp1,fp2: integer);
        var k : integer;
      begin
        if prcode then
          begin putic; write(prr,mn[fop]:4);
            case fop of
              45,50,54,56:
                writeln(prr,' ',fp1:3,fp2:8);
              47,48,49,52,53,55:
                begin write(prr,chr(fp1));
                  if chr(fp1) = 'm' then write(prr,fp2:11);
                  writeln(prr)
                end;
              51:
                case fp1 of
                  1: writeln(prr,'i ',fp2);
                  2: begin write(prr,'r ');
                       with cstptr[fp2]^ do
                         for k := 1 to strglgth do write(prr,rval[k]);
                       writeln(prr)
                     end;
                  3: writeln(prr,'b ',fp2);
                  4: writeln(prr,'n');
                  6: writeln(prr,'c ''':3,chr(fp2),'''');
                  5: begin write(prr,'(');
                       with cstptr[fp2]^ do
                         for k := setlow to sethigh do
                           (* increased for testing [sam] *)
                           if k in pval then write(prr,k:7(*3*));
                       writeln(prr,')')
                     end
                end
            end;
          end;
        ic := ic + 1; mes(fop)
      end (*gen2*) ;

      procedure gentypindicator(fsp: stp);
      begin
        if fsp<>nil then
          with fsp^ do
            case form of
             scalar: if fsp=intptr then write(prr,'i')
                     else
                       if fsp=boolptr then write(prr,'b')
                       else
                         if fsp=charptr then write(prr,'c')
                         else
                           if scalkind = declared then write(prr,'i')
                           else write(prr,'r');
             subrange: gentypindicator(rangetype);
             pointer:  write(prr,'a');
             power:    write(prr,'s');
             records,arrays: write(prr,'m');
             files,tagfld,variant: error(500)
            end
      end (*typindicator*);

      procedure gen0t(fop: oprange; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]:4);
            gentypindicator(fsp);
            writeln(prr);
          end;
        ic := ic + 1; mes(fop)
      end (*gen0t*);

      procedure gen1t(fop: oprange; fp2: integer; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]:4);
            gentypindicator(fsp);
            writeln(prr,fp2:11)
          end;
        ic := ic + 1; mes(fop)
      end (*gen1t*);

      procedure gen2t(fop: oprange; fp1,fp2: integer; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]: 4);
            gentypindicator(fsp);
            (* needed to increase the range of digits here. [sam] *)
            writeln(prr,fp1:3+5*ord(abs(fp1)>99),fp2:11(*8*));
          end;
        ic := ic + 1; mes(fop)
      end (*gen2t*);

      procedure load;
      begin
        with gattr do
          if typtr <> nil then
            begin
              case kind of
                cst:   if (typtr^.form = scalar) and (typtr <> realptr) then
                         if typtr = boolptr then gen2(51(*ldc*),3,cval.ival)
                         else
                           if typtr=charptr then
                             gen2(51(*ldc*),6,cval.ival)
                           else gen2(51(*ldc*),1,cval.ival)
                       else
                         if typtr = nilptr then gen2(51(*ldc*),4,0)
                         else
                           if cstptrix >= cstoccmax then error(254)
                           else
                             begin cstptrix := cstptrix + 1;
                               cstptr[cstptrix] := cval.valp;
                               if typtr = realptr then
                                 gen2(51(*ldc*),2,cstptrix)
                               else
                                 gen2(51(*ldc*),5,cstptrix)
                             end;
                varbl: case access of
                         drct:   if vlevel<=1 then
                                   gen1t(39(*ldo*),dplmt,typtr)
                                 else gen2t(54(*lod*),level-vlevel,dplmt,typtr);
                         indrct: gen1t(35(*ind*),idplmt,typtr);
                         inxd:   error(400)
                       end;
                expr:
              end;
              kind := expr
            end
      end (*load*) ;

      procedure store(var fattr: attr);
      begin
        with fattr do
          if typtr <> nil then
            case access of
              drct:   if vlevel <= 1 then gen1t(43(*sro*),dplmt,typtr)
                      else gen2t(56(*str*),level-vlevel,dplmt,typtr);
              indrct: if idplmt <> 0 then error(400)
                      else gen0t(26(*sto*),typtr);
              inxd:   error(400)
            end
      end (*store*) ;

      procedure loadaddress;
      begin
        with gattr do
          if typtr <> nil then
            begin
              case kind of
                cst:   if string(typtr) then
                         if cstptrix >= cstoccmax then error(254)
                         else
                           begin cstptrix := cstptrix + 1;
                             cstptr[cstptrix] := cval.valp;
                             gen1(38(*lca*),cstptrix)
                           end
                       else error(400);
                varbl: case access of
                         drct:   if vlevel <= 1 then gen1(37(*lao*),dplmt)
                                 else gen2(50(*lda*),level-vlevel,dplmt);
                         indrct: if idplmt <> 0 then
                                   gen1t(34(*inc*),idplmt,nilptr);
                         inxd:   error(400)
                       end;
                expr:  error(400)
              end;
              kind := varbl; access := indrct; idplmt := 0
            end
      end (*loadaddress*) ;


      procedure genfjp(faddr: integer);
      begin load;
        if gattr.typtr <> nil then
          if gattr.typtr <> boolptr then error(144);
        if prcode then begin putic; writeln(prr,mn[33]:4,' l':8,faddr:4) end;
        ic := ic + 1; mes(33)
      end (*genfjp*) ;

      procedure genujpxjp(fop: oprange; fp2: integer);
      begin
       if prcode then
          begin putic; writeln(prr, mn[fop]:4, ' l':8,fp2:4) end;
        ic := ic + 1; mes(fop)
      end (*genujpxjp*);


      procedure gencupent(fop: oprange; fp1,fp2: integer);
      begin
        if prcode then
          begin putic;
            writeln(prr,mn[fop]:4,fp1:4,'l':4,fp2:4)
          end;
        ic := ic + 1; mes(fop)
      end;


      procedure checkbnds(fsp: stp);
        var lmin,lmax: integer;
      begin
        if fsp <> nil then
          if fsp <> intptr then
            if fsp <> realptr then
              if fsp^.form <= subrange then
                begin
                  getbounds(fsp,lmin,lmax);
                  gen2t(45(*chk*),lmin,lmax,fsp)
                end
      end (*checkbnds*);


      procedure putlabel(labname: integer);
      begin if prcode then writeln(prr, 'l', labname:4)
      end (*putlabel*);

      procedure statement(fsys: setofsys);
        label 1;
        var lcp: ctp; llp: lbp;

        procedure expression(fsys: setofsys); forward;

        procedure selector(fsys: setofsys; fcp: ctp);
        var lattr: attr; lcp: ctp; lsize: addrrange; lmin,lmax: integer;
        begin
          with fcp^, gattr do
            begin typtr := idtype; kind := varbl;
              case klass of
                vars:
                  if vkind = actual then
                    begin access := drct; vlevel := vlev;
                      dplmt := vaddr
                    end
                  else
                    begin gen2t(54(*lod*),level-vlev,vaddr,nilptr);
                      access := indrct; idplmt := 0
                    end;
                field:
                  with display[disx] do
                    if occur = crec then
                      begin access := drct; vlevel := clev;
                        dplmt := cdspl + fldaddr
                      end
                    else
                      begin
                        if level = 1 then gen1t(39(*ldo*),vdspl,nilptr)
                        else gen2t(54(*lod*),0,vdspl,nilptr);
                        access := indrct; idplmt := fldaddr
                      end;
                func:
                  if pfdeckind = standard then
                    begin error(150); typtr := nil end
                  else
                    begin
                      if pfkind = formal then error(151)
                      else
                        if (pflev+1<>level)or(fprocp<>fcp) then error(177);
                        begin access := drct; vlevel := pflev + 1;
                          dplmt := 0   (*impl. relat. addr. of fct. result*)
                        end
                    end
              end (*case*)
            end (*with*);
          if not (sy in selectsys + fsys) then
            begin error(59); skip(selectsys + fsys) end;
          while sy in selectsys do
            begin
        (*[*) if sy = lbrack then
                begin
                  repeat lattr := gattr;
                    with lattr do
                      if typtr <> nil then
                        if typtr^.form <> arrays then
                          begin error(138); typtr := nil end;
                    loadaddress;
                    insymbol; expression(fsys + [comma,rbrack]);
                    load;
                    if gattr.typtr <> nil then
                      if gattr.typtr^.form<>scalar then error(113)
                      else if not comptypes(gattr.typtr,intptr) then
                             gen0t(58(*ord*),gattr.typtr);
                    if lattr.typtr <> nil then
                      with lattr.typtr^ do
                        begin
                          if comptypes(inxtype,gattr.typtr) then
                            begin
                              if inxtype <> nil then
                                begin getbounds(inxtype,lmin,lmax);
                                  if debug then
                                    gen2t(45(*chk*),lmin,lmax,intptr);
                                  if lmin>0 then gen1t(31(*dec*),lmin,intptr)
                                  else if lmin<0 then
                                    gen1t(34(*inc*),-lmin,intptr);
                                  (*or simply gen1(31,lmin)*)
                                end
                            end
                          else error(139);
                          with gattr do
                            begin typtr := aeltype; kind := varbl;
                              access := indrct; idplmt := 0
                            end;
                          if gattr.typtr <> nil then
                            begin
                              lsize := gattr.typtr^.size;
                              align(gattr.typtr,lsize);
                              gen1(36(*ixa*),lsize)
                            end
                        end
                  until sy <> comma;
                  if sy = rbrack then insymbol else error(12)
                end (*if sy = lbrack*)
              else
        (*.*)   if sy = period then
                  begin
                    with gattr do
                      begin
                        if typtr <> nil then
                          if typtr^.form <> records then
                            begin error(140); typtr := nil end;
                        insymbol;
                        if sy = ident then
                          begin
                            if typtr <> nil then
                              begin searchsection(typtr^.fstfld,lcp);
                                if lcp = nil then
                                  begin error(152); typtr := nil end
                                else
                                  with lcp^ do
                                    begin typtr := idtype;
                                      case access of
                                        drct:   dplmt := dplmt + fldaddr;
                                        indrct: idplmt := idplmt + fldaddr;
                                        inxd:   error(400)
                                      end
                                    end
                              end;
                            insymbol
                          end (*sy = ident*)
                        else error(2)
                      end (*with gattr*)
                  end (*if sy = period*)
                else
        (*^*)     begin
                    if gattr.typtr <> nil then
                      with gattr,typtr^ do
                        if form = pointer then
                          begin load; typtr := eltype;
                            if debug then gen2t(45(*chk*),1,maxaddr,nilptr);
                            with gattr do
                              begin kind := varbl; access := indrct;
                                idplmt := 0
                              end
                          end
                        else
                          if form = files then typtr := filtype
                          else error(141);
                    insymbol
                  end;
              if not (sy in fsys + selectsys) then
                begin error(6); skip(fsys + selectsys) end
            end (*while*)
        end (*selector*) ;

        procedure call(fsys: setofsys; fcp: ctp);
          var lkey: 1..15;

          procedure variable(fsys: setofsys);
            var lcp: ctp;
          begin
            if sy = ident then
              begin searchid([vars,field],lcp); insymbol end
            else begin error(2); lcp := uvarptr end;
            selector(fsys,lcp)
          end (*variable*) ;

          procedure getputresetrewrite;
          begin variable(fsys + [rparent]); loadaddress;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> files then error(116);
            if lkey <= 2 then gen1(30(*csp*),lkey(*get,put*))
            else error(399)
          end (*getputresetrewrite*) ;

          procedure read;
            var llev:levrange; laddr:addrrange;
                lsp : stp;
          begin
            llev := 1; laddr := lcaftermarkstack;
            if sy = lparent then
              begin insymbol;
                variable(fsys + [comma,rparent]);
                lsp := gattr.typtr; test := false;
                if lsp <> nil then
                  if lsp^.form = files then
                    with gattr, lsp^ do
                      begin
                        if filtype = charptr then
                          begin llev := vlevel; laddr := dplmt end
                        else error(399);
                        if sy = rparent then
                          begin if lkey = 5 then error(116);
                            test := true
                          end
                        else
                          if sy <> comma then
                            begin error(116); skip(fsys + [comma,rparent]) end;
                        if sy = comma then
                          begin insymbol; variable(fsys + [comma,rparent])
                          end
                        else test := true
                      end;
               if not test then
                repeat loadaddress;
                  gen2(50(*lda*),level-llev,laddr);
                  if gattr.typtr <> nil then
                    if gattr.typtr^.form <= subrange then
                      if comptypes(intptr,gattr.typtr) then
                        gen1(30(*csp*),3(*rdi*))
                      else
                        if comptypes(realptr,gattr.typtr) then
                          gen1(30(*csp*),4(*rdr*))
                        else
                          if comptypes(charptr,gattr.typtr) then
                            gen1(30(*csp*),5(*rdc*))
                          else error(399)
                    else error(116);
                  test := sy <> comma;
                  if not test then
                    begin insymbol; variable(fsys + [comma,rparent])
                    end
                until test;
                if sy = rparent then insymbol else error(4)
              end
            else if lkey = 5 then error(116);
            if lkey = 11 then
              begin gen2(50(*lda*),level-llev,laddr);
                gen1(30(*csp*),21(*rln*))
              end
          end (*read*) ;

          procedure write;
            var lsp: stp; default : boolean; llkey: 1..15;
                llev:levrange; laddr,len:addrrange;
          begin llkey := lkey;
            llev := 1; laddr := lcaftermarkstack + charmax;
            if sy = lparent then
            begin insymbol;
            expression(fsys + [comma,colon,rparent]);
            lsp := gattr.typtr; test := false;
            if lsp <> nil then
              if lsp^.form = files then
                with gattr, lsp^ do
                  begin
                    if filtype = charptr then
                      begin llev := vlevel; laddr := dplmt end
                    else error(399);
                    if sy = rparent then
                      begin if llkey = 6 then error(116);
                        test := true
                      end
                    else
                      if sy <> comma then
                        begin error(116); skip(fsys+[comma,rparent]) end;
                    if sy = comma then
                      begin insymbol; expression(fsys+[comma,colon,rparent])
                      end
                    else test := true
                  end;
           if not test then
            repeat
              lsp := gattr.typtr;
              if lsp <> nil then
                if lsp^.form <= subrange then load else loadaddress;
              if sy = colon then
                begin insymbol; expression(fsys + [comma,colon,rparent]);
                  if gattr.typtr <> nil then
                    if gattr.typtr <> intptr then error(116);
                  load; default := false
                end
              else default := true;
              if sy = colon then
                begin insymbol; expression(fsys + [comma,rparent]);
                  if gattr.typtr <> nil then
                    if gattr.typtr <> intptr then error(116);
                  if lsp <> realptr then error(124);
                  load; error(399);
                end
              else
                if lsp = intptr then
                  begin if default then gen2(51(*ldc*),1,10);
                    gen2(50(*lda*),level-llev,laddr);
                    gen1(30(*csp*),6(*wri*))
                  end
                else
                  if lsp = realptr then
                    begin if default then gen2(51(*ldc*),1,20);
                      gen2(50(*lda*),level-llev,laddr);
                      gen1(30(*csp*),8(*wrr*))
                    end
                  else
                    if lsp = charptr then
                      begin if default then gen2(51(*ldc*),1,1);
                        gen2(50(*lda*),level-llev,laddr);
                        gen1(30(*csp*),9(*wrc*))
                      end
                    else
                      if lsp <> nil then
                        begin
                          if lsp^.form = scalar then error(399)
                          else
                            if string(lsp) then
                              begin len := lsp^.size div charmax;
                                if default then
                                      gen2(51(*ldc*),1,len);
                                gen2(51(*ldc*),1,len);
                                gen2(50(*lda*),level-llev,laddr);
                                gen1(30(*csp*),10(*wrs*))
                              end
                            else error(116)
                        end;
              test := sy <> comma;
              if not test then
                begin insymbol; expression(fsys + [comma,colon,rparent])
                end
            until test;
            if sy = rparent then insymbol else error(4)
            end
              else if lkey = 6 then error(116);
            if llkey = 12 then (*writeln*)
              begin gen2(50(*lda*),level-llev,laddr);
                gen1(30(*csp*),22(*wln*))
              end
          end (*write*) ;

          procedure pack;
            var lsp,lsp1: stp;
          begin error(399); variable(fsys + [comma,rparent]);
            lsp := nil; lsp1 := nil;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin lsp := inxtype; lsp1 := aeltype end
                else error(116);
            if sy = comma then insymbol else error(20);
            expression(fsys + [comma,rparent]);
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(116)
              else
                if not comptypes(lsp,gattr.typtr) then error(116);
            if sy = comma then insymbol else error(20);
            variable(fsys + [rparent]);
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin
                    if not comptypes(aeltype,lsp1)
                      or not comptypes(inxtype,lsp) then
                      error(116)
                  end
                else error(116)
          end (*pack*) ;

          procedure unpack;
            var lsp,lsp1: stp;
          begin error(399); variable(fsys + [comma,rparent]);
            lsp := nil; lsp1 := nil;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin lsp := inxtype; lsp1 := aeltype end
                else error(116);
            if sy = comma then insymbol else error(20);
            variable(fsys + [comma,rparent]);
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin
                    if not comptypes(aeltype,lsp1)
                      or not comptypes(inxtype,lsp) then
                      error(116)
                  end
                else error(116);
            if sy = comma then insymbol else error(20);
            expression(fsys + [rparent]);
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(116)
              else
                if not comptypes(lsp,gattr.typtr) then error(116);
          end (*unpack*) ;

          procedure new;
            label 1;
            var lsp,lsp1: stp; varts: integer;
                lsize: addrrange; lval: valu;
          begin variable(fsys + [comma,rparent]); loadaddress;
            lsp := nil; varts := 0; lsize := 0;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = pointer then
                  begin
                    if eltype <> nil then
                      begin lsize := eltype^.size;
                        if eltype^.form = records then lsp := eltype^.recvar
                      end
                  end
                else error(116);
            while sy = comma do
              begin insymbol;constant(fsys + [comma,rparent],lsp1,lval);
                varts := varts + 1;
                (*check to insert here: is constant in tagfieldtype range*)
                if lsp = nil then error(158)
                else
                  if lsp^.form <> tagfld then error(162)
                  else
                    if lsp^.tagfieldp <> nil then
                      if string(lsp1) or (lsp1 = realptr) then error(159)
                      else
                        if comptypes(lsp^.tagfieldp^.idtype,lsp1) then
                          begin
                            lsp1 := lsp^.fstvar;
                            while lsp1 <> nil do
                              with lsp1^ do
                                if varval.ival = lval.ival then
                                  begin lsize := size; lsp := subvar;
                                    goto 1
                                  end
                                else lsp1 := nxtvar;
                            lsize := lsp^.size; lsp := nil;
                          end
                        else error(116);
          1:  end (*while*) ;
            gen2(51(*ldc*),1,lsize);
            gen1(30(*csp*),12(*new*));
          end (*new*) ;

          procedure mark;
          begin variable(fsys+[rparent]);
             if gattr.typtr <> nil then
               if gattr.typtr^.form = pointer then
                 begin loadaddress; gen1(30(*csp*),23(*sav*)) end
               else error(116)
          end(*mark*);

          procedure release;
          begin variable(fsys+[rparent]);
                if gattr.typtr <> nil then
                   if gattr.typtr^.form = pointer then
                      begin load; gen1(30(*csp*),13(*rst*)) end
                   else error(116)
          end (*release*);



          procedure abs;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr = intptr then gen0(0(*abi*))
              else
                if gattr.typtr = realptr then gen0(1(*abr*))
                else begin error(125); gattr.typtr := intptr end
          end (*abs*) ;

          procedure sqr;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr = intptr then gen0(24(*sqi*))
              else
                if gattr.typtr = realptr then gen0(25(*sqr*))
                else begin error(125); gattr.typtr := intptr end
          end (*sqr*) ;

          procedure trunc;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr <> realptr then error(125);
            gen0(27(*trc*));
            gattr.typtr := intptr
          end (*trunc*) ;

          procedure odd;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr <> intptr then error(125);
            gen0(20(*odd*));
            gattr.typtr := boolptr
          end (*odd*) ;

          procedure ord;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr^.form >= power then error(125);
            gen0t(58(*ord*),gattr.typtr);
            gattr.typtr := intptr
          end (*ord*) ;

          procedure chr;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr <> intptr then error(125);
            gen0(59(*chr*));
            gattr.typtr := charptr
          end (*chr*) ;

          procedure predsucc;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(125);
            if lkey = 7 then gen1t(31(*dec*),1,gattr.typtr)
            else gen1t(34(*inc*),1,gattr.typtr)
          end (*predsucc*) ;

          procedure eof;
          begin
            if sy = lparent then
              begin insymbol; variable(fsys + [rparent]);
                if sy = rparent then insymbol else error(4)
              end
            else
              with gattr do
                begin typtr := textptr; kind := varbl; access := drct;
                  vlevel := 1; dplmt := lcaftermarkstack
                end;
            loadaddress;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> files then error(125);
            if lkey = 9 then gen0(8(*eof*)) else gen1(30(*csp*),14(*eln*));
              gattr.typtr := boolptr
          end (*eof*) ;



          procedure callnonstandard;
            var nxt,lcp: ctp; lsp: stp; lkind: idkind; lb: boolean;
                locpar, llc: addrrange;
          begin locpar := 0;
            with fcp^ do
              begin nxt := next; lkind := pfkind;
                if not externl then gen1(41(*mst*),level-pflev)
              end;
            if sy = lparent then
              begin llc := lc;
                repeat lb := false; (*decide whether proc/func must be passed*)
                  if lkind = actual then
                    begin
                      if nxt = nil then error(126)
                      else lb := nxt^.klass in [proc,func]
                    end else error(399);
                  (*For formal proc/func, lb is false and expression
                   will be called, which will always interpret a proc/func id
                   at its beginning as a call rather than a parameter passing.
                   In this implementation, parameter procedures/functions
                   are therefore not allowed to have procedure/function
                   parameters*)
                  insymbol;
                  if lb then   (*pass function or procedure*)
                    begin error(399);
                      if sy <> ident then
                        begin error(2); skip(fsys + [comma,rparent]) end
                      else
                        begin
                          if nxt^.klass = proc then searchid([proc],lcp)
                          else
                            begin searchid([func],lcp);
                              if not comptypes(lcp^.idtype,nxt^.idtype) then
                                error(128)
                            end;
                          insymbol;
                          if not (sy in fsys + [comma,rparent]) then
                            begin error(6); skip(fsys + [comma,rparent]) end
                        end
                    end (*if lb*)
                  else
                    begin expression(fsys + [comma,rparent]);
                      if gattr.typtr <> nil then
                        if lkind = actual then
                          begin
                            if nxt <> nil then
                              begin lsp := nxt^.idtype;
                                if lsp <> nil then
                                  begin
                                    if (nxt^.vkind = actual) then
                                      if lsp^.form <= power then
                                        begin load;
                                          if debug then checkbnds(lsp);
                                          if comptypes(realptr,lsp)
                                             and (gattr.typtr = intptr) then
                                            begin gen0(10(*flt*));
                                              gattr.typtr := realptr
                                            end;
                                          locpar := locpar+lsp^.size;
                                          align(parmptr,locpar);
                                        end
                                      else
                                        begin
                                          loadaddress;
                                          locpar := locpar+ptrsize;
                                          align(parmptr,locpar)
                                        end
                                    else
                                      if gattr.kind = varbl then
                                        begin loadaddress;
                                          locpar := locpar+ptrsize;
                                          align(parmptr,locpar);
                                        end
                                      else error(154);
                                    if not comptypes(lsp,gattr.typtr) then
                                      error(142)
                                  end
                              end
                          end
                      else (*lkind = formal*)
                        begin (*pass formal param*)
                        end
                    end;
                  if (lkind = actual) and (nxt <> nil) then nxt := nxt^.next
                until sy <> comma;
                lc := llc;
                if sy = rparent then insymbol else error(4)
              end (*if lparent*);
            if lkind = actual then
              begin if nxt <> nil then error(126);
                with fcp^ do
                  begin
                    if externl then gen1(30(*csp*),pfname)
                    else gencupent(46(*cup*),locpar,pfname);
                  end
              end;
            gattr.typtr := fcp^.idtype
          end (*callnonstandard*) ;

        begin (*call*)
          if fcp^.pfdeckind = standard then
            begin lkey := fcp^.key;
              if fcp^.klass = proc then
               begin
                if not(lkey in [5,6,11,12]) then
                  if sy = lparent then insymbol else error(9);
                case lkey of
                  1,2,
                  3,4:  getputresetrewrite;
                  5,11: read;
                  6,12: write;
                  7:    pack;
                  8:    unpack;
                  9:    new;
                  10:   release;
                  13:   mark
                end;
                if not(lkey in [5,6,11,12]) then
                  if sy = rparent then insymbol else error(4)
               end
              else
                begin
                  if lkey <= 8 then
                    begin
                      if sy = lparent then insymbol else error(9);
                      expression(fsys+[rparent]); load
                    end;
                  case lkey of
                    1:    abs;
                    2:    sqr;
                    3:    trunc;
                    4:    odd;
                    5:    ord;
                    6:    chr;
                    7,8:  predsucc;
                    9,10: eof
                  end;
                  if lkey <= 8 then
                    if sy = rparent then insymbol else error(4)
                end;
            end (*standard procedures and functions*)
          else callnonstandard
        end (*call*) ;

        procedure expression;
          var lattr: attr; lop: operator; typind: char; lsize: addrrange;

          procedure simpleexpression(fsys: setofsys);
            var lattr: attr; lop: operator; signed: boolean;

            procedure term(fsys: setofsys);
              var lattr: attr; lop: operator;

              procedure factor(fsys: setofsys);
                var lcp: ctp; lvp: csp; varpart: boolean;
                    cstpart: setty; lsp: stp;
              begin
                if not (sy in facbegsys) then
                  begin error(58); skip(fsys + facbegsys);
                    gattr.typtr := nil
                  end;
                while sy in facbegsys do
                  begin
                    case sy of
              (*id*)    ident:
                        begin searchid([konst,vars,field,func],lcp);
                          insymbol;
                          if lcp^.klass = func then
                            begin call(fsys,lcp);
                              with gattr do
                                begin kind := expr;
                                  if typtr <> nil then
                                    if typtr^.form=subrange then
                                      typtr := typtr^.rangetype
                                end
                            end
                          else
                            if lcp^.klass = konst then
                              with gattr, lcp^ do
                                begin typtr := idtype; kind := cst;
                                  cval := values
                                end
                            else
                              begin selector(fsys,lcp);
                                if gattr.typtr<>nil then(*elim.subr.types to*)
                                  with gattr,typtr^ do(*simplify later tests*)
                                    if form = subrange then
                                      typtr := rangetype
                              end
                        end;
              (*cst*)   intconst:
                        begin
                          with gattr do
                            begin typtr := intptr; kind := cst;
                              cval := val
                            end;
                          insymbol
                        end;
                      realconst:
                        begin
                          with gattr do
                            begin typtr := realptr; kind := cst;
                              cval := val
                            end;
                          insymbol
                        end;
                      stringconst:
                        begin
                          with gattr do
                            begin
                              if lgth = 1 then typtr := charptr
                              else
                                begin new(lsp,arrays);
                                  with lsp^ do
                                    begin aeltype := charptr; form:=arrays;
                                      inxtype := nil; size := lgth*charsize
                                    end;
                                  typtr := lsp
                                end;
                              kind := cst; cval := val
                            end;
                          insymbol
                        end;
              (* ( *)   lparent:
                        begin insymbol; expression(fsys + [rparent]);
                          if sy = rparent then insymbol else error(4)
                        end;
              (*not*)   notsy:
                        begin insymbol; factor(fsys);
                          load; gen0(19(*not*));
                          if gattr.typtr <> nil then
                            if gattr.typtr <> boolptr then
                              begin error(135); gattr.typtr := nil end;
                        end;
              (*[*)     lbrack:
                        begin insymbol; cstpart := [ ]; varpart := false;
                          new(lsp,power);
                          with lsp^ do
                            begin elset:=nil;size:=setsize;form:=power end;
                          if sy = rbrack then
                            begin
                              with gattr do
                                begin typtr := lsp; kind := cst end;
                              insymbol
                            end
                          else
                            begin
                              repeat expression(fsys + [comma,rbrack]);
                                if gattr.typtr <> nil then
                                  if gattr.typtr^.form <> scalar then
                                    begin error(136); gattr.typtr := nil end
                                  else
                                    if comptypes(lsp^.elset,gattr.typtr) then
                                      begin
                                        if gattr.kind = cst then
                                          if (gattr.cval.ival < setlow) or
                                            (gattr.cval.ival > sethigh) then
                                            error(304)
                                          else
                                            cstpart := cstpart+[gattr.cval.ival]
                                        else
                                          begin load;
                                            if not comptypes(gattr.typtr,intptr)
                                            then gen0t(58(*ord*),gattr.typtr);
                                            gen0(23(*sgs*));
                                            if varpart then gen0(28(*uni*))
                                            else varpart := true
                                          end;
                                        lsp^.elset := gattr.typtr;
                                        gattr.typtr := lsp
                                      end
                                    else error(137);
                                test := sy <> comma;
                                if not test then insymbol
                              until test;
                              if sy = rbrack then insymbol else error(12)
                            end;
                          if varpart then
                            begin
                              if cstpart <> [ ] then
                                begin new(lvp,pset); lvp^.pval := cstpart;
                                  lvp^.cclass := pset;
                                  if cstptrix = cstoccmax then error(254)
                                  else
                                    begin cstptrix := cstptrix + 1;
                                      cstptr[cstptrix] := lvp;
                                      gen2(51(*ldc*),5,cstptrix);
                                      gen0(28(*uni*)); gattr.kind := expr
                                    end
                                end
                            end
                          else
                            begin new(lvp,pset); lvp^.cclass := pset;
                              lvp^.pval := cstpart;
                              gattr.cval.valp := lvp
                            end
                        end
                    end (*case*) ;
                    if not (sy in fsys) then
                      begin error(6); skip(fsys + facbegsys) end
                  end (*while*)
              end (*factor*) ;

            begin (*term*)
              factor(fsys + [mulop]);
              while sy = mulop do
                begin load; lattr := gattr; lop := op;
                  insymbol; factor(fsys + [mulop]); load;
                  if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                    case lop of
            (***)     mul:  if (lattr.typtr=intptr)and(gattr.typtr=intptr)
                            then gen0(15(*mpi*))
                            else
                              begin
                                if lattr.typtr = intptr then
                                  begin gen0(9(*flo*));
                                    lattr.typtr := realptr
                                  end
                                else
                                  if gattr.typtr = intptr then
                                    begin gen0(10(*flt*));
                                      gattr.typtr := realptr
                                    end;
                                if (lattr.typtr = realptr)
                                  and(gattr.typtr=realptr)then gen0(16(*mpr*))
                                else
                                  if(lattr.typtr^.form=power)
                                    and comptypes(lattr.typtr,gattr.typtr)then
                                    gen0(12(*int*))
                                  else begin error(134); gattr.typtr:=nil end
                              end;
            (* / *)   rdiv: begin
                              if gattr.typtr = intptr then
                                begin gen0(10(*flt*));
                                  gattr.typtr := realptr
                                end;
                              if lattr.typtr = intptr then
                                begin gen0(9(*flo*));
                                  lattr.typtr := realptr
                                end;
                              if (lattr.typtr = realptr)
                                and (gattr.typtr=realptr)then gen0(7(*dvr*))
                              else begin error(134); gattr.typtr := nil end
                            end;
            (*div*)   idiv: if (lattr.typtr = intptr)
                              and (gattr.typtr = intptr) then gen0(6(*dvi*))
                            else begin error(134); gattr.typtr := nil end;
            (*mod*)   imod: if (lattr.typtr = intptr)
                              and (gattr.typtr = intptr) then gen0(14(*mod*))
                            else begin error(134); gattr.typtr := nil end;
            (*and*)   andop:if (lattr.typtr = boolptr)
                              and (gattr.typtr = boolptr) then gen0(4(*and*))
                            else begin error(134); gattr.typtr := nil end
                    end (*case*)
                  else gattr.typtr := nil
                end (*while*)
            end (*term*) ;

          begin (*simpleexpression*)
            signed := false;
            if (sy = addop) and (op in [plus,minus]) then
              begin signed := op = minus; insymbol end;
            term(fsys + [addop]);
            if signed then
              begin load;
                if gattr.typtr = intptr then gen0(17(*ngi*))
                else
                  if gattr.typtr = realptr then gen0(18(*ngr*))
                  else begin error(134); gattr.typtr := nil end
              end;
            while sy = addop do
              begin load; lattr := gattr; lop := op;
                insymbol; term(fsys + [addop]); load;
                if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                  case lop of
          (*+*)       plus:
                      if (lattr.typtr = intptr)and(gattr.typtr = intptr) then
                        gen0(2(*adi*))
                      else
                        begin
                          if lattr.typtr = intptr then
                            begin gen0(9(*flo*));
                              lattr.typtr := realptr
                            end
                          else
                            if gattr.typtr = intptr then
                              begin gen0(10(*flt*));
                                gattr.typtr := realptr
                              end;
                          if (lattr.typtr = realptr)and(gattr.typtr = realptr)
                            then gen0(3(*adr*))
                          else if(lattr.typtr^.form=power)
                                 and comptypes(lattr.typtr,gattr.typtr) then
                                 gen0(28(*uni*))
                               else begin error(134); gattr.typtr:=nil end
                        end;
          (*-*)       minus:
                      if (lattr.typtr = intptr)and(gattr.typtr = intptr) then
                        gen0(21(*sbi*))
                      else
                        begin
                          if lattr.typtr = intptr then
                            begin gen0(9(*flo*));
                              lattr.typtr := realptr
                            end
                          else
                            if gattr.typtr = intptr then
                              begin gen0(10(*flt*));
                                gattr.typtr := realptr
                              end;
                          if (lattr.typtr = realptr)and(gattr.typtr = realptr)
                            then gen0(22(*sbr*))
                          else
                            if (lattr.typtr^.form = power)
                              and comptypes(lattr.typtr,gattr.typtr) then
                              gen0(5(*dif*))
                            else begin error(134); gattr.typtr := nil end
                        end;
          (*or*)      orop:
                      if(lattr.typtr=boolptr)and(gattr.typtr=boolptr)then
                        gen0(13(*ior*))
                      else begin error(134); gattr.typtr := nil end
                  end (*case*)
                else gattr.typtr := nil
              end (*while*)
          end (*simpleexpression*) ;

        begin (*expression*)
          simpleexpression(fsys + [relop]);
          if sy = relop then
            begin
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              lattr := gattr; lop := op;
              if lop = inop then
                if not comptypes(gattr.typtr,intptr) then
                  gen0t(58(*ord*),gattr.typtr);
              insymbol; simpleexpression(fsys);
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                if lop = inop then
                  if gattr.typtr^.form = power then
                    if comptypes(lattr.typtr,gattr.typtr^.elset) then
                      gen0(11(*inn*))
                    else begin error(129); gattr.typtr := nil end
                  else begin error(130); gattr.typtr := nil end
                else
                  begin
                    if lattr.typtr <> gattr.typtr then
                      if lattr.typtr = intptr then
                        begin gen0(9(*flo*));
                          lattr.typtr := realptr
                        end
                      else
                        if gattr.typtr = intptr then
                          begin gen0(10(*flt*));
                            gattr.typtr := realptr
                          end;
                    if comptypes(lattr.typtr,gattr.typtr) then
                      begin lsize := lattr.typtr^.size;
                        case lattr.typtr^.form of
                          scalar:
                            if lattr.typtr = realptr then typind := 'r'
                            else
                              if lattr.typtr = boolptr then typind := 'b'
                              else
                                if lattr.typtr = charptr then typind := 'c'
                                else typind := 'i';
                          pointer:
                            begin
                              if lop in [ltop,leop,gtop,geop] then error(131);
                              typind := 'a'
                            end;
                          power:
                            begin if lop in [ltop,gtop] then error(132);
                              typind := 's'
                            end;
                          arrays:
                            begin
                              if not string(lattr.typtr)
                                then error(134);
                              typind := 'm'
                            end;
                          records:
                            begin
                              error(134);
                              typind := 'm'
                            end;
                          files:
                            begin error(133); typind := 'f' end
                        end;
                        case lop of
                          ltop: gen2(53(*les*),ord(typind),lsize);
                          leop: gen2(52(*leq*),ord(typind),lsize);
                          gtop: gen2(49(*grt*),ord(typind),lsize);
                          geop: gen2(48(*geq*),ord(typind),lsize);
                          neop: gen2(55(*neq*),ord(typind),lsize);
                          eqop: gen2(47(*equ*),ord(typind),lsize)
                        end
                      end
                    else error(129)
                  end;
              gattr.typtr := boolptr; gattr.kind := expr
            end (*sy = relop*)
        end (*expression*) ;

        procedure assignment(fcp: ctp);
          var lattr: attr;
        begin selector(fsys + [becomes],fcp);
          if sy = becomes then
            begin
              if gattr.typtr <> nil then
                if (gattr.access<>drct) or (gattr.typtr^.form>power) then
                  loadaddress;
              lattr := gattr;
              insymbol; expression(fsys);
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                begin
                  if comptypes(realptr,lattr.typtr)and(gattr.typtr=intptr)then
                    begin gen0(10(*flt*));
                      gattr.typtr := realptr
                    end;
                  if comptypes(lattr.typtr,gattr.typtr) then
                    case lattr.typtr^.form of
                      scalar,
                      subrange: begin
                                  if debug then checkbnds(lattr.typtr);
                                  store(lattr)
                                end;
                      pointer: begin
                                 if debug then
                                   gen2t(45(*chk*),0,maxaddr,nilptr);
                                 store(lattr)
                               end;
                      power:   store(lattr);
                      arrays,
                      records: gen1(40(*mov*),lattr.typtr^.size);
                      files: error(146)
                    end
                  else error(129)
                end
            end (*sy = becomes*)
          else error(51)
        end (*assignment*) ;

        procedure gotostatement;
          var llp: lbp; found: boolean; ttop,ttop1: disprange;
        begin
          if sy = intconst then
            begin
              found := false;
              ttop := top;
              while display[ttop].occur <> blck do ttop := ttop - 1;
              ttop1 := ttop;
              repeat
                llp := display[ttop].flabel;
                while (llp <> nil) and not found do
                  with llp^ do
                    if labval = val.ival then
                      begin found := true;
                        if ttop = ttop1 then
                          genujpxjp(57(*ujp*),labname)
                        else (*goto leads out of procedure*) error(399)
                      end
                    else llp := nextlab;
                ttop := ttop - 1
              until found or (ttop = 0);
              if not found then error(167);
              insymbol
            end
          else error(15)
        end (*gotostatement*) ;

        procedure compoundstatement;
        begin
          repeat
            repeat statement(fsys + [semicolon,endsy])
            until not (sy in statbegsys);
            test := sy <> semicolon;
            if not test then insymbol
          until test;
          if sy = endsy then insymbol else error(13)
        end (*compoundstatemenet*) ;

        procedure ifstatement;
          var lcix1,lcix2: integer;
        begin expression(fsys + [thensy]);
          genlabel(lcix1); genfjp(lcix1);
          if sy = thensy then insymbol else error(52);
          statement(fsys + [elsesy]);
          if sy = elsesy then
            begin genlabel(lcix2); genujpxjp(57(*ujp*),lcix2);
              putlabel(lcix1);
              insymbol; statement(fsys);
              putlabel(lcix2)
            end
          else putlabel(lcix1)
        end (*ifstatement*) ;

        procedure casestatement;
          label 1;
          type cip = ^caseinfo;
               caseinfo = packed
                          record next: cip;
                            csstart: integer;
                            cslab: integer
                          end;
          var lsp,lsp1: stp; fstptr,lpt1,lpt2,lpt3: cip; lval: valu;
              laddr, lcix, lcix1, lmin, lmax: integer;
        begin expression(fsys + [ofsy,comma,colon]);
          load; genlabel(lcix);
          lsp := gattr.typtr;
          if lsp <> nil then
            if (lsp^.form <> scalar) or (lsp = realptr) then
              begin error(144); lsp := nil end
            else if not comptypes(lsp,intptr) then gen0t(58(*ord*),lsp);
          genujpxjp(57(*ujp*),lcix);
          if sy = ofsy then insymbol else error(8);
          fstptr := nil; genlabel(laddr);
          repeat
            lpt3 := nil; genlabel(lcix1);
            if not(sy in [semicolon,endsy]) then
              begin
                repeat constant(fsys + [comma,colon],lsp1,lval);
                  if lsp <> nil then
                    if comptypes(lsp,lsp1) then
                      begin lpt1 := fstptr; lpt2 := nil;
                        while lpt1 <> nil do
                          with lpt1^ do
                            begin
                              if cslab <= lval.ival then
                                begin if cslab = lval.ival then error(156);
                                  goto 1
                                end;
                              lpt2 := lpt1; lpt1 := next
                            end;
            1:    new(lpt3);
                        with lpt3^ do
                          begin next := lpt1; cslab := lval.ival;
                            csstart := lcix1
                          end;
                        if lpt2 = nil then fstptr := lpt3
                        else lpt2^.next := lpt3
                      end
                    else error(147);
                  test := sy <> comma;
                  if not test then insymbol
                until test;
                if sy = colon then insymbol else error(5);
                putlabel(lcix1);
                repeat statement(fsys + [semicolon])
                until not (sy in statbegsys);
                if lpt3 <> nil then
                  genujpxjp(57(*ujp*),laddr);
              end;
            test := sy <> semicolon;
            if not test then insymbol
          until test;
          putlabel(lcix);
          if fstptr <> nil then
            begin lmax := fstptr^.cslab;
              (*reverse pointers*)
              lpt1 := fstptr; fstptr := nil;
              repeat lpt2 := lpt1^.next; lpt1^.next := fstptr;
                fstptr := lpt1; lpt1 := lpt2
              until lpt1 = nil;
              lmin := fstptr^.cslab;
              if lmax - lmin < cixmax then
                begin
                  gen2t(45(*chk*),lmin,lmax,intptr);
                  gen2(51(*ldc*),1,lmin); gen0(21(*sbi*)); genlabel(lcix);
                  genujpxjp(44(*xjp*),lcix); putlabel(lcix);
                  repeat
                    with fstptr^ do
                      begin
                        while cslab > lmin do
                           begin gen0(60(*ujc error*));
                             lmin := lmin+1
                           end;
                        genujpxjp(57(*ujp*),csstart);
                        fstptr := next; lmin := lmin + 1
                      end
                  until fstptr = nil;
                  putlabel(laddr)
                end
              else error(157)
            end;
            if sy = endsy then insymbol else error(13)
        end (*casestatement*) ;

        procedure repeatstatement;
          var laddr: integer;
        begin genlabel(laddr); putlabel(laddr);
          repeat statement(fsys + [semicolon,untilsy]);
            if sy in statbegsys then error(14)
          until not(sy in statbegsys);
          while sy = semicolon do
            begin insymbol;
              repeat statement(fsys + [semicolon,untilsy]);
                if sy in statbegsys then error(14)
              until not (sy in statbegsys);
            end;
          if sy = untilsy then
            begin insymbol; expression(fsys); genfjp(laddr)
            end
          else error(53)
        end (*repeatstatement*) ;

        procedure whilestatement;
          var laddr, lcix: integer;
        begin genlabel(laddr); putlabel(laddr);
          expression(fsys + [dosy]); genlabel(lcix); genfjp(lcix);
          if sy = dosy then insymbol else error(54);
          statement(fsys); genujpxjp(57(*ujp*),laddr); putlabel(lcix)
        end (*whilestatement*) ;

        procedure forstatement;
          var lattr: attr;  lsy: symbol;
              lcix, laddr: integer;
                    llc: addrrange;
              typind: char; (* added for typing [sam] *)
        begin llc := lc;
          with lattr do
            begin typtr := nil; kind := varbl;
              access := drct; vlevel := level; dplmt := 0
            end;
          typind := 'i'; (* default to integer [sam] *)
          if sy = ident then
            begin searchid([vars],lcp);
              with lcp^, lattr do
                begin typtr := idtype; kind := varbl;
                  if vkind = actual then
                    begin access := drct; vlevel := vlev;
                      dplmt := vaddr
                    end
                  else begin error(155); typtr := nil end
                end;
              (* determine type of control variable [sam] *)
              if lattr.typtr = boolptr then typind := 'b'
              else if lattr.typtr = charptr then typind := 'c';
              if lattr.typtr <> nil then
                if (lattr.typtr^.form > subrange)
                   or comptypes(realptr,lattr.typtr) then
                  begin error(143); lattr.typtr := nil end;
              insymbol
            end
          else
            begin error(2); skip(fsys + [becomes,tosy,downtosy,dosy]) end;
          if sy = becomes then
            begin insymbol; expression(fsys + [tosy,downtosy,dosy]);
              if gattr.typtr <> nil then
                  if gattr.typtr^.form <> scalar then error(144)
                  else
                    if comptypes(lattr.typtr,gattr.typtr) then
                      begin load; store(lattr) end
                    else error(145)
            end
          else
            begin error(51); skip(fsys + [tosy,downtosy,dosy]) end;
          if sy in [tosy,downtosy] then
            begin lsy := sy; insymbol; expression(fsys + [dosy]);
              if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(144)
                else
                  if comptypes(lattr.typtr,gattr.typtr) then
                    begin load;
                      if not comptypes(lattr.typtr,intptr) then
                        gen0t(58(*ord*),gattr.typtr);
                      align(intptr,lc);
                      gen2t(56(*str*),0,lc,intptr);
                      genlabel(laddr); putlabel(laddr);
                      gattr := lattr; load;
                      if not comptypes(gattr.typtr,intptr) then
                        gen0t(58(*ord*),gattr.typtr);
                      gen2t(54(*lod*),0,lc,intptr);
                      lc := lc + intsize;
                      if lc > lcmax then lcmax := lc;
                      if lsy = tosy then gen2(52(*leq*),ord(typind),1)
                      else gen2(48(*geq*),ord(typind),1);
                    end
                  else error(145)
            end
          else begin error(55); skip(fsys + [dosy]) end;
          genlabel(lcix); genujpxjp(33(*fjp*),lcix);
          if sy = dosy then insymbol else error(54);
          statement(fsys);
          gattr := lattr; load;
          if lsy=tosy then gen1t(34(*inc*),1,gattr.typtr)
          else  gen1t(31(*dec*),1,gattr.typtr);
          store(lattr); genujpxjp(57(*ujp*),laddr); putlabel(lcix);
          lc := llc;
        end (*forstatement*) ;


        procedure withstatement;
          var lcp: ctp; lcnt1: disprange; llc: addrrange;
        begin lcnt1 := 0; llc := lc;
          repeat
            if sy = ident then
              begin searchid([vars,field],lcp); insymbol end
            else begin error(2); lcp := uvarptr end;
            selector(fsys + [comma,dosy],lcp);
            if gattr.typtr <> nil then
              if gattr.typtr^.form = records then
                if top < displimit then
                  begin top := top + 1; lcnt1 := lcnt1 + 1;
                    with display[top] do
                      begin fname := gattr.typtr^.fstfld;
                        flabel := nil
                      end;
                    if gattr.access = drct then
                      with display[top] do
                        begin occur := crec; clev := gattr.vlevel;
                          cdspl := gattr.dplmt
                        end
                    else
                      begin loadaddress;
                        align(nilptr,lc);
                        gen2t(56(*str*),0,lc,nilptr);
                        with display[top] do
                          begin occur := vrec; vdspl := lc end;
                        lc := lc+ptrsize;
                        if lc > lcmax then lcmax := lc
                      end
                  end
                else error(250)
              else error(140);
            test := sy <> comma;
            if not test then insymbol
          until test;
          if sy = dosy then insymbol else error(54);
          statement(fsys);
          top := top-lcnt1; lc := llc;
        end (*withstatement*) ;

      begin (*statement*)
        if sy = intconst then (*label*)
          begin llp := display[level].flabel;
            while llp <> nil do
              with llp^ do
                if labval = val.ival then
                  begin if defined then error(165);
                    putlabel(labname); defined := true;
                    goto 1
                  end
                else llp := nextlab;
            error(167);
      1:    insymbol;
            if sy = colon then insymbol else error(5)
          end;
        if not (sy in fsys + [ident]) then
          begin error(6); skip(fsys) end;
        if sy in statbegsys + [ident] then
          begin
            case sy of
              ident:    begin searchid([vars,field,func,proc],lcp); insymbol;
                          if lcp^.klass = proc then call(fsys,lcp)
                          else assignment(lcp)
                        end;
              beginsy:  begin insymbol; compoundstatement end;
              gotosy:   begin insymbol; gotostatement end;
              ifsy:     begin insymbol; ifstatement end;
              casesy:   begin insymbol; casestatement end;
              whilesy:  begin insymbol; whilestatement end;
              repeatsy: begin insymbol; repeatstatement end;
              forsy:    begin insymbol; forstatement end;
              withsy:   begin insymbol; withstatement end
            end;
            if not (sy in [semicolon,endsy,elsesy,untilsy]) then
              begin error(6); skip(fsys) end
          end
      end (*statement*) ;

    begin (*body*)
      if fprocp <> nil then entname := fprocp^.pfname
      else genlabel(entname);
      cstptrix := 0; topnew := lcaftermarkstack; topmax := lcaftermarkstack;
      putlabel(entname); genlabel(segsize); genlabel(stacktop);
      gencupent(32(*ent1*),1,segsize); gencupent(32(*ent2*),2,stacktop);
      if fprocp <> nil then (*copy multiple values into local cells*)
        begin llc1 := lcaftermarkstack;
          lcp := fprocp^.next;
          while lcp <> nil do
            with lcp^ do
              begin
                align(parmptr,llc1);
                if klass = vars then
                  if idtype <> nil then
                    if idtype^.form > power then
                      begin
                        if vkind = actual then
                          begin
                            gen2(50(*lda*),0,vaddr);
                            gen2t(54(*lod*),0,llc1,nilptr);
                            gen1(40(*mov*),idtype^.size);
                          end;
                        llc1 := llc1 + ptrsize
                      end
                    else llc1 := llc1 + idtype^.size;
                lcp := lcp^.next;
              end;
        end;
      lcmax := lc;
      repeat
        repeat statement(fsys + [semicolon,endsy])
        until not (sy in statbegsys);
        test := sy <> semicolon;
        if not test then insymbol
      until test;
      if sy = endsy then insymbol else error(13);
      llp := display[top].flabel; (*test for undefined labels*)
      while llp <> nil do
        with llp^ do
          begin
            if not defined then
              begin error(168);
                writeln(output); writeln(output,' label ',labval);
                write(output,' ':chcnt+16)
              end;
            llp := nextlab
          end;
      if fprocp <> nil then
        begin
          if fprocp^.idtype = nil then gen1(42(*ret*),ord('p'))
          else gen0t(42(*ret*),fprocp^.idtype);
          align(parmptr,lcmax);
          if prcode then
            begin writeln(prr,'l',segsize:4,'=',lcmax);
              writeln(prr,'l',stacktop:4,'=',topmax)
            end
        end
      else
        begin gen1(42(*ret*),ord('p'));
          align(parmptr,lcmax);
          if prcode then
            begin writeln(prr,'l',segsize:4,'=',lcmax);
              writeln(prr,'l',stacktop:4,'=',topmax);
              writeln(prr,'q')
            end;
          ic := 0;
          (*generate call of main program; note that this call must be loaded
            at absolute address zero*)
          gen1(41(*mst*),0); gencupent(46(*cup*),0,entname); gen0(29(*stp*));
          if prcode then
            writeln(prr,'q');
          saveid := id;
          while fextfilep <> nil do
            begin
              with fextfilep^ do
                if not ((filename = 'input   ') or (filename = 'output  ') or
                        (filename = 'prd     ') or (filename = 'prr     '))
                then begin id := filename;
                       searchid([vars],llcp);
                       if llcp^.idtype<>nil then
                         if llcp^.idtype^.form<>files then
                           begin writeln(output);
                             writeln(output,' ':8,'undeclared ','external ',
                                   'file',fextfilep^.filename:8);
                             write(output,' ':chcnt+16)
                           end
                     end;
                fextfilep := fextfilep^.nextfile
            end;
          id := saveid;
          if prtables then
            begin writeln(output); printtables(true)
            end
        end;
    end (*body*) ;

  begin (*block*)
    dp := true;
    repeat
      if sy = labelsy then
        begin insymbol; labeldeclaration end;
      if sy = constsy then
        begin insymbol; constdeclaration end;
      if sy = typesy then
        begin insymbol; typedeclaration end;
      if sy = varsy then
        begin insymbol; vardeclaration end;
      while sy in [procsy,funcsy] do
        begin lsy := sy; insymbol; procdeclaration(lsy) end;
      if sy <> beginsy then
        begin error(18); skip(fsys) end
    until (sy in statbegsys) or eof(input);
    dp := false;
    if sy = beginsy then insymbol else error(17);
    repeat body(fsys + [casesy]);
      if sy <> fsy then
        begin error(6); skip(fsys) end
    until ((sy = fsy) or (sy in blockbegsys)) or eof(input);
  end (*block*) ;

  procedure programme(fsys:setofsys);
    var extfp:extfilep;
  begin
    if sy = progsy then
      begin insymbol; if sy <> ident then error(2); insymbol;
        if not (sy in [lparent,semicolon]) then error(14);
        if sy = lparent  then
          begin
            repeat insymbol;
              if sy = ident then
                begin new(extfp);
                  with extfp^ do
                    begin filename := id; nextfile := fextfilep end;
                  fextfilep := extfp;
                  insymbol;
                  if not ( sy in [comma,rparent] ) then error(20)
                end
              else error(2)
            until sy <> comma;
            if sy <> rparent then error(4);
            insymbol
          end;
        if sy <> semicolon then error(14)
        else insymbol;
      end;
    repeat block(fsys,period,nil);
      if sy <> period then error(21)
    until (sy = period) or eof(input);
    if list then writeln(output);
    if errinx <> 0 then
      begin list := false; endofline end
  end (*programme*) ;


  procedure stdnames;
  begin
    na[ 1] := 'false   '; na[ 2] := 'true    '; na[ 3] := 'input   ';
    na[ 4] := 'output  '; na[ 5] := 'get     '; na[ 6] := 'put     ';
    na[ 7] := 'reset   '; na[ 8] := 'rewrite '; na[ 9] := 'read    ';
    na[10] := 'write   '; na[11] := 'pack    '; na[12] := 'unpack  ';
    na[13] := 'new     '; na[14] := 'release '; na[15] := 'readln  ';
    na[16] := 'writeln ';
    na[17] := 'abs     '; na[18] := 'sqr     '; na[19] := 'trunc   ';
    na[20] := 'odd     '; na[21] := 'ord     '; na[22] := 'chr     ';
    na[23] := 'pred    '; na[24] := 'succ    '; na[25] := 'eof     ';
    na[26] := 'eoln    ';
    na[27] := 'sin     '; na[28] := 'cos     '; na[29] := 'exp     ';
    na[30] := 'sqrt    '; na[31] := 'ln      '; na[32] := 'arctan  ';
    na[33] := 'prd     '; na[34] := 'prr     '; na[35] := 'mark    ';
  end (*stdnames*) ;

  procedure enterstdtypes;

  begin                                          (*type underlying:*)
                                                        (******************)

    new(intptr,scalar,standard);                              (*integer*)
    with intptr^ do
      begin size := intsize; form := scalar; scalkind := standard end;
    new(realptr,scalar,standard);                            (*real*)
    with realptr^ do
      begin size := realsize; form := scalar; scalkind := standard end;
    new(charptr,scalar,standard);                            (*char*)
    with charptr^ do
      begin size := charsize; form := scalar; scalkind := standard end;
    new(boolptr,scalar,declared);                            (*boolean*)
    with boolptr^ do
      begin size := boolsize; form := scalar; scalkind := declared end;
    new(nilptr,pointer);                                      (*nil*)
    with nilptr^ do
      begin eltype := nil; size := ptrsize; form := pointer end;
    new(parmptr,scalar,standard); (*for alignment of parameters*)
    with parmptr^ do
      begin size := parmsize; form := scalar; scalkind := standard end ;
    new(textptr,files);                                (*text*)
    with textptr^ do
      begin filtype := charptr; size := charsize; form := files end
  end (*enterstdtypes*) ;

  procedure entstdnames;
    var cp,cp1: ctp; i: integer;
  begin                                                (*name:*)
                                                              (*******)

    new(cp,types);                                          (*integer*)
    with cp^ do
      begin name := 'integer '; idtype := intptr; klass := types end;
    enterid(cp);
    new(cp,types);                                          (*real*)
    with cp^ do
      begin name := 'real    '; idtype := realptr; klass := types end;
    enterid(cp);
    new(cp,types);                                          (*char*)
    with cp^ do
      begin name := 'char    '; idtype := charptr; klass := types end;
    enterid(cp);
    new(cp,types);                                          (*boolean*)
    with cp^ do
      begin name := 'boolean '; idtype := boolptr; klass := types end;
    enterid(cp);
    cp1 := nil;
    for i := 1 to 2 do
      begin new(cp,konst);                                  (*false,true*)
        with cp^ do
          begin name := na[i]; idtype := boolptr;
            next := cp1; values.ival := i - 1; klass := konst
          end;
        enterid(cp); cp1 := cp
      end;
    boolptr^.fconst := cp;
    new(cp,konst);                                          (*nil*)
    with cp^ do
      begin name := 'nil     '; idtype := nilptr;
        next := nil; values.ival := 0; klass := konst
      end;
    enterid(cp);
    for i := 3 to 4 do
      begin new(cp,vars);                                    (*input,output*)
        with cp^ do
          begin name := na[i]; idtype := textptr; klass := vars;
            vkind := actual; next := nil; vlev := 1;
            vaddr := lcaftermarkstack+(i-3)*charmax;
          end;
        enterid(cp)
      end;
    for i:=33 to 34 do
      begin new(cp,vars);                                    (*prd,prr files*)
         with cp^ do
           begin name := na[i]; idtype := textptr; klass := vars;
              vkind := actual; next := nil; vlev := 1;
              vaddr := lcaftermarkstack+(i-31)*charmax;
           end;
         enterid(cp)
      end;
    for i := 5 to 16 do
      begin new(cp,proc,standard);                          (*get,put,reset*)
        with cp^ do                                        (*rewrite,read*)
          begin name := na[i]; idtype := nil;            (*write,pack*)
            next := nil; key := i - 4;                  (*unpack,pack*)
            klass := proc; pfdeckind := standard
          end;
        enterid(cp)
      end;
    new(cp,proc,standard);
    with cp^ do
      begin name:=na[35]; idtype:=nil;
            next:= nil; key:=13;
            klass:=proc; pfdeckind:= standard
      end; enterid(cp);
    for i := 17 to 26 do
      begin new(cp,func,standard);                          (*abs,sqr,trunc*)
        with cp^ do                                        (*odd,ord,chr*)
          begin name := na[i]; idtype := nil;            (*pred,succ,eof*)
            next := nil; key := i - 16;
            klass := func; pfdeckind := standard
          end;
        enterid(cp)
      end;
    new(cp,vars);                     (*parameter of predeclared functions*)
    with cp^ do
      begin name := '        '; idtype := realptr; klass := vars;
        vkind := actual; next := nil; vlev := 1; vaddr := 0
      end;
    for i := 27 to 32 do
      begin new(cp1,func,declared,actual);                  (*sin,cos,exp*)
        with cp1^ do                                      (*sqrt,ln,arctan*)
          begin name := na[i]; idtype := realptr; next := cp;
            forwdecl := false; externl := true; pflev := 0; pfname := i - 12;
            klass := func; pfdeckind := declared; pfkind := actual
          end;
        enterid(cp1)
      end
  end (*entstdnames*) ;

  procedure enterundecl;
  begin
    new(utypptr,types);
    with utypptr^ do
      begin name := '        '; idtype := nil; klass := types end;
    new(ucstptr,konst);
    with ucstptr^ do
      begin name := '        '; idtype := nil; next := nil;
        klass := konst; values.ival := 0
      end;
    new(uvarptr,vars);
    with uvarptr^ do
      begin name := '        '; idtype := nil; vkind := actual;
        next := nil; vlev := 0; vaddr := 0; klass := vars
      end;
    new(ufldptr,field);
    with ufldptr^ do
      begin name := '        '; idtype := nil; next := nil; fldaddr := 0;
        klass := field
      end;
    new(uprcptr,proc,declared,actual);
    with uprcptr^ do
      begin name := '        '; idtype := nil; forwdecl := false;
        next := nil; externl := false; pflev := 0; genlabel(pfname);
        klass := proc; pfdeckind := declared; pfkind := actual
      end;
    new(ufctptr,func,declared,actual);
    with ufctptr^ do
      begin name := '        '; idtype := nil; next := nil;
        forwdecl := false; externl := false; pflev := 0; genlabel(pfname);
        klass := func; pfdeckind := declared; pfkind := actual
      end
  end (*enterundecl*) ;

  procedure initscalars;
  begin fwptr := nil;
    prtables := false; list := true; prcode := true; debug := true;
    dp := true; prterr := true; errinx := 0;
    intlabel := 0; kk := 8; fextfilep := nil;
    lc := lcaftermarkstack+filebuffer*charmax;
    (* note in the above reservation of buffer store for 2 text files *)
    ic := 3; eol := true; linecount := 0;
    ch := ' '; chcnt := 0;
    globtestp := nil;
    mxint10 := maxint div 10; digmax := strglgth - 1;
  end (*initscalars*) ;

  procedure initsets;
  begin
    constbegsys := [addop,intconst,realconst,stringconst,ident];
    simptypebegsys := [lparent] + constbegsys;
    typebegsys:=[arrow,packedsy,arraysy,recordsy,setsy,filesy]+simptypebegsys;
    typedels := [arraysy,recordsy,setsy,filesy];
    blockbegsys := [labelsy,constsy,typesy,varsy,procsy,funcsy,beginsy];
    selectsys := [arrow,period,lbrack];
    facbegsys := [intconst,realconst,stringconst,ident,lparent,lbrack,notsy];
    statbegsys := [beginsy,gotosy,ifsy,whilesy,repeatsy,forsy,withsy,casesy];
  end (*initsets*) ;

  procedure inittables;
    procedure reswords;
    begin
      rw[ 1] := 'if      '; rw[ 2] := 'do      '; rw[ 3] := 'of      ';
      rw[ 4] := 'to      '; rw[ 5] := 'in      '; rw[ 6] := 'or      ';
      rw[ 7] := 'end     '; rw[ 8] := 'for     '; rw[ 9] := 'var     ';
      rw[10] := 'div     '; rw[11] := 'mod     '; rw[12] := 'set     ';
      rw[13] := 'and     '; rw[14] := 'not     '; rw[15] := 'then    ';
      rw[16] := 'else    '; rw[17] := 'with    '; rw[18] := 'goto    ';
      rw[19] := 'case    '; rw[20] := 'type    ';
      rw[21] := 'file    '; rw[22] := 'begin   ';
      rw[23] := 'until   '; rw[24] := 'while   '; rw[25] := 'array   ';
      rw[26] := 'const   '; rw[27] := 'label   ';
      rw[28] := 'repeat  '; rw[29] := 'record  '; rw[30] := 'downto  ';
      rw[31] := 'packed  '; rw[32] := 'forward '; rw[33] := 'program ';
      rw[34] := 'function'; rw[35] := 'procedur';
      frw[1] :=  1; frw[2] :=  1; frw[3] :=  7; frw[4] := 15; frw[5] := 22;
      frw[6] := 28; frw[7] := 32; frw[8] := 34; frw[9] := 36;
    end (*reswords*) ;

    procedure symbols;
    begin
      rsy[ 1] := ifsy;      rsy[ 2] := dosy;      rsy[ 3] := ofsy;
      rsy[ 4] := tosy;      rsy[ 5] := relop;     rsy[ 6] := addop;
      rsy[ 7] := endsy;     rsy[ 8] := forsy;     rsy[ 9] := varsy;
      rsy[10] := mulop;     rsy[11] := mulop;     rsy[12] := setsy;
      rsy[13] := mulop;     rsy[14] := notsy;     rsy[15] := thensy;
      rsy[16] := elsesy;    rsy[17] := withsy;    rsy[18] := gotosy;
      rsy[19] := casesy;    rsy[20] := typesy;
      rsy[21] := filesy;    rsy[22] := beginsy;
      rsy[23] := untilsy;   rsy[24] := whilesy;   rsy[25] := arraysy;
      rsy[26] := constsy;   rsy[27] := labelsy;
      rsy[28] := repeatsy;  rsy[29] := recordsy;  rsy[30] := downtosy;
      rsy[31] := packedsy;  rsy[32] := forwardsy; rsy[33] := progsy;
      rsy[34] := funcsy;    rsy[35] := procsy;
      ssy['+'] := addop ;   ssy['-'] := addop;    ssy['*'] := mulop;
      ssy['/'] := mulop ;   ssy['('] := lparent;  ssy[')'] := rparent;
      ssy['$'] := othersy ; ssy['='] := relop;    ssy[' '] := othersy;
      ssy[','] := comma ;   ssy['.'] := period;   ssy['''']:= othersy;
      ssy['['] := lbrack ;  ssy[']'] := rbrack;   ssy[':'] := colon;
      ssy['^'] := arrow ;   ssy['<'] := relop;    ssy['>'] := relop;
      ssy[';'] := semicolon;
    end (*symbols*) ;

    procedure rators;
      var i: integer;
    begin
      for i := 1 to 35 (*nr of res words*) do rop[i] := noop;
      rop[5] := inop; rop[10] := idiv; rop[11] := imod;
      rop[6] := orop; rop[13] := andop;
      for i := ordminchar to ordmaxchar do sop[chr(i)] := noop;
      sop['+'] := plus; sop['-'] := minus; sop['*'] := mul; sop['/'] := rdiv;
      sop['='] := eqop; sop['<'] := ltop;  sop['>'] := gtop;
    end (*rators*) ;

    procedure procmnemonics;
    begin
      sna[ 1] :=' get'; sna[ 2] :=' put'; sna[ 3] :=' rdi'; sna[ 4] :=' rdr';
      sna[ 5] :=' rdc'; sna[ 6] :=' wri'; sna[ 7] :=' wro'; sna[ 8] :=' wrr';
      sna[ 9] :=' wrc'; sna[10] :=' wrs'; sna[11] :=' pak'; sna[12] :=' new';
      sna[13] :=' rst'; sna[14] :=' eln'; sna[15] :=' sin'; sna[16] :=' cos';
      sna[17] :=' exp'; sna[18] :=' sqt'; sna[19] :=' log'; sna[20] :=' atn';
      sna[21] :=' rln'; sna[22] :=' wln'; sna[23] :=' sav';
    end (*procmnemonics*) ;

    procedure instrmnemonics;
    begin
      mn[ 0] :=' abi'; mn[ 1] :=' abr'; mn[ 2] :=' adi'; mn[ 3] :=' adr';
      mn[ 4] :=' and'; mn[ 5] :=' dif'; mn[ 6] :=' dvi'; mn[ 7] :=' dvr';
      mn[ 8] :=' eof'; mn[ 9] :=' flo'; mn[10] :=' flt'; mn[11] :=' inn';
      mn[12] :=' int'; mn[13] :=' ior'; mn[14] :=' mod'; mn[15] :=' mpi';
      mn[16] :=' mpr'; mn[17] :=' ngi'; mn[18] :=' ngr'; mn[19] :=' not';
      mn[20] :=' odd'; mn[21] :=' sbi'; mn[22] :=' sbr'; mn[23] :=' sgs';
      mn[24] :=' sqi'; mn[25] :=' sqr'; mn[26] :=' sto'; mn[27] :=' trc';
      mn[28] :=' uni'; mn[29] :=' stp'; mn[30] :=' csp'; mn[31] :=' dec';
      mn[32] :=' ent'; mn[33] :=' fjp'; mn[34] :=' inc'; mn[35] :=' ind';
      mn[36] :=' ixa'; mn[37] :=' lao'; mn[38] :=' lca'; mn[39] :=' ldo';
      mn[40] :=' mov'; mn[41] :=' mst'; mn[42] :=' ret'; mn[43] :=' sro';
      mn[44] :=' xjp'; mn[45] :=' chk'; mn[46] :=' cup'; mn[47] :=' equ';
      mn[48] :=' geq'; mn[49] :=' grt'; mn[50] :=' lda'; mn[51] :=' ldc';
      mn[52] :=' leq'; mn[53] :=' les'; mn[54] :=' lod'; mn[55] :=' neq';
      mn[56] :=' str'; mn[57] :=' ujp'; mn[58] :=' ord'; mn[59] :=' chr';
      mn[60] :=' ujc';
    end (*instrmnemonics*) ;

    procedure chartypes;
    var i : integer;
    begin
      for i := ordminchar to ordmaxchar do chartp[chr(i)] := illegal;
      chartp['a'] := letter  ;
      chartp['b'] := letter  ; chartp['c'] := letter  ;
      chartp['d'] := letter  ; chartp['e'] := letter  ;
      chartp['f'] := letter  ; chartp['g'] := letter  ;
      chartp['h'] := letter  ; chartp['i'] := letter  ;
      chartp['j'] := letter  ; chartp['k'] := letter  ;
      chartp['l'] := letter  ; chartp['m'] := letter  ;
      chartp['n'] := letter  ; chartp['o'] := letter  ;
      chartp['p'] := letter  ; chartp['q'] := letter  ;
      chartp['r'] := letter  ; chartp['s'] := letter  ;
      chartp['t'] := letter  ; chartp['u'] := letter  ;
      chartp['v'] := letter  ; chartp['w'] := letter  ;
      chartp['x'] := letter  ; chartp['y'] := letter  ;
      chartp['z'] := letter  ; chartp['0'] := number  ;
      chartp['1'] := number  ; chartp['2'] := number  ;
      chartp['3'] := number  ; chartp['4'] := number  ;
      chartp['5'] := number  ; chartp['6'] := number  ;
      chartp['7'] := number  ; chartp['8'] := number  ;
      chartp['9'] := number  ; chartp['+'] := special ;
      chartp['-'] := special ; chartp['*'] := special ;
      chartp['/'] := special ; chartp['('] := chlparen;
      chartp[')'] := special ; chartp['$'] := special ;
      chartp['='] := special ; chartp[' '] := chspace ;
      chartp[','] := special ; chartp['.'] := chperiod;
      chartp['''']:= chstrquo; chartp['['] := special ;
      chartp[']'] := special ; chartp[':'] := chcolon ;
      chartp['^'] := special ; chartp[';'] := special ;
      chartp['<'] := chlt    ; chartp['>'] := chgt    ;
      ordint['0'] := 0; ordint['1'] := 1; ordint['2'] := 2;
      ordint['3'] := 3; ordint['4'] := 4; ordint['5'] := 5;
      ordint['6'] := 6; ordint['7'] := 7; ordint['8'] := 8;
      ordint['9'] := 9;
    end;

    procedure initdx;
    begin
      cdx[ 0] :=  0; cdx[ 1] :=  0; cdx[ 2] := -1; cdx[ 3] := -1;
      cdx[ 4] := -1; cdx[ 5] := -1; cdx[ 6] := -1; cdx[ 7] := -1;
      cdx[ 8] :=  0; cdx[ 9] :=  0; cdx[10] :=  0; cdx[11] := -1;
      cdx[12] := -1; cdx[13] := -1; cdx[14] := -1; cdx[15] := -1;
      cdx[16] := -1; cdx[17] :=  0; cdx[18] :=  0; cdx[19] :=  0;
      cdx[20] :=  0; cdx[21] := -1; cdx[22] := -1; cdx[23] :=  0;
      cdx[24] :=  0; cdx[25] :=  0; cdx[26] := -2; cdx[27] :=  0;
      cdx[28] := -1; cdx[29] :=  0; cdx[30] :=  0; cdx[31] :=  0;
      cdx[32] :=  0; cdx[33] := -1; cdx[34] :=  0; cdx[35] :=  0;
      cdx[36] := -1; cdx[37] := +1; cdx[38] := +1; cdx[39] := +1;
      cdx[40] := -2; cdx[41] :=  0; cdx[42] :=  0; cdx[43] := -1;
      cdx[44] := -1; cdx[45] :=  0; cdx[46] :=  0; cdx[47] := -1;
      cdx[48] := -1; cdx[49] := -1; cdx[50] := +1; cdx[51] := +1;
      cdx[52] := -1; cdx[53] := -1; cdx[54] := +1; cdx[55] := -1;
      cdx[56] := -1; cdx[57] :=  0; cdx[58] :=  0; cdx[59] :=  0;
      cdx[60] :=  0;
      pdx[ 1] := -1; pdx[ 2] := -1; pdx[ 3] := -2; pdx[ 4] := -2;
      pdx[ 5] := -2; pdx[ 6] := -3; pdx[ 7] := -3; pdx[ 8] := -3;
      pdx[ 9] := -3; pdx[10] := -4; pdx[11] :=  0; pdx[12] := -2;
      pdx[13] := -1; pdx[14] :=  0; pdx[15] :=  0; pdx[16] :=  0;
      pdx[17] :=  0; pdx[18] :=  0; pdx[19] :=  0; pdx[20] :=  0;
      pdx[21] := -1; pdx[22] := -1; pdx[23] := -1;
    end;

  begin (*inittables*)
    reswords; symbols; rators;
    instrmnemonics; procmnemonics;
    chartypes; initdx;
  end (*inittables*) ;

begin

  (*initialize*)
  (************)
  initscalars; initsets; inittables;


  (*enter standard names and standard types:*)
  (******************************************)
  level := 0; top := 0;
  with display[0] do
    begin fname := nil; flabel := nil; occur := blck end;
  enterstdtypes;   stdnames; entstdnames;   enterundecl;
  top := 1; level := 1;
  with display[1] do
    begin fname := nil; flabel := nil; occur := blck end;


  (*compile:*)
  (**********)
  {rewrite(prr);} (* Required for ISO 7185 [sam] *)
  insymbol;
  programme(blockbegsys+statbegsys-[casesy]);

end.
