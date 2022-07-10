(*$L-*)
 (*********************************************************
  *                                                       *
  *                                                       *
  *     STEP-WISE DEVELOPMENT OF A PASCAL COMPILER        *
  *     ******************************************        *
  *                                                       *
  *                                                       *
  *     STEP 5:   SYNTAX ANALYSIS INCLUDING ERROR         *
  *               HANDLING; CHECKS BASED ON DECLARA-      *
  *     10/7/73   TIONS; ADDRESS AND CODE GENERATION      *
  *               FOR A HYPOTHETICAL STACK COMPUTER       *
  *                                                       *
  *                                                       *
  *     AUTHOR:   URS AMMANN                              *
  *               FACHGRUPPE COMPUTERWISSENSCHAFTEN       *
  *               EIDG. TECHNISCHE HOCHSCHULE             *
  *               CH-8006 ZUERICH                         *
  *                                                       *
  *                                                       *
  *                                                       *
  *     MODIFICATION OF STEP 5 OF PASCAL COMPILER         *
  *     *****************************************         *
  *                                                       *
  *     THE COMPILER IS NOW WRITTEN IN A SUBSET OF        *
  *     STANDARD PASCAL  -  AS DEFINED IN THE NEW         *
  *     MANUAL BY K. JENSEN AND N. WIRTH  - AND IT        *
  *     PROCESSES EXACTLY THIS SUBSET.                    *
  *                                                       *
  *     AUTHOR OF CHANGES:   KESAV NORI                   *
  *                          COMPUTER GROUP               *
  *                          T.I.F.R.                     *
  *                          HOMI BHABHA ROAD             *
  *                          BOMBAY - 400005              *
  *                          INDIA                        *
  *                                                       *
  *     THESE CHANGES WERE COMPLETED AT ETH, ZURICH       *
  *     ON 20/5/74.                                       *
  *                                                       *
  *     CONVERTED TO ISO 7185 PASCAL BY SCOTT A. MOORE    *
  *     [SAM] ON JAN 22, 2011.                            *
  *                                                       *
  *     VARIOUS CHANGES WERE MADE, ALL MARKED WITH MY     *
  *     INITIALS THUS [SAM]. THERE ARE COMMENTS FOR ALL   *
  *     CHANGES MADE. THE ONLY OTHERS WERE MINOR FORMAT   *
  *     GLITCHES, APPARENTLY DUE TO SEVERAL EOLS          *
  *     INSERTED AT VARIOUS PLACES INTO THE CODE.         *
  *                                                       *
  *********************************************************)


PROGRAM PASCALCOMPILER(INPUT,OUTPUT,PRR);



CONST DISPLIMIT = 20; MAXLEVEL = 10; MAXADDR = 8096;
      INTSIZE = 1; REALSIZE = 2;
      CHARSIZE = 1; BOOLSIZE = 1; SETSIZE =2; PTRSIZE = 1;
      STRGLGTH = 100; MAXINT = 32767;
      { THE NUMBER FOR LCAFTERMARKSTACK WAS FOUND WRONG. THE FORUMLA BELOW AND
        THE MST CODE IN PASINT SHOW IT SHOULD BE 4. [SAM] }
      LCAFTERMARKSTACK = 4{5};
      (*  3*PTRSIZE+MAX OF STANDARD SCALAR SIZES AND PTRSIZE  *)
      FILEBUFFER = 4;
      maxchr = 255; { range of char is 0..255 }



TYPE                                                        (*DESCRIBING:*)
                                                            (*************)


                                                            (*BASIC SYMBOLS*)
                                                            (***************)

     SYMBOL = (IDENT,INTCONST,REALCONST,STRINGCONST,NOTSY,MULOP,ADDOP,RELOP,
               LPARENT,RPARENT,LBRACK,RBRACK,COMMA,SEMICOLON,PERIOD,ARROW,
               COLON,BECOMES,LABELSY,CONSTSY,TYPESY,VARSY,FUNCSY,PROGSY,
               PROCSY,SETSY,PACKEDSY,ARRAYSY,RECORDSY,FILESY,FORWARDSY,
               BEGINSY,IFSY,CASESY,REPEATSY,WHILESY,FORSY,WITHSY,
               GOTOSY,ENDSY,ELSESY,UNTILSY,OFSY,DOSY,TOSY,DOWNTOSY,
               THENSY,OTHERSY);
     OPERATOR = (MUL,RDIV,ANDOP,IDIV,IMOD,PLUS,MINUS,OROP,LTOP,LEOP,GEOP,GTOP,
                 NEOP,EQOP,INOP,NOOP);
     SETOFSYS = SET OF SYMBOL;

                                                            (*CONSTANTS*)
                                                            (***********)

     CSTCLASS = (REEL,PSET,STRG);
     CSP = ^ CONSTANT;
     CONSTANT = RECORD CASE CCLASS: CSTCLASS OF
                         REEL: (RVAL: PACKED ARRAY [1..STRGLGTH] OF CHAR);
                         PSET: (PVAL: SET OF 0..58);
                         STRG: (SLGTH: 0..STRGLGTH;
                                SVAL: PACKED ARRAY [1..STRGLGTH] OF CHAR)
                       END;

     VALU = RECORD CASE {INTVAL:} BOOLEAN OF  (*INTVAL NEVER SET NORE TESTED*)
                     TRUE:  (IVAL: INTEGER);
                     FALSE: (VALP: CSP)
                   END;

                                                           (*DATA STRUCTURES*)
                                                           (*****************)
     LEVRANGE = 0..MAXLEVEL; ADDRRANGE = 0..MAXADDR;
     STRUCTFORM = (SCALAR,SUBRANGE,POINTER,POWER,ARRAYS,RECORDS,FILES,
                   TAGFLD,VARIANT);
     DECLKIND = (STANDARD,DECLARED);
     STP = ^ STRUCTURE; CTP = ^ IDENTIFIER;

     STRUCTURE = { PACKED } RECORD
                   MARKED: BOOLEAN;   (*FOR TEST PHASE ONLY*)
                   SIZE: ADDRRANGE;
                   CASE FORM: STRUCTFORM OF
                     SCALAR:   (CASE SCALKIND: DECLKIND OF
                                  STANDARD: (); { ADDED EMPTY CASE PER ISO 7185
                                                  [SAM] }
                                  DECLARED: (FCONST: CTP));
                     SUBRANGE: (RANGETYPE: STP; MIN,MAX: VALU);
                     POINTER:  (ELTYPE: STP);
                     POWER:    (ELSET: STP);
                     ARRAYS:   (AELTYPE,INXTYPE: STP);
                     RECORDS:  (FSTFLD: CTP; RECVAR: STP);
                     FILES:    (FILTYPE: STP);
                     TAGFLD:   (TAGFIELDP: CTP; FSTVAR: STP);
                     VARIANT:  (NXTVAR,SUBVAR: STP; VARVAL: VALU)
                   END;

                                                            (*NAMES*)
                                                            (*******)

     IDCLASS = (TYPES,KONST,VARS,FIELD,PROC,FUNC);
     SETOFIDS = SET OF IDCLASS;
     IDKIND = (ACTUAL,FORMAL);
     ALPHA = PACKED ARRAY [1..8] OF CHAR;

     IDENTIFIER = { PACKED } RECORD
                   NAME: ALPHA; LLINK, RLINK: CTP;
                   IDTYPE: STP; NEXT: CTP;
                   CASE KLASS: IDCLASS OF
                     TYPES: (); { ADDED EMPTY CASE PER ISO 7185 [SAM] }
                     KONST: (VALUES: VALU);
                     VARS:  (VKIND: IDKIND; VLEV: LEVRANGE; VADDR: ADDRRANGE);
                     FIELD: (FLDADDR: ADDRRANGE);
                     PROC,
                     FUNC:  (CASE PFDECKIND: DECLKIND OF
                              STANDARD: (KEY: 1..15);
                              DECLARED: (PFLEV: LEVRANGE; PFNAME: INTEGER;
                                          CASE PFKIND: IDKIND OF
                                           ACTUAL: (FORWDECL, EXTERN:
                                                    BOOLEAN);
                                           FORMAL: ())) { ADDED EMPTY CASE PER
                                                          ISO 7185 [SAM] }
                   END;


     DISPRANGE = 0..DISPLIMIT;
     WHERE = (BLCK,CREC,VREC,REC);

                                                            (*EXPRESSIONS*)
                                                            (*************)
     ATTRKIND = (CST,VARBL,EXPR);
     VACCESS = (DRCT,INDRCT,INXD);

     ATTR = RECORD TYPTR: STP;
              CASE KIND: ATTRKIND OF
                CST:   (CVAL: VALU);
                VARBL: (CASE ACCESS: VACCESS OF
                          DRCT: (VLEVEL: LEVRANGE; DPLMT: ADDRRANGE);
                          INDRCT: (IDPLMT: ADDRRANGE);
                          INXD: ()); { ADDED EMPTY CASE PER ISO 7185 [SAM] }
                EXPR: () { ADDED EMPTY CASE PER ISO 7185 [SAM] }
              END;

     TESTP = ^ TESTPOINTER;
     TESTPOINTER = PACKED RECORD
                     ELT1,ELT2 : STP;
                     LASTTESTP : TESTP
                   END;

                                                                 (*LABELS*)
                                                                 (********)
     LBP = ^ LABL;
     LABL = RECORD NEXTLAB: LBP; DEFINED: BOOLEAN;
                   LABVAL, LABNAME: INTEGER
            END;

     EXTFILEP = ^FILEREC;
     FILEREC = RECORD FILENAME:ALPHA; NEXTFILE:EXTFILEP END;

     MARKTYPE = ^ INTEGER; { ADDED TYPE FOR STACK MARKS [SAM] }

(*-------------------------------------------------------------------------*)


VAR
 (*PRD, PRR:                    TEXT; *)
    { PRR: TEXT; }                      { DECLARES THE OUTPUT INTERMEDIATE FILE
                                      [SAM] }

                                    (*RETURNED BY SOURCE PROGRAM SCANNER
                                     INSYMBOL:
                                     **********)

    SY: SYMBOL;                     (*LAST SYMBOL*)
    OP: OPERATOR;                   (*CLASSIFICATION OF LAST SYMBOL*)
    VAL: VALU;                      (*VALUE OF LAST CONSTANT*)
    LGTH: INTEGER;                  (*LENGTH OF LAST STRING CONSTANT*)
    ID: ALPHA;                      (*LAST IDENTIFIER (POSSIBLY TRUNCATED)*)
    KK: 1..8;                       (*NR OF CHARS IN LAST IDENTIFIER*)
    CH: CHAR;                       (*LAST CHARACTER*)
    EOL: BOOLEAN;                   (*END OF LINE FLAG*)


                                    (*COUNTERS:*)
                                    (***********)

    CHCNT: 0..81;                   (*CHARACTER COUNTER*)
    LC,IC: ADDRRANGE;               (*DATA LOCATION AND INSTRUCTION COUNTER*)
    LINECOUNT: INTEGER;


                                    (*SWITCHES:*)
                                    (***********)

    DP,                             (*DECLARATION PART*)
    PRTERR,                     (*TO ALLOW FORWARD REFERENCES IN POINTER TYPE
                                  DECLARATION BY SUPPRESSING ERROR MESSAGE*)
    LIST,PRCODE,PRTABLES: BOOLEAN;  (*OUTPUT OPTIONS FOR
                                        -- SOURCE PROGRAM LISTING
                                        -- PRINTING SYMBOLIC CODE
                                        -- DISPLAYING IDENT AND STRUCT TABLES
                                        --> PROCEDURE OPTION*)


                                    (*POINTERS:*)
                                    (***********)
    INTPTR,REALPTR,CHARPTR,
    BOOLPTR,NILPTR,TEXTPTR: STP;    (*POINTERS TO ENTRIES OF STANDARD IDS*)
    UTYPPTR,UCSTPTR,UVARPTR,
    UFLDPTR,UPRCPTR,UFCTPTR,        (*POINTERS TO ENTRIES FOR UNDECLARED IDS*)
    FWPTR: CTP;                     (*HEAD OF CHAIN OF FORW DECL TYPE IDS*)
    FEXTFILEP: EXTFILEP;            (*HEAD OF CHAIN OF EXTERNAL FILES*)
    GLOBTESTP: TESTP;                (*LAST TESTPOINTER*)


                                    (*BOOKKEEPING OF DECLARATION LEVELS:*)
                                    (************************************)

    LEVEL: LEVRANGE;                (*CURRENT STATIC LEVEL*)
    DISX,                           (*LEVEL OF LAST ID SEARCHED BY SEARCHID*)
    TOP: DISPRANGE;                 (*TOP OF DISPLAY*)

    DISPLAY:                        (*WHERE:   MEANS:*)
      ARRAY [DISPRANGE] OF
        PACKED RECORD               (*=BLCK:   ID IS VARIABLE ID*)
          FNAME: CTP; FLABEL: LBP;  (*=CREC:   ID IS FIELD ID IN RECORD WITH*)
          CASE OCCUR: WHERE OF      (*         CONSTANT ADDRESS*)
            BLCK: (); { ADDED EMPTY CASE PER ISO 7185 [SAM] }
            CREC: (CLEV: LEVRANGE;  (*=VREC:   ID IS FIELD ID IN RECORD WITH*)
                  CDSPL: ADDRRANGE);(*         VARIABLE ADDRESS*)
            VREC: (VDSPL: ADDRRANGE);
            REC:  () { ADDED EMPTY CASE PER ISO 7185 [SAM] }
          END;                      (* --> PROCEDURE WITHSTATEMENT*)


                                    (*ERROR MESSAGES:*)
                                    (*****************)

    ERRINX: 0..10;                  (*NR OF ERRORS IN CURRENT SOURCE LINE*)
    ERRLIST:
      ARRAY [1..10] OF
        PACKED RECORD POS: 1..81;
                      NMR: 1..400
               END;




                                    (*EXPRESSION COMPILATION:*)
                                    (*************************)

    GATTR: ATTR;                    (*DESCRIBES THE EXPR CURRENTLY COMPILED*)


                                    (*STRUCTURED CONSTANTS:*)
                                    (***********************)

    CONSTBEGSYS,SIMPTYPEBEGSYS,TYPEBEGSYS,BLOCKBEGSYS,SELECTSYS,FACBEGSYS,
    STATBEGSYS,TYPEDELS: SETOFSYS;
    RW:  ARRAY [1..35(*NR. OF RES. WORDS*)] OF ALPHA;
    FRW: ARRAY [1..9] OF 1..36(*NR. OF RES. WORDS + 1*);
    RSY: ARRAY [1..35(*NR. OF RES. WORDS*)] OF SYMBOL;
    { THIS DEFINITION IS CDC DEPENDENT, CHANGED TO ALL CHARACTERS [SAM] }
    SSY: ARRAY [CHAR {'+'..';'}] OF SYMBOL;
    ROP: ARRAY [1..35(*NR. OF RES. WORDS*)] OF OPERATOR;
    { THIS DEFINITION IS CDC DEPENDENT, CHANGED TO ALL CHARACTERS [SAM] }
    SOP: ARRAY [CHAR {'+'..';'}] OF OPERATOR;
    NA:  ARRAY [1..35] OF ALPHA;
    MN:  ARRAY [0..57] OF PACKED ARRAY [1..4] OF CHAR;
    SNA: ARRAY [1..23] OF PACKED ARRAY [1..4] OF CHAR;

    INTLABEL,MXINT10,DIGMAX: INTEGER;

(*-------------------------------------------------------------------------*)

{ THESE ARE ADDED AS NO-OPS TO GET THINGS WORKING. THE RESULT IS LOSS OF
  STORAGE. [SAM] }
PROCEDURE MARK(VAR P: MARKTYPE); BEGIN P := P (* SHUT UP *) END;
PROCEDURE RELEASE(P: MARKTYPE); BEGIN P := P (* SHUT UP *) END;

PROCEDURE ENDOFLINE;
    VAR LASTPOS,FREEPOS,CURRPOS,CURRNMR,F,K: INTEGER;
  BEGIN
    IF ERRINX > 0 THEN   (*OUTPUT ERROR MESSAGES*)
      BEGIN WRITE(OUTPUT,' ****  ':15);
        LASTPOS := 0; FREEPOS := 1;
        FOR K := 1 TO ERRINX DO
          BEGIN
            WITH ERRLIST[K] DO
              BEGIN CURRPOS := POS; CURRNMR := NMR END;
            IF CURRPOS = LASTPOS THEN WRITE(OUTPUT,',')
            ELSE
              BEGIN
                WHILE FREEPOS < CURRPOS DO
                  BEGIN WRITE(OUTPUT,' '); FREEPOS := FREEPOS + 1 END;
                WRITE(OUTPUT,'^');
                LASTPOS := CURRPOS
              END;
            IF CURRNMR < 10 THEN F := 1
            ELSE IF CURRNMR < 100 THEN F := 2
              ELSE F := 3;
            WRITE(OUTPUT,CURRNMR:F);
            FREEPOS := FREEPOS + F + 1
          END;
        WRITELN(OUTPUT); ERRINX := 0
      END;
    IF LIST THEN
      BEGIN LINECOUNT := LINECOUNT + 1; WRITE(OUTPUT,LINECOUNT:6,'  ':2);
        IF DP THEN WRITE(OUTPUT,LC:7) ELSE WRITE(OUTPUT,IC:7);
        WRITE(OUTPUT,' ')
      END;
    CHCNT := 0
  END  (*ENDOFLINE*) ;

  PROCEDURE ERROR(FERRNR: INTEGER);
  BEGIN
    IF ERRINX >= 9 THEN
      BEGIN ERRLIST[10].NMR := 255; ERRINX := 10 END
    ELSE
      BEGIN ERRINX := ERRINX + 1;
        ERRLIST[ERRINX].NMR := FERRNR
      END;
    ERRLIST[ERRINX].POS := CHCNT
  END (*ERROR*) ;

  PROCEDURE INSYMBOL;
    (*READ NEXT BASIC SYMBOL OF SOURCE PROGRAM AND RETURN ITS
    DESCRIPTION IN THE GLOBAL VARIABLES SY, OP, ID, VAL AND LGTH*)
    LABEL 1,2,3;
    VAR I,K: INTEGER;
        DIGIT: PACKED ARRAY [1..STRGLGTH] OF CHAR;
        STRING: PACKED ARRAY [1..STRGLGTH] OF CHAR;
        LVP: CSP;TEST: BOOLEAN;

    PROCEDURE NEXTCH;
    BEGIN IF EOL THEN
      BEGIN IF LIST THEN WRITELN(OUTPUT); ENDOFLINE
      END;
      IF NOT EOF(INPUT) THEN
       BEGIN EOL := EOLN(INPUT); READ(INPUT,CH);
        IF LIST THEN WRITE(OUTPUT,CH);
        CHCNT := CHCNT + 1
       END
      ELSE WRITELN(OUTPUT,'EOF ENCOUNTERED')
    END;

    PROCEDURE OPTIONS;
    BEGIN
      REPEAT NEXTCH;
        IF CH <> '*' THEN
          BEGIN
            IF CH = 'T' THEN
              BEGIN NEXTCH; PRTABLES := CH = '+' END
            ELSE
              IF CH = 'L' THEN
                BEGIN NEXTCH; LIST := CH = '+';
                  IF NOT LIST THEN WRITELN(OUTPUT)
                END
              ELSE
                IF CH = 'C' THEN
                  BEGIN NEXTCH; PRCODE := CH = '+' END;
            NEXTCH
          END
      UNTIL CH <> ','
    END (*OPTIONS*) ;

  { THIS CODE WAS MOVED HERE TO REFACTOR THE INSYMBOL CODE AND REMOVE THE
    NEED TO JUMP INTO AN INNER BLOCK. [SAM] }
  PROCEDURE CVTINT;
  VAR K: INTEGER;
  BEGIN
     IF I > DIGMAX THEN BEGIN ERROR(203); VAL.IVAL := 0 END
     ELSE
       WITH VAL DO
         BEGIN IVAL := 0;
           FOR K := 1 TO I DO
             BEGIN
               IF IVAL <= MXINT10 THEN
                 IVAL := IVAL*10 + (ORD(DIGIT[K])-ORD('0'))
               ELSE BEGIN ERROR(203); IVAL := 0 END
             END;
           SY := INTCONST
        END
  END;

  BEGIN (*INSYMBOL*)
  1:
    REPEAT WHILE (CH = ' ') AND NOT EOL DO NEXTCH;
      TEST := EOL;
      IF TEST THEN NEXTCH
    UNTIL NOT TEST;
    CASE CH OF
      'A','B','C','D','E','F','G','H','I',
      'J','K','L','M','N','O','P','Q','R',
      'S','T','U','V','W','X','Y','Z':
        BEGIN K := 0;
          REPEAT
            IF K < 8 THEN
             BEGIN K := K + 1; ID[K] := CH END ;
            NEXTCH
          { REPLACED CDC SPECIFIC CHARACTER TEST }
          UNTIL NOT (CH IN ['A','B','C','D','E','F','G','H','I',
                            'J','K','L','M','N','O','P','Q','R',
                            'S','T','U','V','W','X','Y','Z',
                            '0', '1', '1', '2', '3', '4',
                            '5', '6', '7', '8', '9'])
                {(ORD(CH)<ORD('A')) OR (ORD(CH)>ORD('9'))};
          IF K >= KK THEN KK := K
          ELSE
            REPEAT ID[KK] := ' '; KK := KK - 1
            UNTIL KK = K;
          FOR I := FRW[K] TO FRW[K+1] - 1 DO
            IF RW[I] = ID THEN
              BEGIN SY := RSY[I]; OP := ROP[I]; GOTO 2 END;
            SY := IDENT; OP := NOOP;
  2:    END;
      '0','1','2','3','4','5','6','7','8','9':
        BEGIN OP := NOOP; I := 0;
          REPEAT I := I+1; IF I<= DIGMAX THEN DIGIT[I] := CH; NEXTCH
          UNTIL (ORD(CH)<ORD('0')) OR (ORD(CH)>ORD('9'));
          IF (CH = '.') OR (CH = 'E') THEN
            BEGIN
                  K := I;
                  IF CH = '.' THEN
                    BEGIN K := K+1; IF K <= DIGMAX THEN DIGIT[K] := CH;
                      NEXTCH; IF CH = '.' THEN BEGIN CH := ':'; CVTINT; GOTO 3
                                               END;
                      IF (ORD(CH)<ORD('0')) OR (ORD(CH)>ORD('9')) THEN
                        ERROR(201)
                      ELSE
                        REPEAT K := K + 1;
                          IF K <= DIGMAX THEN DIGIT[K] := CH; NEXTCH
                        UNTIL (ORD(CH)<ORD('0')) OR (ORD(CH)>ORD('9'))
                    END;
                  IF CH = 'E' THEN
                    BEGIN K := K+1; IF K <= DIGMAX THEN DIGIT[K] := CH;
                      NEXTCH;
                      IF (CH = '+') OR (CH ='-') THEN
                        BEGIN K := K+1; IF K <= DIGMAX THEN DIGIT[K] := CH;
                          NEXTCH
                        END;
                      IF (ORD(CH)<ORD('0')) OR (ORD(CH)>ORD('9')) THEN
                        ERROR(201)
                      ELSE
                        REPEAT K := K+1;
                          IF K <= DIGMAX THEN DIGIT[K] := CH; NEXTCH
                        UNTIL (ORD(CH)<ORD('0')) OR (ORD(CH)>ORD('9'))
                     END;
                   NEW(LVP,REEL); SY:= REALCONST; LVP^.CCLASS := REEL;
                   WITH LVP^ DO
                     BEGIN FOR I := 1 TO STRGLGTH DO RVAL[I] := ' ';
                       IF K <= DIGMAX THEN
                         FOR I := 2 TO K + 1 DO RVAL[I] := DIGIT[I-1]
                       ELSE BEGIN ERROR(203); RVAL[2] := '0';
                              RVAL[3] := '.'; RVAL[4] := '0'
                            END
                     END;
                   VAL.VALP := LVP;
  3:        END
          ELSE
  {3:}      BEGIN
              { MOVED TO REFACTOR [SAM] }
              CVTINT
              {
              IF I > DIGMAX THEN BEGIN ERROR(203); VAL.IVAL := 0 END
              ELSE
                WITH VAL DO
                  BEGIN IVAL := 0;
                    FOR K := 1 TO I DO
                      BEGIN
                        IF IVAL <= MXINT10 THEN
                          IVAL := IVAL*10 + (ORD(DIGIT[K])-ORD('0'))
                        ELSE BEGIN ERROR(203); IVAL := 0 END
                      END;
                    SY := INTCONST
                 END
              }
            END
        END;
      '''':
        BEGIN LGTH := 0; SY := STRINGCONST;  OP := NOOP;
          REPEAT
            REPEAT NEXTCH; LGTH := LGTH + 1;
                   IF LGTH <= STRGLGTH THEN STRING[LGTH] := CH
            UNTIL (EOL) OR (CH = '''');
            IF EOL THEN ERROR(202) ELSE NEXTCH
          UNTIL CH <> '''';
          LGTH := LGTH - 1;   (*NOW LGTH = NR OF CHARS IN STRING*)
          IF LGTH = 1 THEN VAL.IVAL := ORD(STRING[1])
          ELSE
            BEGIN NEW(LVP,STRG); LVP^.CCLASS:=STRG;
              IF LGTH > STRGLGTH THEN
                BEGIN ERROR(399); LGTH := STRGLGTH END;
              WITH LVP^ DO
                BEGIN SLGTH := LGTH;
                  FOR I := 1 TO LGTH DO SVAL[I] := STRING[I]
                END;
              VAL.VALP := LVP
            END
        END;
      ':':
        BEGIN OP := NOOP; NEXTCH;
          IF CH = '=' THEN
            BEGIN SY := BECOMES; NEXTCH END
          ELSE SY := COLON
        END;
      '.':
        BEGIN OP := NOOP; NEXTCH;
          IF CH = '.' THEN
            BEGIN SY := COLON; NEXTCH END
          ELSE SY := PERIOD
        END;
      '<':
        BEGIN NEXTCH; SY := RELOP;
          IF CH = '=' THEN
            BEGIN OP := LEOP; NEXTCH END
          ELSE
            IF CH = '>' THEN
              BEGIN OP := NEOP; NEXTCH END
            ELSE OP := LTOP
        END;
      '>':
        BEGIN NEXTCH; SY := RELOP;
          IF CH = '=' THEN
            BEGIN OP := GEOP; NEXTCH END
          ELSE OP := GTOP
        END;
      '(':
       BEGIN NEXTCH;
         IF CH = '*' THEN
           BEGIN NEXTCH;
             IF CH = '$' THEN OPTIONS;
             REPEAT
               WHILE CH <> '*'  DO NEXTCH;
               NEXTCH
             UNTIL CH = ')';
             NEXTCH; GOTO 1
           END;
         SY := LPARENT; OP := NOOP
       END;
      '*','+','-',
      '=','/',')',
      '[',']',',',';','^','$':
        BEGIN SY := SSY[CH]; OP := SOP[CH];
          NEXTCH
        END;
      { THIS SECTION FILLS OUT THE CASE FOR THE ENTIRE CHARACTER SET. I CHANGED
        IT TO USE THE ISO 8859-1 (ASCII) CHARACTER SET FROM THE ORIGINAL CDC
        CHARACTER SET. THE CHARACTERS ARE ALL THOSE THAT ARE NOT USED IN THE
        LANGUAGE, AND THEY APPEAR IN ISO 8859-1 ORDERING. [SAM] }
      '!','"','#','%', '&','?','@','\','_','`','{','|','~',' ':
        BEGIN SY := OTHERSY; OP := NOOP; ERROR(399) END
    END (*CASE*)
  END (*INSYMBOL*) ;

  PROCEDURE ENTERID(FCP: CTP);
    (*ENTER ID POINTED AT BY FCP INTO THE NAME-TABLE,
     WHICH ON EACH DECLARATION LEVEL IS ORGANISED AS
     AN UNBALANCED BINARY TREE*)
    VAR NAM: ALPHA; LCP, LCP1: CTP; LLEFT: BOOLEAN;
  BEGIN NAM := FCP^.NAME;
    LCP := DISPLAY[TOP].FNAME;
    IF LCP = NIL THEN
      DISPLAY[TOP].FNAME := FCP
    ELSE
      BEGIN
        REPEAT LCP1 := LCP;
          IF LCP^.NAME = NAM THEN   (*NAME CONFLICT, FOLLOW RIGHT LINK*)
            BEGIN ERROR(101); LCP := LCP^.RLINK; LLEFT := FALSE END
          ELSE
            IF LCP^.NAME < NAM THEN
              BEGIN LCP := LCP^.RLINK; LLEFT := FALSE END
            ELSE BEGIN LCP := LCP^.LLINK; LLEFT := TRUE END
        UNTIL LCP = NIL;
        IF LLEFT THEN LCP1^.LLINK := FCP ELSE LCP1^.RLINK := FCP
      END;
    FCP^.LLINK := NIL; FCP^.RLINK := NIL
  END (*ENTERID*) ;

  PROCEDURE SEARCHSECTION(FCP: CTP; VAR FCP1: CTP);
    (*TO FIND RECORD FIELDS AND FORWARD DECLARED PROCEDURE ID'S
     --> PROCEDURE PROCEDUREDECLARATION
     --> PROCEDURE SELECTOR*)
     LABEL 1;
  BEGIN
    WHILE FCP <> NIL DO
      IF FCP^.NAME = ID THEN GOTO 1
      ELSE IF FCP^.NAME < ID THEN FCP := FCP^.RLINK
        ELSE FCP := FCP^.LLINK;
1:  FCP1 := FCP
  END (*SEARCHSECTION*) ;

  PROCEDURE SEARCHID(FIDCLS: SETOFIDS; VAR FCP: CTP);
    LABEL 1;
    VAR LCP: CTP;
        { THIS NEEDED TO BE LOCAL [SAM] }
        DISXL: DISPRANGE; (*LEVEL OF LAST ID SEARCHED BY SEARCHID*)
  BEGIN
    FOR DISXL := TOP DOWNTO 0 DO
      { BECAUSE THE ORIGINAL PROGRAM RELIES ON USING THE EARLY OUT VALUE OF
        DISX, WE SIMULATE THIS BEHAVIOR BY ASSIGNING THE LOCAL DISXL (LOCAL
        DISX, AS ISO 7185 REQUIRES) TO THE GLOBAL DISX ON EACH LOOP. }
      BEGIN DISX := DISXL; LCP := DISPLAY[DISXL].FNAME;
        WHILE LCP <> NIL DO
          IF LCP^.NAME = ID THEN
            IF LCP^.KLASS IN FIDCLS THEN GOTO 1
            ELSE
              BEGIN IF PRTERR THEN ERROR(103);
                LCP := LCP^.RLINK
              END
          ELSE
            IF LCP^.NAME < ID THEN
              LCP := LCP^.RLINK
            ELSE LCP := LCP^.LLINK
      END;
    (*SEARCH NOT SUCCSESSFUL; SUPPRESS ERROR MESSAGE IN CASE
     OF FORWARD REFERENCED TYPE ID IN POINTER TYPE DEFINITION
     --> PROCEDURE SIMPLETYPE*)
    IF PRTERR THEN
      BEGIN ERROR(104);
        (*TO AVOID RETURNING NIL, REFERENCE AN ENTRY
         FOR AN UNDECLARED ID OF APPROPRIATE CLASS
         --> PROCEDURE ENTERUNDECL*)
        IF TYPES IN FIDCLS THEN LCP := UTYPPTR
        ELSE
          IF VARS IN FIDCLS THEN LCP := UVARPTR
          ELSE
            IF FIELD IN FIDCLS THEN LCP := UFLDPTR
            ELSE
              IF KONST IN FIDCLS THEN LCP := UCSTPTR
              ELSE
                IF PROC IN FIDCLS THEN LCP := UPRCPTR
                ELSE LCP := UFCTPTR;
      END;
1:  FCP := LCP
  END (*SEARCHID*) ;

  PROCEDURE GETBOUNDS(FSP: STP; VAR FMIN,FMAX: INTEGER);
    (*GET INTERNAL BOUNDS OF SUBRANGE OR SCALAR TYPE*)
    (*ASSUME (FSP <> NIL) AND (FSP^.FORM <= SUBRANGE) AND (FSP <> INTPTR)
     AND NOT COMPTYPES(REALPTR,FSP)*)
  BEGIN
    WITH FSP^ DO
      IF FORM = SUBRANGE THEN
        BEGIN FMIN := MIN.IVAL; FMAX := MAX.IVAL END
      ELSE
        BEGIN FMIN := 0;
          IF FSP = CHARPTR THEN FMAX := 63
          ELSE
            IF FSP^.FCONST <> NIL THEN
              FMAX := FSP^.FCONST^.VALUES.IVAL
            ELSE FMAX := 0
        END
  END (*GETBOUNDS*) ;

  PROCEDURE PRINTTABLES(FB: BOOLEAN);
    (*PRINT DATA STRUCTURE AND NAME TABLE*)
    VAR I, LIM: DISPRANGE;

    PROCEDURE MARKER;
      (*MARK DATA STRUCTURE ENTRIES TO AVOID MULTIPLE PRINTOUT*)
      VAR I: INTEGER;

      PROCEDURE MARKCTP(FP: CTP); FORWARD;

      PROCEDURE MARKSTP(FP: STP);
        (*MARK DATA STRUCTURES, PREVENT CYCLES*)
      BEGIN
        IF FP <> NIL THEN
          WITH FP^ DO
            BEGIN MARKED := TRUE;
              CASE FORM OF
              SCALAR:   ;
              SUBRANGE: MARKSTP(RANGETYPE);
              POINTER:  (*DON'T MARK ELTYPE: CYCLE POSSIBLE; WILL BE MARKED
                        ANYWAY, IF FP = TRUE*) ;
              POWER:    MARKSTP(ELSET) ;
              ARRAYS:   BEGIN MARKSTP(AELTYPE); MARKSTP(INXTYPE) END;
              RECORDS:  BEGIN MARKCTP(FSTFLD); MARKSTP(RECVAR) END;
              FILES:    MARKSTP(FILTYPE);
              TAGFLD:   MARKSTP(FSTVAR);
              VARIANT:  BEGIN MARKSTP(NXTVAR); MARKSTP(SUBVAR) END
              END (*CASE*)
            END (*WITH*)
      END (*MARKSTP*);

      PROCEDURE MARKCTP;
      BEGIN
        IF FP <> NIL THEN
          WITH FP^ DO
            BEGIN MARKCTP(LLINK); MARKCTP(RLINK);
              MARKSTP(IDTYPE)
            END
      END (*MARKCTP*);

    BEGIN (*MARK*)
      FOR I := TOP DOWNTO LIM DO
        MARKCTP(DISPLAY[I].FNAME)
    END (*MARK*);

    { THE ORIGINAL COMPILER USED ORD() TO ACT AS A UNIVERSAL TYPE ESCAPE.
      THIS WAS CHANGED TO USE A MORE GENERALLY AVAILABLE METHOD WITH
      UNDISCRIMINATED VARIANTS. LOOK FOR THESE NEW DEFINITIONS IN THE ROUTINES
      FOLLOWSTP AND FOLLOWCTP BELOW. [SAM] }
    FUNCTION ORDSTP(P: STP): INTEGER;
    VAR TCR: RECORD CASE BOOLEAN OF TRUE: (A: STP); FALSE: (B: INTEGER) END;
    BEGIN TCR.A := P; ORDSTP := TCR.B END;
    FUNCTION ORDCTP(P: CTP): INTEGER;
    VAR TCR: RECORD CASE BOOLEAN OF TRUE: (A: CTP); FALSE: (B: INTEGER) END;
    BEGIN TCR.A := P; ORDCTP := TCR.B END;

    PROCEDURE FOLLOWCTP(FP: CTP); FORWARD;

    PROCEDURE FOLLOWSTP(FP: STP);
    BEGIN
      IF FP <> NIL THEN
        WITH FP^ DO
          IF MARKED THEN
            BEGIN MARKED := FALSE; WRITE(OUTPUT,' ':4,ORDSTP(FP):6,SIZE:10);
              CASE FORM OF
              SCALAR:   BEGIN WRITE(OUTPUT,'SCALAR':10);
                          IF SCALKIND = STANDARD THEN
                           WRITE(OUTPUT,'STANDARD':10)
                          ELSE WRITE(OUTPUT,'DECLARED':10,' ':4,ORDCTP(FCONST):6);
                          WRITELN(OUTPUT)
                        END;
              SUBRANGE:BEGIN
                        WRITE(OUTPUT,'SUBRANGE':10,' ':4,ORDSTP(RANGETYPE):6);
                            IF RANGETYPE <> REALPTR THEN
                              WRITE(OUTPUT,MIN.IVAL,MAX.IVAL)
                            ELSE
                              IF (MIN.VALP <> NIL) AND (MAX.VALP <> NIL) THEN
                                WRITE(OUTPUT,' ',MIN.VALP^.RVAL:9,
                                      ' ',MAX.VALP^.RVAL:9);
                            WRITELN(OUTPUT); FOLLOWSTP(RANGETYPE);
                          END;

              POINTER:  WRITELN(OUTPUT,'POINTER':10,' ':4,ORDSTP(ELTYPE):6);
              POWER:    BEGIN WRITELN(OUTPUT,'SET':10,' ':4,ORDSTP(ELSET):6);
                            FOLLOWSTP(ELSET)
                          END;
              ARRAYS:   BEGIN
                         WRITELN(OUTPUT,'ARRAY':10,' ':4,ORDSTP(AELTYPE):6,' ':4,
                            ORDSTP(INXTYPE):6);
                            FOLLOWSTP(AELTYPE); FOLLOWSTP(INXTYPE)
                          END;
              RECORDS:  BEGIN
                        WRITELN(OUTPUT,'RECORD':10,' ':4,ORDCTP(FSTFLD):6,' ':4,
                            ORDSTP(RECVAR):6); FOLLOWCTP(FSTFLD);
                            FOLLOWSTP(RECVAR)
                          END;
              FILES:    BEGIN WRITE(OUTPUT,'FILE':10,' ':4,ORDSTP(FILTYPE):6);
                            FOLLOWSTP(FILTYPE)
                          END;
              TAGFLD:   BEGIN WRITELN(OUTPUT,'TAGFLD':10,' ':4,ORDCTP(TAGFIELDP):6,
                            ' ':4,ORDSTP(FSTVAR):6);
                            FOLLOWSTP(FSTVAR)
                          END;
              VARIANT:  BEGIN WRITELN(OUTPUT,'VARIANT':10,' ':4,ORDSTP(NXTVAR):6,
                            ' ':4,ORDSTP(SUBVAR):6,VARVAL.IVAL);
                            FOLLOWSTP(NXTVAR); FOLLOWSTP(SUBVAR)
                          END
              END (*CASE*)
            END (*IF MARKED*)
    END (*FOLLOWSTP*);

    PROCEDURE FOLLOWCTP;
      VAR I: INTEGER;
    BEGIN
      IF FP <> NIL THEN
        WITH FP^ DO
          BEGIN WRITE(OUTPUT,' ':4,ORDCTP(FP):6,' ',NAME:9,' ':4,ORDCTP(LLINK):6,
            ' ':4,ORDCTP(RLINK):6,' ':4,ORDSTP(IDTYPE):6);
            CASE KLASS OF
              TYPES: WRITE(OUTPUT,'TYPE':10);
              KONST: BEGIN WRITE(OUTPUT,'CONSTANT':10,' ':4,ORDCTP(NEXT):6);
                     IF IDTYPE <> NIL THEN
                         IF IDTYPE = REALPTR THEN
                           BEGIN
                             IF VALUES.VALP <> NIL THEN
                               WRITE(OUTPUT,' ',VALUES.VALP^.RVAL:9)
                           END
                         ELSE
                           IF IDTYPE^.FORM = ARRAYS THEN  (*STRINGCONST*)
                             BEGIN
                               IF VALUES.VALP <> NIL THEN
                                 BEGIN WRITE(OUTPUT,' ');
                                   WITH VALUES.VALP^ DO
                                     FOR I := 1 TO SLGTH DO
                                      WRITE(OUTPUT,SVAL[I])
                                 END
                             END
                           ELSE WRITE(OUTPUT,VALUES.IVAL)
                       END;
              VARS:  BEGIN WRITE(OUTPUT,'VARIABLE':10);
                        IF VKIND = ACTUAL THEN WRITE(OUTPUT,'ACTUAL':10)
                        ELSE WRITE(OUTPUT,'FORMAL':10);
                        WRITE(OUTPUT,' ':4,ORDCTP(NEXT):6,VLEV,' ':4,VADDR:6 );
                      END;
              FIELD: WRITE(OUTPUT,'FIELD':10,' ':4,ORDCTP(NEXT):6,' ':4,FLDADDR:6);
              PROC,
              FUNC:  BEGIN
                        IF KLASS = PROC THEN WRITE(OUTPUT,'PROCEDURE':10)
                        ELSE WRITE(OUTPUT,'FUNCTION':10);
                        IF PFDECKIND = STANDARD THEN
                         WRITE(OUTPUT,'STANDARD':10,
                          KEY:10)
                        ELSE
                          BEGIN WRITE(OUTPUT,'DECLARED':10,' ':4,ORDCTP(NEXT):6);
                            WRITE(OUTPUT,PFLEV,' ':4,PFNAME:6);
                            IF PFKIND = ACTUAL THEN
                              BEGIN WRITE(OUTPUT,'ACTUAL':10);
                                IF FORWDECL THEN WRITE(OUTPUT,'FORWARD':10)
                                ELSE WRITE(OUTPUT,'NOTFORWARD':10);
                                IF EXTERN THEN WRITE(OUTPUT,'EXTERN':10)
                                ELSE WRITE(OUTPUT,'NOT EXTERN':10);
                              END
                            ELSE WRITE(OUTPUT,'FORMAL':10)
                          END
                     END
            END (*CASE*);
            WRITELN(OUTPUT); FOLLOWCTP(LLINK); FOLLOWCTP(RLINK);
            FOLLOWSTP(IDTYPE)
          END (*WITH*)
    END (*FOLLOWCTP*);

  BEGIN (*PRINTTABLES*)
    WRITELN(OUTPUT); WRITELN(OUTPUT); WRITELN(OUTPUT);
    IF FB THEN LIM := 0
    ELSE BEGIN LIM := TOP; WRITE(OUTPUT,' LOCAL') END;
    WRITELN(OUTPUT,' TABLES '); WRITELN(OUTPUT);
    MARKER;
    FOR I := TOP DOWNTO LIM DO
      FOLLOWCTP(DISPLAY[I].FNAME);
      WRITELN(OUTPUT);
      IF NOT EOL THEN WRITE(OUTPUT,' ':CHCNT+16)
  END (*PRINTTABLES*);

  PROCEDURE GENLABEL(VAR NXTLAB: INTEGER);
  BEGIN INTLABEL := INTLABEL + 1;
    NXTLAB := INTLABEL
  END (*GENLABEL*);

  PROCEDURE BLOCK(FSYS: SETOFSYS; FSY: SYMBOL; FPROCP: CTP);
    VAR LSY: SYMBOL; TEST: BOOLEAN;

    PROCEDURE SKIP(FSYS: SETOFSYS);
      (*SKIP INPUT STRING UNTIL RELEVANT SYMBOL FOUND*)
    BEGIN WHILE NOT(SY IN FSYS) DO INSYMBOL
    END (*SKIP*) ;

    PROCEDURE CONSTANT(FSYS: SETOFSYS; VAR FSP: STP; VAR FVALU: VALU);
      VAR LSP: STP; LCP: CTP; SIGN: (NONE,POS,NEG);
          LVP: CSP; I: 2..STRGLGTH;
    BEGIN LSP := NIL; FVALU.IVAL := 0;
      IF NOT(SY IN CONSTBEGSYS) THEN
        BEGIN ERROR(50); SKIP(FSYS+CONSTBEGSYS) END;
      IF SY IN CONSTBEGSYS THEN
        BEGIN
          { STRINGCONSTSY CHANGED TO STRINGCONST. THE MISTAKE SURVIVED ONLY
            BECAUSE THE ORIGINAL IMPLEMENTATIONS RECOGNIZED 8 CHARACTERS ONLY.
            [SAM] }
          IF SY = STRINGCONST THEN
            BEGIN
              IF LGTH = 1 THEN LSP := CHARPTR
              ELSE
                BEGIN
                  NEW(LSP,ARRAYS);
                  WITH LSP^ DO
                    BEGIN AELTYPE := CHARPTR; INXTYPE := NIL;
                       SIZE := LGTH*CHARSIZE; FORM := ARRAYS
                    END
                END;
              FVALU := VAL; INSYMBOL
            END
          ELSE
            BEGIN
              SIGN := NONE;
              IF (SY = ADDOP) AND (OP IN [PLUS,MINUS]) THEN
                BEGIN IF OP = PLUS THEN SIGN := POS ELSE SIGN := NEG;
                  INSYMBOL
                END;
              IF SY = IDENT THEN
                BEGIN SEARCHID([KONST],LCP);
                  WITH LCP^ DO
                    BEGIN LSP := IDTYPE; FVALU := VALUES END;
                  IF SIGN <> NONE THEN
                    IF LSP = INTPTR THEN
                      BEGIN IF SIGN = NEG THEN FVALU.IVAL := -FVALU.IVAL END
                    ELSE
                      IF LSP = REALPTR THEN
                        BEGIN
                          IF SIGN = NEG THEN
                            BEGIN NEW(LVP,REEL);
                              IF FVALU.VALP^.RVAL[1] = '-' THEN
                                LVP^.RVAL[1] := '+'
                              ELSE LVP^.RVAL[1] := '-';
                              FOR I := 2 TO STRGLGTH DO
                                LVP^.RVAL[I] := FVALU.VALP^.RVAL[I];
                              FVALU.VALP := LVP;
                            END
                          END
                        ELSE ERROR(105);
                  INSYMBOL;
                END
              ELSE
                IF SY = INTCONST THEN
                  BEGIN IF SIGN = NEG THEN VAL.IVAL := -VAL.IVAL;
                    LSP := INTPTR; FVALU := VAL; INSYMBOL
                  END
                ELSE
                  IF SY = REALCONST THEN
                    BEGIN IF SIGN = NEG THEN VAL.VALP^.RVAL[1] := '-';
                      LSP := REALPTR; FVALU := VAL; INSYMBOL
                    END
                  ELSE
                    BEGIN ERROR(106); SKIP(FSYS) END
            END;
          IF NOT (SY IN FSYS) THEN
            BEGIN ERROR(6); SKIP(FSYS) END
          END;
      FSP := LSP
    END (*CONSTANT*) ;

    FUNCTION COMPTYPES(FSP1,FSP2: STP) : BOOLEAN;
      (*DECIDE WHETHER STRUCTURES POINTED AT BY FSP1 AND FSP2 ARE COMPATIBLE*)
      VAR NXT1,NXT2: CTP; COMP: BOOLEAN;
        LTESTP1,LTESTP2 : TESTP;
    BEGIN
      IF FSP1 = FSP2 THEN COMPTYPES := TRUE
      ELSE
        IF (FSP1 <> NIL) AND (FSP2 <> NIL) THEN
          IF FSP1^.FORM = FSP2^.FORM THEN
            CASE FSP1^.FORM OF
              SCALAR:
                COMPTYPES := FALSE;
                (* IDENTICAL SCALARS DECLARED ON DIFFERENT LEVELS ARE
                 NOT RECOGNIZED TO BE COMPATIBLE*)
              SUBRANGE:
                COMPTYPES := COMPTYPES(FSP1^.RANGETYPE,FSP2^.RANGETYPE);
              POINTER:
                  BEGIN
                    COMP := FALSE; LTESTP1 := GLOBTESTP;
                    LTESTP2 := GLOBTESTP;
                    WHILE LTESTP1 <> NIL DO
                      WITH LTESTP1^ DO
                        BEGIN
                          IF (ELT1 = FSP1^.ELTYPE) AND
                            (ELT2 = FSP2^.ELTYPE) THEN COMP := TRUE;
                          LTESTP1 := LASTTESTP
                        END;
                    IF NOT COMP THEN

            BEGIN NEW(LTESTP1);
                        WITH LTESTP1^ DO
                          BEGIN ELT1 := FSP1^.ELTYPE;
                            ELT2 := FSP2^.ELTYPE;
                            LASTTESTP := GLOBTESTP
                          END;
                        GLOBTESTP := LTESTP1;
                        COMP := COMPTYPES(FSP1^.ELTYPE,FSP2^.ELTYPE)
                      END;
                    COMPTYPES := COMP; GLOBTESTP := LTESTP2
                  END;
              POWER:
                COMPTYPES := COMPTYPES(FSP1^.ELSET,FSP2^.ELSET);
              ARRAYS:
                COMPTYPES := COMPTYPES(FSP1^.AELTYPE,FSP2^.AELTYPE)
                             AND (FSP1^.SIZE = FSP2^.SIZE);
                (*ALTERNATIVES: -- ADD A THIRD BOOLEAN TERM: INDEXTYPE MUST
                                  BE COMPATIBLE.
                               -- ADD A FOURTH BOOLEAN TERM: LOWBOUNDS MUST
                                  BE THE SAME*)
              RECORDS:
                BEGIN NXT1 := FSP1^.FSTFLD; NXT2 := FSP2^.FSTFLD; COMP:=TRUE;
                  WHILE (NXT1 <> NIL) AND (NXT2 <> NIL) DO
                    BEGIN COMP:=COMP AND COMPTYPES(NXT1^.IDTYPE,NXT2^.IDTYPE);
                      NXT1 := NXT1^.NEXT; NXT2 := NXT2^.NEXT
                    END;
                  COMPTYPES := COMP AND (NXT1 = NIL) AND (NXT2 = NIL)
                              AND(FSP1^.RECVAR = NIL)AND(FSP2^.RECVAR = NIL)
                END;
                (*IDENTICAL RECORDS ARE RECOGNIZED TO BE COMPATIBLE
                 IFF NO VARIANTS OCCUR*)
              FILES:
                COMPTYPES := COMPTYPES(FSP1^.FILTYPE,FSP2^.FILTYPE)
            END (*CASE*)
          ELSE (*FSP1^.FORM <> FSP2^.FORM*)
            IF FSP1^.FORM = SUBRANGE THEN
              COMPTYPES := COMPTYPES(FSP1^.RANGETYPE,FSP2)
            ELSE
              IF FSP2^.FORM = SUBRANGE THEN
                COMPTYPES := COMPTYPES(FSP1,FSP2^.RANGETYPE)
              ELSE COMPTYPES := FALSE
        ELSE COMPTYPES := TRUE
    END (*COMPTYPES*) ;

    FUNCTION STRING(FSP: STP) : BOOLEAN;
    BEGIN STRING := FALSE;
      IF FSP <> NIL THEN
        IF FSP^.FORM = ARRAYS THEN
          IF COMPTYPES(FSP^.AELTYPE,CHARPTR) THEN STRING := TRUE
    END (*STRING*) ;

    PROCEDURE TYP(FSYS: SETOFSYS; VAR FSP: STP; VAR FSIZE: ADDRRANGE);
      VAR LSP,LSP1,LSP2: STP; OLDTOP: DISPRANGE; LCP: CTP;
          LSIZE,DISPL: ADDRRANGE; LMIN,LMAX: INTEGER;

      PROCEDURE SIMPLETYPE(FSYS:SETOFSYS; VAR FSP:STP; VAR FSIZE:ADDRRANGE);
        VAR LSP,LSP1: STP; LCP,LCP1: CTP; TTOP: DISPRANGE;
            LCNT: INTEGER; LVALU: VALU;
      BEGIN FSIZE := 1;
        IF NOT (SY IN SIMPTYPEBEGSYS) THEN
          BEGIN ERROR(1); SKIP(FSYS + SIMPTYPEBEGSYS) END;
        IF SY IN SIMPTYPEBEGSYS THEN
          BEGIN
            IF SY = LPARENT THEN
              BEGIN TTOP := TOP;   (*DECL. CONSTS LOCAL TO INNERMOST BLOCK*)
                WHILE DISPLAY[TOP].OCCUR <> BLCK DO TOP := TOP - 1;
                NEW(LSP,SCALAR,DECLARED);
                WITH LSP^ DO
                  BEGIN SIZE := INTSIZE; FORM := SCALAR;
                    SCALKIND := DECLARED
                  END;
                LCP1 := NIL; LCNT := 0;
                REPEAT INSYMBOL;
                  IF SY = IDENT THEN
                    BEGIN NEW(LCP,KONST);
                      WITH LCP^ DO
                        BEGIN NAME := ID; IDTYPE := LSP; NEXT := LCP1;
                          VALUES.IVAL := LCNT; KLASS := KONST
                        END;
                      ENTERID(LCP);
                      LCNT := LCNT + 1;
                      LCP1 := LCP; INSYMBOL
                    END
                  ELSE ERROR(2);
                  IF NOT (SY IN FSYS + [COMMA,RPARENT]) THEN
                    BEGIN ERROR(6); SKIP(FSYS + [COMMA,RPARENT]) END
                UNTIL SY <> COMMA;
                LSP^.FCONST := LCP1; TOP := TTOP;
                IF SY = RPARENT THEN INSYMBOL ELSE ERROR(4)
              END
            ELSE
              BEGIN
                IF SY = IDENT THEN
                  BEGIN SEARCHID([TYPES,KONST],LCP);
                    INSYMBOL;
                    IF LCP^.KLASS = KONST THEN
                      BEGIN NEW(LSP,SUBRANGE);
                        WITH LSP^, LCP^ DO
                          BEGIN RANGETYPE := IDTYPE; FORM := SUBRANGE;
                            IF STRING(RANGETYPE) THEN
                              BEGIN ERROR(148); RANGETYPE := NIL END;
                            MIN := VALUES; SIZE := INTSIZE
                          END;
                        IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
                        CONSTANT(FSYS,LSP1,LVALU);
                        LSP^.MAX := LVALU;
                        IF LSP^.RANGETYPE <> LSP1 THEN ERROR(107)
                      END
                    ELSE
                      BEGIN LSP := LCP^.IDTYPE;
                        IF LSP <> NIL THEN FSIZE := LSP^.SIZE
                      END
                  END (*SY = IDENT*)
                ELSE
                  BEGIN NEW(LSP,SUBRANGE); LSP^.FORM := SUBRANGE;
                    CONSTANT(FSYS + [COLON],LSP1,LVALU);
                    IF STRING(LSP1) THEN
                      BEGIN ERROR(148); LSP1 := NIL END;
                    WITH LSP^ DO
                      BEGIN RANGETYPE:=LSP1; MIN:=LVALU; SIZE:=INTSIZE END;
                    IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
                    CONSTANT(FSYS,LSP1,LVALU);
                    LSP^.MAX := LVALU;
                    IF LSP^.RANGETYPE <> LSP1 THEN ERROR(107)
                  END;
                IF LSP <> NIL THEN
                  WITH LSP^ DO
                    IF FORM = SUBRANGE THEN
                      IF RANGETYPE <> NIL THEN
                        IF RANGETYPE = REALPTR THEN ERROR(399)
                        ELSE
                          IF MIN.IVAL > MAX.IVAL THEN ERROR(102)
              END;
            FSP := LSP;
            IF NOT (SY IN FSYS) THEN
              BEGIN ERROR(6); SKIP(FSYS) END
          END
            ELSE FSP := NIL
      END (*SIMPLETYPE*) ;

      PROCEDURE FIELDLIST(FSYS: SETOFSYS; VAR FRECVAR: STP);
        VAR LCP,LCP1,NXT,NXT1: CTP; LSP,LSP1,LSP2,LSP3,LSP4: STP;
            MINSIZE,MAXSIZE,LSIZE: ADDRRANGE; LVALU: VALU;
      BEGIN NXT1 := NIL; LSP := NIL;
        IF NOT (SY IN [IDENT,CASESY]) THEN
          BEGIN ERROR(19); SKIP(FSYS + [IDENT,CASESY]) END;
        WHILE SY = IDENT DO
          BEGIN NXT := NXT1;
            REPEAT
              IF SY = IDENT THEN
                BEGIN NEW(LCP,FIELD);
                  WITH LCP^ DO
                    BEGIN NAME := ID; IDTYPE := NIL; NEXT := NXT;
                      KLASS := FIELD
                    END;
                  NXT := LCP;
                  ENTERID(LCP);
                  INSYMBOL
                END
              ELSE ERROR(2);
              IF NOT (SY IN [COMMA,COLON]) THEN
                BEGIN ERROR(6); SKIP(FSYS + [COMMA,COLON,SEMICOLON,CASESY])
                END;
            TEST := SY <> COMMA;
              IF NOT TEST  THEN INSYMBOL
            UNTIL TEST;
            IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
            TYP(FSYS + [CASESY,SEMICOLON],LSP,LSIZE);
            WHILE NXT <> NXT1 DO
              WITH NXT^ DO
                BEGIN IDTYPE := LSP; FLDADDR := DISPL;
                  NXT := NEXT; DISPL := DISPL + LSIZE
                END;
            NXT1 := LCP;
            IF SY = SEMICOLON THEN
              BEGIN INSYMBOL;
                IF NOT (SY IN [IDENT,CASESY]) THEN
                  BEGIN ERROR(19); SKIP(FSYS + [IDENT,CASESY]) END
              END
          END (*WHILE*);
        NXT := NIL;
        WHILE NXT1 <> NIL DO
          WITH NXT1^ DO
            BEGIN LCP := NEXT; NEXT := NXT; NXT := NXT1; NXT1 := LCP END;
        IF SY = CASESY THEN
          BEGIN NEW(LSP,TAGFLD);
            WITH LSP^ DO
              BEGIN TAGFIELDP := NIL; FSTVAR := NIL; FORM:=TAGFLD END;
            FRECVAR := LSP;
            INSYMBOL;
            IF SY = IDENT THEN
              BEGIN NEW(LCP,FIELD);
                WITH LCP^ DO
                  BEGIN NAME := ID; IDTYPE := NIL; KLASS:=FIELD;
                    NEXT := NIL; FLDADDR := DISPL
                  END;
                ENTERID(LCP);
                INSYMBOL;
                IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
                IF SY = IDENT THEN
                  BEGIN SEARCHID([TYPES],LCP1);
                    LSP1 := LCP1^.IDTYPE;
                    IF LSP1 <> NIL THEN
                      BEGIN DISPL := DISPL + LSP1^.SIZE;
                        IF (LSP1^.FORM <= SUBRANGE) OR STRING(LSP1) THEN
                          BEGIN IF COMPTYPES(REALPTR,LSP1) THEN ERROR(109)
                            ELSE IF STRING(LSP1) THEN ERROR(399);
                            LCP^.IDTYPE := LSP1; LSP^.TAGFIELDP := LCP;
                          END
                        ELSE ERROR(110);
                    END;
                    INSYMBOL;
                  END
                ELSE BEGIN ERROR(2); SKIP(FSYS + [OFSY,LPARENT]) END
              END
            ELSE BEGIN ERROR(2); SKIP(FSYS + [OFSY,LPARENT]) END;
            LSP^.SIZE := DISPL;
            IF SY = OFSY THEN INSYMBOL ELSE ERROR(8);
            LSP1 := NIL; MINSIZE := DISPL; MAXSIZE := DISPL;
            REPEAT LSP2 := NIL;
              REPEAT CONSTANT(FSYS + [COMMA,COLON,LPARENT],LSP3,LVALU);
                IF LSP^.TAGFIELDP <> NIL THEN
                 IF NOT COMPTYPES(LSP^.TAGFIELDP^.IDTYPE,LSP3)THEN ERROR(111);
                NEW(LSP3,VARIANT);
                WITH LSP3^ DO
                  BEGIN NXTVAR := LSP1; SUBVAR := LSP2; VARVAL := LVALU;
                    FORM := VARIANT
                  END;
                LSP1 := LSP3; LSP2 := LSP3;
                TEST := SY <> COMMA;
                IF NOT TEST THEN INSYMBOL
              UNTIL TEST;
              IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
              IF SY = LPARENT THEN INSYMBOL ELSE ERROR(9);
              FIELDLIST(FSYS + [RPARENT,SEMICOLON],LSP2);
              IF DISPL > MAXSIZE THEN MAXSIZE := DISPL;
              WHILE LSP3 <> NIL DO
                BEGIN LSP4 := LSP3^.SUBVAR; LSP3^.SUBVAR := LSP2;
                  LSP3^.SIZE := DISPL;
                  LSP3 := LSP4
                END;
              IF SY = RPARENT THEN
                BEGIN INSYMBOL;
                  IF NOT (SY IN FSYS + [SEMICOLON]) THEN
                    BEGIN ERROR(6); SKIP(FSYS + [SEMICOLON]) END
                END
              ELSE ERROR(4);
              TEST := SY <> SEMICOLON;
              IF NOT TEST THEN
                BEGIN DISPL := MINSIZE;
                      INSYMBOL
                END
            UNTIL TEST;
            DISPL := MAXSIZE;
            LSP^.FSTVAR := LSP1;
          END
        ELSE FRECVAR := NIL
      END (*FIELDLIST*) ;

    BEGIN (*TYP*)
      IF NOT (SY IN TYPEBEGSYS) THEN
         BEGIN ERROR(10); SKIP(FSYS + TYPEBEGSYS) END;
      IF SY IN TYPEBEGSYS THEN
        BEGIN
          IF SY IN SIMPTYPEBEGSYS THEN SIMPLETYPE(FSYS,FSP,FSIZE)
          ELSE
    (*^*)     IF SY = ARROW THEN
              BEGIN NEW(LSP,POINTER); FSP := LSP;
                WITH LSP^ DO
                  BEGIN ELTYPE := NIL; SIZE := PTRSIZE; FORM:=POINTER END;
                INSYMBOL;
                IF SY = IDENT THEN
                  BEGIN PRTERR := FALSE; (*NO ERROR IF SEARCH NOT SUCCESSFUL*)
                    SEARCHID([TYPES],LCP); PRTERR := TRUE;
                    IF LCP = NIL THEN   (*FORWARD REFERENCED TYPE ID*)
                      BEGIN NEW(LCP,TYPES);
                        WITH LCP^ DO
                          BEGIN NAME := ID; IDTYPE := LSP;
                            NEXT := FWPTR; KLASS := TYPES
                          END;
                        FWPTR := LCP
                      END
                    ELSE
                      BEGIN
                        IF LCP^.IDTYPE <> NIL THEN
                          IF LCP^.IDTYPE^.FORM = FILES THEN ERROR(108)
                          ELSE LSP^.ELTYPE := LCP^.IDTYPE
                      END;
                    INSYMBOL;
                  END
                ELSE ERROR(2);
              END
            ELSE
              BEGIN
                IF SY = PACKEDSY THEN
                  BEGIN INSYMBOL;
                    IF NOT (SY IN TYPEDELS) THEN
                      BEGIN
                        ERROR(10); SKIP(FSYS + TYPEDELS)
                      END
                  END;
    (*ARRAY*)     IF SY = ARRAYSY THEN
                  BEGIN INSYMBOL;
                    IF SY = LBRACK THEN INSYMBOL ELSE ERROR(11);
                    LSP1 := NIL;
                    REPEAT NEW(LSP,ARRAYS);
                      WITH LSP^ DO
                        BEGIN AELTYPE := LSP1; INXTYPE := NIL; FORM:=ARRAYS END;
                      LSP1 := LSP;
                      SIMPLETYPE(FSYS + [COMMA,RBRACK,OFSY],LSP2,LSIZE);
                      LSP1^.SIZE := LSIZE;
                      IF LSP2 <> NIL THEN
                        IF LSP2^.FORM <= SUBRANGE THEN
                          BEGIN
                            IF LSP2 = REALPTR THEN
                              BEGIN ERROR(109); LSP2 := NIL END
                            ELSE
                              IF LSP2 = INTPTR THEN
                                BEGIN ERROR(149); LSP2 := NIL END;
                            LSP^.INXTYPE := LSP2
                          END
                        ELSE BEGIN ERROR(113); LSP2 := NIL END;
                      TEST := SY <> COMMA;
                      IF NOT TEST THEN INSYMBOL
                    UNTIL TEST;
                    IF SY = RBRACK THEN INSYMBOL ELSE ERROR(12);
                    IF SY = OFSY THEN INSYMBOL ELSE ERROR(8);
                    TYP(FSYS,LSP,LSIZE);
                    REPEAT
                      WITH LSP1^ DO
                        BEGIN LSP2 := AELTYPE; AELTYPE := LSP;
                          IF INXTYPE <> NIL THEN
                            BEGIN GETBOUNDS(INXTYPE,LMIN,LMAX);
                              LSIZE := LSIZE*(LMAX - LMIN + 1);
                              SIZE := LSIZE
                            END
                        END;
                      LSP := LSP1; LSP1 := LSP2
                    UNTIL LSP1 = NIL
                  END
                ELSE
    (*RECORD*)      IF SY = RECORDSY THEN
                    BEGIN INSYMBOL;
                      OLDTOP := TOP;
                      IF TOP < DISPLIMIT THEN
                        BEGIN TOP := TOP + 1;
                          WITH DISPLAY[TOP] DO
                            BEGIN FNAME := NIL;
                              FLABEL := NIL;
                                  OCCUR := REC
                            END
                        END
                      ELSE ERROR(250);
                      DISPL := 0;
                      FIELDLIST(FSYS-[SEMICOLON]+[ENDSY],LSP1);
                      NEW(LSP,RECORDS);
                      WITH LSP^ DO
                        BEGIN FSTFLD := DISPLAY[TOP].FNAME;
                          RECVAR := LSP1; SIZE := DISPL; FORM := RECORDS
                        END;
                      TOP := OLDTOP;
                      IF SY = ENDSY THEN INSYMBOL ELSE ERROR(13)
                    END
                  ELSE
    (*SET*)           IF SY = SETSY THEN
                      BEGIN INSYMBOL;
                        IF SY = OFSY THEN INSYMBOL ELSE ERROR(8);
                        SIMPLETYPE(FSYS,LSP1,LSIZE);
                        IF LSP1 <> NIL THEN
                          IF LSP1^.FORM > SUBRANGE THEN
                            BEGIN ERROR(115); LSP1 := NIL END
                          ELSE
                            IF LSP1 = REALPTR THEN ERROR(114);
                        NEW(LSP,POWER);
                        WITH LSP^ DO
                          BEGIN ELSET:=LSP1; SIZE:=SETSIZE; FORM:=POWER END;
                      END
                    ELSE
    (*FILE*)            IF SY = FILESY THEN
                        BEGIN ERROR(399);SKIP(FSYS);FSP:= NIL END;
                FSP := LSP
              END;
          IF NOT (SY IN FSYS) THEN
            BEGIN ERROR(6); SKIP(FSYS) END
        END
      ELSE FSP := NIL;
      IF FSP = NIL THEN FSIZE := 1 ELSE FSIZE := FSP^.SIZE
    END (*TYP*) ;

    PROCEDURE LABELDECLARATION;
      VAR LLP: LBP; REDEF: BOOLEAN; LBNAME: INTEGER;
    BEGIN
      REPEAT
        IF SY = INTCONST THEN
          WITH DISPLAY[TOP] DO
            BEGIN LLP := FLABEL; REDEF := FALSE;
              WHILE (LLP <> NIL) AND NOT REDEF DO
                IF LLP^.LABVAL <> VAL.IVAL THEN
                  LLP := LLP^.NEXTLAB
                ELSE BEGIN REDEF := TRUE; ERROR(166) END;
              IF NOT REDEF THEN
                BEGIN NEW(LLP);
                  WITH LLP^ DO
                    BEGIN LABVAL := VAL.IVAL; GENLABEL(LBNAME);
                      DEFINED := FALSE; NEXTLAB := FLABEL; LABNAME := LBNAME
                    END;
                  FLABEL := LLP
                END;
              INSYMBOL
            END
        ELSE ERROR(15);
        IF NOT ( SY IN FSYS + [COMMA, SEMICOLON] ) THEN
          BEGIN ERROR(6); SKIP(FSYS+[COMMA,SEMICOLON]) END;
        TEST := SY <> COMMA;
        IF NOT TEST THEN INSYMBOL
      UNTIL TEST;
      IF SY = SEMICOLON THEN INSYMBOL ELSE ERROR(14)
    END (* LABELDECLARATION *) ;

    PROCEDURE CONSTDECLARATION;
      VAR LCP: CTP; LSP: STP; LVALU: VALU;
    BEGIN
      IF SY <> IDENT THEN
        BEGIN ERROR(2); SKIP(FSYS + [IDENT]) END;
      WHILE SY = IDENT DO
        BEGIN NEW(LCP,KONST);
          WITH LCP^ DO
            BEGIN NAME := ID; IDTYPE := NIL; NEXT := NIL; KLASS:=KONST END;
          INSYMBOL;
          IF (SY = RELOP) AND (OP = EQOP) THEN INSYMBOL ELSE ERROR(16);
          CONSTANT(FSYS + [SEMICOLON],LSP,LVALU);
          ENTERID(LCP);
          LCP^.IDTYPE := LSP; LCP^.VALUES := LVALU;
          IF SY = SEMICOLON THEN
            BEGIN INSYMBOL;
              IF NOT (SY IN FSYS + [IDENT]) THEN
                BEGIN ERROR(6); SKIP(FSYS + [IDENT]) END
            END
          ELSE ERROR(14)
        END
    END (*CONSTDECLARATION*) ;

    PROCEDURE TYPEDECLARATION;
      VAR LCP,LCP1,LCP2: CTP; LSP: STP; LSIZE: ADDRRANGE;
    BEGIN
      IF SY <> IDENT THEN
        BEGIN ERROR(2); SKIP(FSYS + [IDENT]) END;
      WHILE SY = IDENT DO
        BEGIN NEW(LCP,TYPES);
          WITH LCP^ DO
            BEGIN NAME := ID; IDTYPE := NIL; KLASS := TYPES END;
          INSYMBOL;
          IF (SY = RELOP) AND (OP = EQOP) THEN INSYMBOL ELSE ERROR(16);
          TYP(FSYS + [SEMICOLON],LSP,LSIZE);
          ENTERID(LCP);
          LCP^.IDTYPE := LSP;
          (*HAS ANY FORWARD REFERENCE BEEN SATISFIED:*)
          LCP1 := FWPTR;
          WHILE LCP1 <> NIL DO
            BEGIN
              IF LCP1^.NAME = LCP^.NAME THEN
                BEGIN LCP1^.IDTYPE^.ELTYPE := LCP^.IDTYPE;
                  IF LCP1 <> FWPTR THEN
                    LCP2^.NEXT := LCP1^.NEXT
                  ELSE FWPTR := LCP1^.NEXT;
                END;
              LCP2 := LCP1; LCP1 := LCP1^.NEXT
            END;
          IF SY = SEMICOLON THEN
            BEGIN INSYMBOL;
              IF NOT (SY IN FSYS + [IDENT]) THEN
                BEGIN ERROR(6); SKIP(FSYS + [IDENT]) END
            END
          ELSE ERROR(14)
        END;
      IF FWPTR <> NIL THEN
        BEGIN ERROR(117); WRITELN(OUTPUT);
          REPEAT WRITELN(OUTPUT,' TYPE-ID ',FWPTR^.NAME);
            FWPTR := FWPTR^.NEXT
          UNTIL FWPTR = NIL;
          IF NOT EOL THEN WRITE(OUTPUT,' ': CHCNT+16)
        END
    END (*TYPEDECLARATION*) ;

    PROCEDURE VARDECLARATION;
      VAR LCP,NXT: CTP; LSP: STP; LSIZE: ADDRRANGE;
    BEGIN NXT := NIL;
      REPEAT
        REPEAT
          IF SY = IDENT THEN
            BEGIN NEW(LCP,VARS);
              WITH LCP^ DO
               BEGIN NAME := ID; NEXT := NXT; KLASS := VARS;
                  IDTYPE := NIL; VKIND := ACTUAL; VLEV := LEVEL
                END;
              ENTERID(LCP);
              NXT := LCP;
              INSYMBOL;
            END
          ELSE ERROR(2);
          IF NOT (SY IN FSYS + [COMMA,COLON] + TYPEDELS) THEN
            BEGIN ERROR(6); SKIP(FSYS+[COMMA,COLON,SEMICOLON]+TYPEDELS) END;
          TEST := SY <> COMMA;
          IF NOT TEST THEN INSYMBOL
        UNTIL TEST;
        IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
        TYP(FSYS + [SEMICOLON] + TYPEDELS,LSP,LSIZE);
        WHILE NXT <> NIL DO
          WITH  NXT^ DO
            BEGIN IDTYPE := LSP; VADDR := LC;
              LC := LC + LSIZE; NXT := NEXT
            END;
        IF SY = SEMICOLON THEN
          BEGIN INSYMBOL;
            IF NOT (SY IN FSYS + [IDENT]) THEN
              BEGIN ERROR(6); SKIP(FSYS + [IDENT]) END
          END
        ELSE ERROR(14)
      UNTIL (SY <> IDENT) AND NOT (SY IN TYPEDELS);
      IF FWPTR <> NIL THEN
        BEGIN ERROR(117); WRITELN(OUTPUT);
          REPEAT WRITELN(OUTPUT,' TYPE-ID ',FWPTR^.NAME);
            FWPTR := FWPTR^.NEXT
          UNTIL FWPTR = NIL;
          IF NOT EOL THEN WRITE(OUTPUT,' ': CHCNT+16)
        END
    END (*VARDECLARATION*) ;

    PROCEDURE PROCDECLARATION(FSY: SYMBOL);
      VAR OLDLEV: 0..MAXLEVEL; {LSY: SYMBOL;} LCP,LCP1: CTP; LSP: STP;
          FORW: BOOLEAN; OLDTOP: DISPRANGE; {PARCNT: INTEGER;}
          LLC,LCM: ADDRRANGE; LBNAME: INTEGER;
          MARKP: MARKTYPE; { CHANGED TO USE THE MARK TYPE FOR ROUTINES. [SAM] }

      PROCEDURE PARAMETERLIST(FSY: SETOFSYS; VAR FPAR: CTP);
        VAR LCP,LCP1,LCP2,LCP3: CTP; LSP: STP; LKIND: IDKIND;
          LLC,LEN : ADDRRANGE; COUNT : INTEGER;
      BEGIN LCP1 := NIL;
        IF NOT (SY IN FSY + [LPARENT]) THEN
          BEGIN ERROR(7); SKIP(FSYS + FSY + [LPARENT]) END;
        IF SY = LPARENT THEN
          BEGIN IF FORW THEN ERROR(119);
            INSYMBOL;
            IF NOT (SY IN [IDENT,VARSY,PROCSY,FUNCSY]) THEN
              BEGIN ERROR(7); SKIP(FSYS + [IDENT,RPARENT]) END;
            WHILE SY IN [IDENT,VARSY,PROCSY,FUNCSY] DO
              BEGIN
                IF SY = PROCSY THEN
                  BEGIN ERROR(399);
                    REPEAT INSYMBOL;
                      IF SY = IDENT THEN
                      BEGIN NEW(LCP,PROC,DECLARED,FORMAL);
                          WITH LCP^ DO
                            BEGIN NAME := ID; IDTYPE := NIL; NEXT := LCP1;
                              PFLEV := LEVEL (*BEWARE OF PARAMETER PROCEDURES*);
                              KLASS:=PROC;PFDECKIND:=DECLARED;PFKIND:=FORMAL
                            END;
                          ENTERID(LCP);
                          LCP1 := LCP; LC := LC + PTRSIZE;
                          INSYMBOL
                        END
                      ELSE ERROR(2);
                      IF NOT (SY IN FSYS + [COMMA,SEMICOLON,RPARENT]) THEN
                        BEGIN ERROR(7);SKIP(FSYS+[COMMA,SEMICOLON,RPARENT])END
                    UNTIL SY <> COMMA
                  END
                ELSE
                  BEGIN
                    IF SY = FUNCSY THEN
                      BEGIN ERROR(399); LCP2 := NIL;
                        REPEAT INSYMBOL;
                          IF SY = IDENT THEN
                            BEGIN NEW(LCP,FUNC,DECLARED,FORMAL);
                              WITH LCP^ DO
                                BEGIN NAME := ID; IDTYPE := NIL; NEXT := LCP2;
                                  PFLEV := LEVEL (*BEWARE PARAM FUNCS*);
                                  KLASS:=FUNC;PFDECKIND:=DECLARED;
                                  PFKIND:=FORMAL
                                END;
                              ENTERID(LCP);
                              LCP2 := LCP; LC := LC + PTRSIZE;
                              INSYMBOL;
                            END;
                          IF NOT (SY IN [COMMA,COLON] + FSYS) THEN
                           BEGIN ERROR(7);SKIP(FSYS+[COMMA,SEMICOLON,RPARENT])
                            END
                        UNTIL SY <> COMMA;
                        IF SY = COLON THEN
                          BEGIN INSYMBOL;
                            IF SY = IDENT THEN
                              BEGIN SEARCHID([TYPES],LCP);
                                LSP := LCP^.IDTYPE;
                                IF LSP <> NIL THEN
                                 IF NOT(LSP^.FORM IN[SCALAR,SUBRANGE,POINTER])
                                    THEN BEGIN ERROR(120); LSP := NIL END;
                                LCP3 := LCP2;
                                WHILE LCP2 <> NIL DO
                                  BEGIN LCP2^.IDTYPE := LSP; LCP := LCP2;
                                    LCP2 := LCP2^.NEXT
                                  END;
                                LCP^.NEXT := LCP1; LCP1 := LCP3;
                                INSYMBOL
                              END
                            ELSE ERROR(2);
                            IF NOT (SY IN FSYS + [SEMICOLON,RPARENT]) THEN
                              BEGIN ERROR(7);SKIP(FSYS+[SEMICOLON,RPARENT])END
                          END
                        ELSE ERROR(5)
                      END
                    ELSE
                      BEGIN
                        IF SY = VARSY THEN
                          BEGIN LKIND := FORMAL; INSYMBOL END
                        ELSE LKIND := ACTUAL;
                        LCP2 := NIL;
                        COUNT := 0;
                        REPEAT
                          IF SY = IDENT THEN
                            BEGIN NEW(LCP,VARS);
                              WITH LCP^ DO
                                BEGIN NAME:=ID; IDTYPE:=NIL; KLASS:=VARS;
                                  VKIND := LKIND; NEXT := LCP2; VLEV := LEVEL;
                                END;
                              ENTERID(LCP);
                              LCP2 := LCP; COUNT := COUNT+1;
                              INSYMBOL;
                            END;
                          IF NOT (SY IN [COMMA,COLON] + FSYS) THEN
                           BEGIN ERROR(7);SKIP(FSYS+[COMMA,SEMICOLON,RPARENT])
                            END;
                          TEST := SY <> COMMA;
                          IF NOT TEST THEN INSYMBOL
                        UNTIL TEST;
                        IF SY = COLON THEN
                          BEGIN INSYMBOL;
                            IF SY = IDENT THEN
                              BEGIN SEARCHID([TYPES],LCP);
                                LSP := LCP^.IDTYPE;
                                IF LSP <> NIL THEN
                                  IF (LKIND=ACTUAL)AND(LSP^.FORM=FILES) THEN
                                    ERROR(121);
                                LCP3 := LCP2;
                                IF (LKIND=ACTUAL) AND (LSP^.SIZE<=PTRSIZE)
                                THEN LEN := LSP^.SIZE
                                ELSE LEN := PTRSIZE;
                                LC := LC+COUNT*LEN;
                                LLC := LC;
                                WHILE LCP2 <> NIL DO
                                  BEGIN LCP := LCP2;
                                    WITH LCP2^ DO
                                      BEGIN IDTYPE := LSP; LLC := LLC-LEN;
                                        VADDR := LLC;
                                      END;
                                    LCP2 := LCP2^.NEXT
                                  END;
                                LCP^.NEXT := LCP1; LCP1 := LCP3;
                                INSYMBOL
                              END
                            ELSE ERROR(2);
                            IF NOT (SY IN FSYS + [SEMICOLON,RPARENT]) THEN
                              BEGIN ERROR(7);SKIP(FSYS+[SEMICOLON,RPARENT])END
                          END
                        ELSE ERROR(5);
                      END;
                  END;
                IF SY = SEMICOLON THEN
                  BEGIN INSYMBOL;
                    IF NOT (SY IN FSYS + [IDENT,VARSY,PROCSY,FUNCSY]) THEN
                      BEGIN ERROR(7); SKIP(FSYS + [IDENT,RPARENT]) END
                  END
              END (*WHILE*) ;
            IF SY = RPARENT THEN
              BEGIN INSYMBOL;
                IF NOT (SY IN FSY + FSYS) THEN
                  BEGIN ERROR(6); SKIP(FSY + FSYS) END
              END
            ELSE ERROR(4);
            LCP3 := NIL;
            (*REVERSE POINTERS AND RESERVE LOCAL CELLS FOR COPIES OF MULTIPLE
             VALUES*)
            WHILE LCP1 <> NIL DO
              WITH LCP1^ DO
                BEGIN LCP2 := NEXT; NEXT := LCP3;
                  IF KLASS = VARS THEN
                    IF IDTYPE <> NIL THEN
                      IF (VKIND = ACTUAL) AND (IDTYPE^.SIZE > PTRSIZE) THEN
                        BEGIN VADDR := LC; LC := LC + IDTYPE^.SIZE
                        END;
                  LCP3 := LCP1; LCP1 := LCP2
                END;
            FPAR := LCP3
          END
            ELSE FPAR := NIL
    END (*PARAMETERLIST*) ;

    BEGIN (*PROCDECLARATION*)
      LLC := LC; LC := LCAFTERMARKSTACK;
      IF SY = IDENT THEN
        BEGIN SEARCHSECTION(DISPLAY[TOP].FNAME,LCP); (*DECIDE WHETHER FORW.*)
          IF LCP <> NIL THEN
          BEGIN
            IF LCP^.KLASS = PROC THEN
              FORW := LCP^.FORWDECL AND(FSY = PROCSY)AND(LCP^.PFKIND = ACTUAL)
            ELSE
              IF LCP^.KLASS = FUNC THEN
                FORW:=LCP^.FORWDECL AND(FSY=FUNCSY)AND(LCP^.PFKIND=ACTUAL)
              ELSE FORW := FALSE;
            IF NOT FORW THEN ERROR(160)
          END
          ELSE FORW := FALSE;
          IF NOT FORW THEN
            BEGIN
              IF FSY = PROCSY THEN NEW(LCP,PROC,DECLARED,ACTUAL)
              ELSE NEW(LCP,FUNC,DECLARED,ACTUAL);
              WITH LCP^ DO
                BEGIN NAME := ID; IDTYPE := NIL;
                  EXTERN := FALSE; PFLEV := LEVEL; GENLABEL(LBNAME);
                  PFDECKIND := DECLARED; PFKIND := ACTUAL; PFNAME := LBNAME;
                  IF FSY = PROCSY THEN KLASS := PROC
                  ELSE KLASS := FUNC
                END;
              ENTERID(LCP)
            END
          ELSE
            BEGIN LCP1 := LCP^.NEXT;
              WHILE LCP1 <> NIL DO
                BEGIN
                  WITH LCP1^ DO
                    IF KLASS = VARS THEN
                      IF IDTYPE <> NIL THEN
                        BEGIN LCM := VADDR + IDTYPE^.SIZE;
                          IF LCM > LC THEN LC := LCM
                        END;
                  LCP1 := LCP1^.NEXT
                END
              END;
          INSYMBOL
        END
      ELSE ERROR(2);
      OLDLEV := LEVEL; OLDTOP := TOP;
      IF LEVEL < MAXLEVEL THEN LEVEL := LEVEL + 1 ELSE ERROR(251);
      IF TOP < DISPLIMIT THEN
        BEGIN TOP := TOP + 1;
          WITH DISPLAY[TOP] DO
            BEGIN
              IF FORW THEN FNAME := LCP^.NEXT
              ELSE FNAME := NIL;
              FLABEL := NIL;
              OCCUR := BLCK
            END
        END
      ELSE ERROR(250);
      IF FSY = PROCSY THEN
        BEGIN PARAMETERLIST([SEMICOLON],LCP1);
          IF NOT FORW THEN LCP^.NEXT := LCP1
        END
      ELSE
        BEGIN PARAMETERLIST([SEMICOLON,COLON],LCP1);
          IF NOT FORW THEN LCP^.NEXT := LCP1;
          IF SY = COLON THEN
            BEGIN INSYMBOL;
              IF SY = IDENT THEN
                BEGIN IF FORW THEN ERROR(122);
                  SEARCHID([TYPES],LCP1);
                  LSP := LCP1^.IDTYPE;
                  LCP^.IDTYPE := LSP;
                  IF LSP <> NIL THEN
                    IF NOT (LSP^.FORM IN [SCALAR,SUBRANGE,POINTER]) THEN
                      BEGIN ERROR(120); LCP^.IDTYPE := NIL END;
                  INSYMBOL
                END
              ELSE BEGIN ERROR(2); SKIP(FSYS + [SEMICOLON]) END
            END
          ELSE
            IF NOT FORW THEN ERROR(123)
        END;
      IF SY = SEMICOLON THEN INSYMBOL ELSE ERROR(14);
      IF SY = FORWARDSY THEN
        BEGIN
          IF FORW THEN ERROR(161)
          ELSE LCP^.FORWDECL := TRUE;
          INSYMBOL;
          IF SY = SEMICOLON THEN INSYMBOL ELSE ERROR(14);
          IF NOT (SY IN FSYS) THEN
            BEGIN ERROR(6); SKIP(FSYS) END
        END
      ELSE
        BEGIN LCP^.FORWDECL := FALSE; MARK(MARKP); (* MARK HEAP *)
          REPEAT BLOCK(FSYS,SEMICOLON,LCP);
            IF SY = SEMICOLON THEN
              BEGIN IF PRTABLES THEN PRINTTABLES(FALSE); INSYMBOL;
                IF NOT (SY IN [BEGINSY,PROCSY,FUNCSY]) THEN
                  BEGIN ERROR(6); SKIP(FSYS) END
              END
            ELSE ERROR(14)
          UNTIL SY IN [BEGINSY,PROCSY,FUNCSY];
          RELEASE(MARKP); (* RETURN LOCAL ENTRIES ON RUNTIME HEAP *)
        END;
      LEVEL := OLDLEV; TOP := OLDTOP; LC := LLC;
    END (*PROCDECLARATION*) ;

    PROCEDURE BODY(FSYS: SETOFSYS);
      CONST CSTOCCMAX = 60; CIXMAX = 1000;
      TYPE OPRANGE = 0..63;
      VAR
          LLCP:CTP; SAVEID:ALPHA;
          CSTPTR: ARRAY [1..CSTOCCMAX] OF CSP;
          CSTPTRIX: 0..CSTOCCMAX;
          (*ALLOWS REFERENCING OF NONINTEGER CONSTANTS BY AN INDEX
           (INSTEAD OF A POINTER), WHICH CAN BE STORED IN THE P2-FIELD
           OF THE INSTRUCTION RECORD UNTIL WRITEOUT.
           --> PROCEDURE LOAD, PROCEDURE WRITEOUT*)
          {I, }ENTNAME, SEGSIZE: INTEGER;
          LCMAX,LLC1: ADDRRANGE; LCP: CTP;
          LLP: LBP;


      PROCEDURE PUTIC;
      BEGIN IF IC MOD 10 = 0 THEN WRITELN(PRR,'I',IC:5) END;


      PROCEDURE GEN0(FOP: OPRANGE);
      BEGIN
        IF PRCODE THEN BEGIN PUTIC; WRITELN(PRR,MN[FOP]:4) END;
        IC := IC + 1
      END (*GEN0*) ;

      PROCEDURE GEN1(FOP: OPRANGE; FP2: INTEGER);
        VAR K: INTEGER;
      BEGIN
        IF PRCODE THEN
          BEGIN PUTIC; WRITE(PRR,MN[FOP]:4);
            IF FOP = 30 THEN WRITELN(PRR,SNA[FP2]:12)
            ELSE IF FOP = 38 THEN
                   BEGIN WRITE(PRR,'''');
                     WITH CSTPTR[FP2]^ DO
                       FOR K := 1 TO SLGTH DO WRITE(PRR,SVAL[K]:1);
                     WRITELN(PRR,'''')
                   END
                 ELSE IF FOP = 42 THEN WRITELN(PRR,CHR(FP2))
                      ELSE WRITELN(PRR,FP2:12)
          END;
        IC := IC + 1
      END (*GEN1*) ;

      PROCEDURE GEN2(FOP: OPRANGE; FP1,FP2: INTEGER);
        VAR K : INTEGER;
      BEGIN
        IF PRCODE THEN
          BEGIN PUTIC; WRITE(PRR,MN[FOP]:4);
            CASE FOP OF
              45,50,54,56:
                WRITELN(PRR,' ',FP1:3,FP2:8);
              47,48,49,52,53,55:
                BEGIN WRITE(PRR,CHR(FP1));
                  IF CHR(FP1) = 'M' THEN WRITE(PRR,FP2:11);
                  WRITELN(PRR)
                END;
              51:
                CASE FP1 OF
                  1: WRITELN(PRR,'I ',FP2);
                  2: BEGIN WRITE(PRR,'R ');
                       WITH CSTPTR[FP2]^ DO
                         FOR K := 1 TO STRGLGTH DO WRITE(PRR,RVAL[K]);
                       WRITELN(PRR)
                     END;
                  3: WRITELN(PRR,'B ',FP2);
                  4: WRITELN(PRR,'N');
                  5: BEGIN WRITE(PRR,'(');
                       WITH CSTPTR[FP2]^ DO
                         FOR K := 0 TO 58 DO
                           IF K IN PVAL THEN WRITE(PRR,K:3);
                       WRITELN(PRR,')')
                     END
                END
            END;
          END;
          IC := IC + 1
      END (*GEN2*) ;

      PROCEDURE LOAD;
      BEGIN
        WITH GATTR DO
          IF TYPTR <> NIL THEN
            BEGIN
              CASE KIND OF
                CST:   IF (TYPTR^.FORM = SCALAR) AND (TYPTR <> REALPTR) THEN
                         IF TYPTR = BOOLPTR THEN GEN2(51(*LDC*),3,CVAL.IVAL)
                         ELSE GEN2(51(*LDC*),1,CVAL.IVAL)
                       ELSE
                         IF TYPTR = NILPTR THEN GEN2(51(*LDC*),4,0)
                         ELSE
                           IF CSTPTRIX >= CSTOCCMAX THEN ERROR(254)
                           ELSE
                             BEGIN CSTPTRIX := CSTPTRIX + 1;
                               CSTPTR[CSTPTRIX] := CVAL.VALP;
                               IF TYPTR = REALPTR THEN
                                 GEN2(51(*LDC*),2,CSTPTRIX)
                               ELSE
                                  GEN2(51(*LDC*),5,CSTPTRIX)
                             END;
                VARBL: CASE ACCESS OF
                         DRCT:   IF VLEVEL <= 1 THEN GEN1(39(*LDO*),DPLMT)
                                 ELSE GEN2(54(*LOD*),LEVEL-VLEVEL,DPLMT);
                         INDRCT: GEN1(35(*IND*),IDPLMT);
                         INXD:   ERROR(400)
                       END;
                EXPR:
              END;
              KIND := EXPR
            END
      END (*LOAD*) ;

      PROCEDURE STORE(VAR FATTR: ATTR);
      BEGIN
        WITH FATTR DO
          IF TYPTR <> NIL THEN
            CASE ACCESS OF
              DRCT:   IF VLEVEL <= 1 THEN GEN1(43(*SRO*),DPLMT)
                      ELSE GEN2(56(*STR*),LEVEL-VLEVEL,DPLMT);
              INDRCT: IF IDPLMT <> 0 THEN ERROR(400)
                      ELSE GEN0(26(*STO*));
              INXD:   ERROR(400)
            END
      END (*STORE*) ;

      PROCEDURE LOADADDRESS;
      BEGIN
        WITH GATTR DO
          IF TYPTR <> NIL THEN
            BEGIN
              CASE KIND OF
                CST:   IF STRING(TYPTR) THEN
                         IF CSTPTRIX >= CSTOCCMAX THEN ERROR(254)
                         ELSE
                           BEGIN CSTPTRIX := CSTPTRIX + 1;
                             CSTPTR[CSTPTRIX] := CVAL.VALP;
                             GEN1(38(*LCA*),CSTPTRIX)
                           END
                       ELSE ERROR(400);
                VARBL: CASE ACCESS OF
                         DRCT:   IF VLEVEL <= 1 THEN GEN1(37(*LAO*),DPLMT)
                                 ELSE GEN2(50(*LDA*),LEVEL-VLEVEL,DPLMT);
                         INDRCT: IF IDPLMT <> 0 THEN GEN1(34(*INC*),IDPLMT);
                         INXD:   ERROR(400)
                       END;
                EXPR:  ERROR(400)
              END;
              KIND := VARBL; ACCESS := INDRCT; IDPLMT := 0
            END
      END (*LOADADDRESS*) ;


      PROCEDURE GENFJP(FADDR: INTEGER);
      BEGIN LOAD;
        IF GATTR.TYPTR <> NIL THEN
          IF GATTR.TYPTR <> BOOLPTR THEN ERROR(144);
        IF PRCODE THEN BEGIN PUTIC; WRITELN(PRR,MN[33]:4,' L':8,FADDR:4) END;
        IC := IC + 1
      END (*GENFJP*) ;

      PROCEDURE GENUJPENT(FOP: OPRANGE; FP2: INTEGER);
     BEGIN
       IF PRCODE THEN
          BEGIN PUTIC; WRITELN(PRR, MN[FOP]:4, ' L':8,FP2:4) END;
        IC := IC + 1
      END (*GENUJPENT*);


      PROCEDURE GENCUP(FP1, FP2: INTEGER);
     BEGIN
       IF PRCODE THEN
          BEGIN PUTIC; WRITELN(PRR, MN[46]:4, FP1:4, ' L':4, FP2:4) END;
        IC := IC + 1
      END (*GENCUP*);


      PROCEDURE PUTLABEL(LABNAME: INTEGER);
      BEGIN IF PRCODE THEN WRITELN(PRR, 'L', LABNAME:4)
      END (*PUTLABEL*);

      PROCEDURE STATEMENT(FSYS: SETOFSYS);
        LABEL 1;
        VAR LCP: CTP; LLP: LBP;

        PROCEDURE EXPRESSION(FSYS: SETOFSYS); FORWARD;

        PROCEDURE SELECTOR(FSYS: SETOFSYS; FCP: CTP);
          VAR LATTR: ATTR; LCP: CTP; LMIN,LMAX: INTEGER;
        BEGIN
          WITH FCP^, GATTR DO
            BEGIN TYPTR := IDTYPE; KIND := VARBL;
              CASE KLASS OF
                VARS:
                  IF VKIND = ACTUAL THEN
                    BEGIN ACCESS := DRCT; VLEVEL := VLEV;
                      DPLMT := VADDR
                    END
                  ELSE
                    BEGIN GEN2(54(*LOD*),LEVEL-VLEV,VADDR);
                      ACCESS := INDRCT; IDPLMT := 0
                    END;
                FIELD:
                  WITH DISPLAY[DISX] DO
                    IF OCCUR = CREC THEN
                      BEGIN ACCESS := DRCT; VLEVEL := CLEV;
                        DPLMT := CDSPL + FLDADDR
                      END
                    ELSE
                      BEGIN
                        IF LEVEL = 1 THEN GEN1(39(*LDO*),VDSPL)
                        ELSE GEN2(54(*LOD*),0,VDSPL);
                        ACCESS := INDRCT; IDPLMT := FLDADDR
                      END;
                FUNC:
                  IF PFDECKIND = STANDARD THEN ERROR(150)
                  ELSE
                    IF PFLEV = 0 THEN ERROR(150)   (*EXTERNAL FCT*)
                    ELSE
                      IF PFKIND = FORMAL THEN ERROR(151)
                      ELSE
                        BEGIN ACCESS := DRCT; VLEVEL := PFLEV + 1;
                          DPLMT := 0   (*IMPL. RELAT. ADDR. OF FCT. RESULT*)
                        END
              END (*CASE*)
            END (*WITH*);
          IF NOT (SY IN SELECTSYS + FSYS) THEN
            BEGIN ERROR(59); SKIP(SELECTSYS + FSYS) END;
          WHILE SY IN SELECTSYS DO
            BEGIN
        (*[*)   IF SY = LBRACK THEN
                BEGIN
                  REPEAT LATTR := GATTR;
                    WITH LATTR DO
                      IF TYPTR <> NIL THEN
                        IF TYPTR^.FORM <> ARRAYS THEN
                          BEGIN ERROR(138); TYPTR := NIL END;
                    LOADADDRESS;
                    INSYMBOL; EXPRESSION(FSYS + [COMMA,RBRACK]);
                    LOAD;
                    IF GATTR.TYPTR <> NIL THEN
                      IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(113);
                    IF LATTR.TYPTR <> NIL THEN
                      WITH LATTR.TYPTR^ DO
                        BEGIN
                          IF COMPTYPES(INXTYPE,GATTR.TYPTR) THEN
                            BEGIN
                              IF INXTYPE <> NIL THEN
                                BEGIN GETBOUNDS(INXTYPE,LMIN,LMAX);
                                  IF LMIN > 0 THEN GEN1(31(*DEC*),LMIN)
                                  ELSE IF LMIN < 0 THEN GEN1(34(*INC*),-LMIN)
                                  (*OR SIMPLY GEN1(31,LMIN)*)
                                END
                            END
                          ELSE ERROR(139);
                          WITH GATTR DO
                            BEGIN TYPTR := AELTYPE; KIND := VARBL;
                              ACCESS := INDRCT; IDPLMT := 0
                            END;
                          IF GATTR.TYPTR <> NIL THEN
                            GEN1(36(*IXA*),GATTR.TYPTR^.SIZE)
                        END
                  UNTIL SY <> COMMA;
                  IF SY = RBRACK THEN INSYMBOL ELSE ERROR(12)
                END (*IF SY = LBRACK*)
              ELSE
        (*.*)     IF SY = PERIOD THEN
                  BEGIN
                    WITH GATTR DO
                      BEGIN
                        IF TYPTR <> NIL THEN
                          IF TYPTR^.FORM <> RECORDS THEN
                            BEGIN ERROR(140); TYPTR := NIL END;
                        INSYMBOL;
                        IF SY = IDENT THEN
                          BEGIN
                            IF TYPTR <> NIL THEN
                              BEGIN SEARCHSECTION(TYPTR^.FSTFLD,LCP);
                                IF LCP = NIL THEN
                                  BEGIN ERROR(152); TYPTR := NIL END
                                ELSE
                                  WITH LCP^ DO
                                    BEGIN TYPTR := IDTYPE;
                                      CASE ACCESS OF

                    DRCT:   DPLMT := DPLMT + FLDADDR;
                                        INDRCT: IDPLMT := IDPLMT + FLDADDR;
                                        INXD:   ERROR(400)
                                      END
                                    END
                              END;
                            INSYMBOL
                          END (*SY = IDENT*)
                        ELSE ERROR(2)
                      END (*WITH GATTR*)
                  END (*IF SY = PERIOD*)
                ELSE
        (*^*)       BEGIN
                    IF GATTR.TYPTR <> NIL THEN
                      WITH GATTR,TYPTR^ DO
                        IF FORM = POINTER THEN
                          BEGIN TYPTR := ELTYPE; LOAD;
                            WITH GATTR DO
                              BEGIN KIND := VARBL; ACCESS := INDRCT;
                                IDPLMT := 0
                              END
                          END
                        ELSE
                          IF FORM = FILES THEN TYPTR := FILTYPE
                          ELSE ERROR(141);
                    INSYMBOL
                  END;
              IF NOT (SY IN FSYS + SELECTSYS) THEN
                BEGIN ERROR(6); SKIP(FSYS + SELECTSYS) END
            END (*WHILE*)
        END (*SELECTOR*) ;

        PROCEDURE CALL(FSYS: SETOFSYS; FCP: CTP);
          VAR LKEY: 1..15;

          PROCEDURE VARIABLE(FSYS: SETOFSYS);
            VAR LCP: CTP;
          BEGIN
            IF SY = IDENT THEN
              BEGIN SEARCHID([VARS,FIELD],LCP); INSYMBOL END
            ELSE BEGIN ERROR(2); LCP := UVARPTR END;
            SELECTOR(FSYS,LCP)
          END (*VARIABLE*) ;

          PROCEDURE GETPUTRESETREWRITE;
          BEGIN VARIABLE(FSYS + [RPARENT]); LOADADDRESS;
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> FILES THEN ERROR(116);
            IF LKEY <= 2 THEN GEN1(30(*CSP*),LKEY(*GET,PUT*))
            ELSE ERROR(399)
          END (*GETPUTRESETREWRITE*) ;

          PROCEDURE READ;
            VAR LCP:CTP; LLEV:LEVRANGE; LADDR:ADDRRANGE;
          BEGIN
            IF SY = IDENT THEN
              BEGIN SEARCHID([VARS],LCP);
                IF LCP <> NIL THEN
                  IF LCP^.IDTYPE^.FORM = FILES THEN
                    WITH LCP^ DO
                      BEGIN
                        IF IDTYPE^.FILTYPE = CHARPTR THEN
                          BEGIN LLEV := VLEV; LADDR := VADDR END
                        ELSE ERROR(399);
                        INSYMBOL;
                        IF NOT (SY IN [COMMA,RPARENT]) THEN ERROR(20)
                      END
                  ELSE BEGIN LLEV := 1; LADDR := LCAFTERMARKSTACK END
                ELSE BEGIN LLEV := 1; LADDR := LCAFTERMARKSTACK END
              END
            ELSE BEGIN ERROR(2); LLEV := 1; LADDR := LCAFTERMARKSTACK;
                   INSYMBOL
                 END;
           IF SY = COMMA THEN INSYMBOL;
           IF SY = IDENT THEN
           BEGIN
            REPEAT VARIABLE(FSYS + [COMMA,RPARENT]); LOADADDRESS;
              GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
              IF GATTR.TYPTR <> NIL THEN
                IF GATTR.TYPTR^.FORM <= SUBRANGE THEN
                  IF COMPTYPES(INTPTR,GATTR.TYPTR) THEN
                    GEN1(30(*CSP*),3(*RDI*))
                  ELSE
                    IF COMPTYPES(REALPTR,GATTR.TYPTR) THEN
                      GEN1(30(*CSP*),4(*RDR*))
                    ELSE
                      IF COMPTYPES(CHARPTR,GATTR.TYPTR) THEN
                        GEN1(30(*CSP*),5(*RDC*))
                      ELSE ERROR(399)
                ELSE ERROR(116);
              TEST := SY <> COMMA;
              IF NOT TEST THEN INSYMBOL
            UNTIL TEST
           END;
           IF LKEY = 11 THEN
             BEGIN GEN2(50(*LDA*),LEVEL - LLEV, LADDR);
               GEN1(30(*CSP*),21(*RLN*))
             END
          END (*READ*) ;

          PROCEDURE WRITE;
            VAR LSP: STP; DEFAULT : BOOLEAN; LLKEY: 1..15;
                LCP:CTP; LLEV:LEVRANGE; LADDR,LEN:ADDRRANGE;
          BEGIN LLKEY := LKEY;
            IF SY = IDENT THEN
              BEGIN SEARCHID([VARS],LCP);
                IF LCP <> NIL THEN
                  IF LCP^.IDTYPE^.FORM = FILES THEN
                    WITH LCP^ DO
                      BEGIN
                        IF IDTYPE^.FILTYPE = CHARPTR THEN
                          BEGIN LLEV := VLEV; LADDR := VADDR END
                        ELSE ERROR(399);
                        INSYMBOL;
                        IF NOT (SY IN [COMMA,RPARENT]) THEN ERROR(20)
                      END
                  ELSE BEGIN LLEV := 1; LADDR := LCAFTERMARKSTACK+CHARSIZE
                       END
                ELSE BEGIN LLEV := 1; LADDR := LCAFTERMARKSTACK+CHARSIZE END
              END
            ELSE BEGIN LLEV := 1; LADDR := LCAFTERMARKSTACK+CHARSIZE END;
           IF SY = COMMA THEN INSYMBOL;
           IF SY IN FACBEGSYS THEN
           BEGIN
            REPEAT EXPRESSION(FSYS + [COMMA,COLON,RPARENT]);
              LSP := GATTR.TYPTR;
              IF LSP <> NIL THEN
                IF LSP^.FORM <= SUBRANGE THEN LOAD ELSE LOADADDRESS;
              IF SY = COLON THEN
                BEGIN INSYMBOL; EXPRESSION(FSYS + [COMMA,COLON,RPARENT]);
                  IF GATTR.TYPTR <> NIL THEN
                    IF GATTR.TYPTR <> INTPTR THEN ERROR(116);
                  LOAD; DEFAULT := FALSE
                END
              ELSE DEFAULT := TRUE;
              IF SY = COLON THEN
                BEGIN INSYMBOL; EXPRESSION(FSYS + [COMMA,RPARENT]);
                  IF GATTR.TYPTR <> NIL THEN
                    IF GATTR.TYPTR <> INTPTR THEN ERROR(116);
                  IF LSP <> REALPTR THEN ERROR(124);
                  LOAD; ERROR(399);
                END
              ELSE
                IF LSP = INTPTR THEN
                  BEGIN IF DEFAULT THEN GEN2(51(*LDC*),1,10);
                    GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
                    GEN1(30(*CSP*),6(*WRI*))
                  END
                ELSE
                  IF LSP = REALPTR THEN
                    BEGIN IF DEFAULT THEN GEN2(51(*LDC*),1,20);
                      GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
                      GEN1(30(*CSP*),8(*WRR*))
                    END
                  ELSE
                    IF LSP = CHARPTR THEN
                      BEGIN IF DEFAULT THEN GEN2(51(*LDC*),1,1);
                        GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
                        GEN1(30(*CSP*),9(*WRC*))
                      END
                    ELSE
                      IF LSP <> NIL THEN
                        BEGIN
                          IF LSP^.FORM = SCALAR THEN ERROR(399)
                          ELSE
                            IF STRING(LSP) THEN
                              BEGIN LEN := LSP^.SIZE DIV CHARSIZE;
                                IF DEFAULT THEN
                                      GEN2(51(*LDC*),1,LEN);
                                GEN2(51(*LDC*),1,LEN);
                                GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
                                GEN1(30(*CSP*),10(*WRS*))
                              END
                            ELSE ERROR(116)
                        END;
              TEST := SY <> COMMA;
              IF NOT TEST THEN INSYMBOL
            UNTIL TEST;
           END;
            IF LLKEY = 12 THEN (*WRITELN*)
              BEGIN GEN2(50(*LDA*),LEVEL-LLEV,LADDR);
                GEN1(30(*CSP*),22(*WLN*))
              END
          END (*WRITE*) ;

          PROCEDURE PACK;
            VAR LSP,LSP1: STP;
          BEGIN ERROR(399); VARIABLE(FSYS + [COMMA,RPARENT]);
            LSP := NIL; LSP1 := NIL;
            IF GATTR.TYPTR <> NIL THEN
              WITH GATTR.TYPTR^ DO
                IF FORM = ARRAYS THEN
                  BEGIN LSP := INXTYPE; LSP1 := AELTYPE END
                ELSE ERROR(116);
            IF SY = COMMA THEN INSYMBOL ELSE ERROR(20);
            EXPRESSION(FSYS + [COMMA,RPARENT]);
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(116)
              ELSE
                IF NOT COMPTYPES(LSP,GATTR.TYPTR) THEN ERROR(116);
            IF SY = COMMA THEN INSYMBOL ELSE ERROR(20);
            VARIABLE(FSYS + [RPARENT]);
            IF GATTR.TYPTR <> NIL THEN
              WITH GATTR.TYPTR^ DO
                IF FORM = ARRAYS THEN
                  BEGIN
                    IF NOT COMPTYPES(AELTYPE,LSP1)
                      OR NOT COMPTYPES(INXTYPE,LSP) THEN
                      ERROR(116)
                  END
                ELSE ERROR(116)
          END (*PACK*) ;

          PROCEDURE UNPACK;
            VAR LSP,LSP1: STP;
          BEGIN ERROR(399); VARIABLE(FSYS + [COMMA,RPARENT])
;
            LSP := NIL; LSP1 := NIL;
            IF GATTR.TYPTR <> NIL THEN
              WITH GATTR.TYPTR^ DO
                IF FORM = ARRAYS THEN
                  BEGIN LSP := INXTYPE; LSP1 := AELTYPE END
                ELSE ERROR(116);
            IF SY = COMMA THEN INSYMBOL ELSE ERROR(20);
            VARIABLE(FSYS + [COMMA,RPARENT]);
            IF GATTR.TYPTR <> NIL THEN
              WITH GATTR.TYPTR^ DO
                IF FORM = ARRAYS THEN
                  BEGIN
                    IF NOT COMPTYPES(AELTYPE,LSP1)
                      OR NOT COMPTYPES(INXTYPE,LSP) THEN
                      ERROR(116)
                  END
                ELSE ERROR(116);
            IF SY = COMMA THEN INSYMBOL ELSE ERROR(20);
            EXPRESSION(FSYS + [RPARENT]);
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(116)
              ELSE
                IF NOT COMPTYPES(LSP,GATTR.TYPTR) THEN ERROR(116);
          END (*UNPACK*) ;

          PROCEDURE NEW;
            LABEL 1;
            VAR LSP,LSP1: STP; VARTS{,LMIN,LMAX}: INTEGER;
                LSIZE{,LSZ}: ADDRRANGE; LVAL: VALU;
          BEGIN VARIABLE(FSYS + [COMMA,RPARENT]); LOADADDRESS;
            LSP := NIL; VARTS := 0; LSIZE := 0;
            IF GATTR.TYPTR <> NIL THEN
              WITH GATTR.TYPTR^ DO
                IF FORM = POINTER THEN
                  BEGIN
                    IF ELTYPE <> NIL THEN
                      BEGIN LSIZE := ELTYPE^.SIZE;
                        IF ELTYPE^.FORM = RECORDS THEN LSP := ELTYPE^.RECVAR
                      END
                  END
                ELSE ERROR(116);
            WHILE SY = COMMA DO
              BEGIN INSYMBOL;CONSTANT(FSYS + [COMMA,RPARENT],LSP1,LVAL);
                VARTS := VARTS + 1;
                (*CHECK TO INSERT HERE: IS CONSTANT IN TAGFIELDTYPE RANGE*)
                IF LSP = NIL THEN ERROR(158)
                ELSE
                  IF LSP^.FORM <> TAGFLD THEN ERROR(162)
                  ELSE
                    IF LSP^.TAGFIELDP <> NIL THEN
                      IF STRING(LSP1) OR (LSP1 = REALPTR) THEN ERROR(159)
                      ELSE
                        IF COMPTYPES(LSP^.TAGFIELDP^.IDTYPE,LSP1) THEN
                          BEGIN
                            LSP1 := LSP^.FSTVAR;
                            WHILE LSP1 <> NIL DO
                              WITH LSP1^ DO
                                IF VARVAL.IVAL = LVAL.IVAL THEN
                                  BEGIN LSIZE := SIZE; LSP := SUBVAR;
                                    GOTO 1
                                  END
                                ELSE LSP1 := NXTVAR;
                            LSIZE := LSP^.SIZE; LSP := NIL;
                          END
                        ELSE ERROR(116);
          1:  END (*WHILE*) ;
            GEN2(51(*LDC*),1,LSIZE);
            GEN1(30(*CSP*),12(*NEW*));
          END (*NEW*) ;

          PROCEDURE MARK;
          BEGIN VARIABLE(FSYS+[RPARENT]);
             IF GATTR.TYPTR <> NIL THEN
               IF GATTR.TYPTR^.FORM = POINTER THEN
                 BEGIN LOADADDRESS; GEN1(30(*CSP*),23(*SAV*)) END
               ELSE ERROR(125)
          END(*MARK*);

          PROCEDURE RELEASE;
          BEGIN VARIABLE(FSYS+[RPARENT]);
                IF GATTR.TYPTR <> NIL THEN
                   IF GATTR.TYPTR^.FORM = POINTER THEN
                      BEGIN LOAD; GEN1(30(*CSP*),13(*RST*)) END
                   ELSE ERROR(125)
          END (*RELEASE*);



          PROCEDURE ABS;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR = INTPTR THEN GEN0(0(*ABI*))
              ELSE
                IF GATTR.TYPTR = REALPTR THEN GEN0(1(*ABR*))
                ELSE BEGIN ERROR(125); GATTR.TYPTR := INTPTR END
          END (*ABS*) ;

          PROCEDURE SQR;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR = INTPTR THEN GEN0(24(*SQI*))
              ELSE
                IF GATTR.TYPTR = REALPTR THEN GEN0(25(*SQR*))
                ELSE BEGIN ERROR(125); GATTR.TYPTR := INTPTR END
          END (*SQR*) ;

          PROCEDURE TRUNC;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR <> REALPTR THEN ERROR(125);
            GEN0(27(*TRC*));
            GATTR.TYPTR := INTPTR
          END (*TRUNC*) ;

          PROCEDURE ODD;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR <> INTPTR THEN ERROR(125);
            GEN0(20(*ODD*));
            GATTR.TYPTR := BOOLPTR
          END (*ODD*) ;

          PROCEDURE ORD;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM >= POWER THEN ERROR(125);
            GATTR.TYPTR := INTPTR
          END (*ORD*) ;

          PROCEDURE CHR;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR <> INTPTR THEN ERROR(125);
            GATTR.TYPTR := CHARPTR
          END (*CHR*) ;



          PROCEDURE PREDSUCC;
          BEGIN ERROR(399);
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(125);
          END (*PREDSUCC*) ;

          PROCEDURE EOF;
          BEGIN
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> FILES THEN ERROR(125);
            IF LKEY = 9 THEN GEN0(8(*EOF*)) ELSE GEN1(30(*CSP*),14(*ELN*));
              GATTR.TYPTR := BOOLPTR
          END (*EOF*) ;

          PROCEDURE CALLNONSTANDARD;
            VAR NXT,LCP: CTP; LSP: STP; LKIND: IDKIND; LB: BOOLEAN;
                LOCPAR, LLC: ADDRRANGE;
          BEGIN LOCPAR := 0;
            WITH FCP^ DO
              BEGIN NXT := NEXT; LKIND := PFKIND;
                IF NOT EXTERN THEN GEN1(41(*MST*),LEVEL-PFLEV)
              END;
            IF SY = LPARENT THEN
              BEGIN LLC := LC;
                REPEAT LB := FALSE; (*DECIDE WHETHER PROC/FUNC MUST BE PASSED*)
                  IF LKIND = ACTUAL THEN
                    BEGIN
                      IF NXT = NIL THEN ERROR(126)
                      ELSE LB := NXT^.KLASS IN [PROC,FUNC]
                    END ELSE ERROR(399);
                  (*FOR FORMAL PROC/FUNC LB IS FALSE AND EXPRESSION
                   WILL BE CALLED, WHICH WILL ALLWAYS INTERPRET A PROC/FUNC ID
                  AT ITS BEGINNING AS A CALL RATHER THAN A PARAMETER PASSING.
                  IN THIS IMPLEMENTATION, PARAMETER PROCEDURES/FUNCTIONS
                  ARE THEREFORE NOT ALLOWED TO HAVE PROCEDURE/FUNCTION
                  PARAMETERS*)
                  INSYMBOL;
                  IF LB THEN   (*PASS FUNCTION OR PROCEDURE*)
                    BEGIN ERROR(399);
                      IF SY <> IDENT THEN
                        BEGIN ERROR(2); SKIP(FSYS + [COMMA,RPARENT]) END
                      ELSE
                        BEGIN
                          IF NXT^.KLASS = PROC THEN SEARCHID([PROC],LCP)
                          ELSE
                            BEGIN SEARCHID([FUNC],LCP);
                              IF NOT COMPTYPES(LCP^.IDTYPE,NXT^.IDTYPE) THEN
                                ERROR(128)
                            END;
                          INSYMBOL;
                          IF NOT (SY IN FSYS + [COMMA,RPARENT]) THEN
                            BEGIN ERROR(6); SKIP(FSYS + [COMMA,RPARENT]) END
                        END
                    END (*IF LB*)
                  ELSE
                    BEGIN EXPRESSION(FSYS + [COMMA,RPARENT]);
                      IF GATTR.TYPTR <> NIL THEN
                        IF LKIND = ACTUAL THEN
                          BEGIN
                            IF NXT <> NIL THEN
                              BEGIN LSP := NXT^.IDTYPE;
                                IF LSP <> NIL THEN
                                  BEGIN
                                    IF (NXT^.VKIND = ACTUAL) THEN
                                      IF LSP^.SIZE <= PTRSIZE THEN
                                      BEGIN LOAD;
                                        IF COMPTYPES(REALPTR,LSP)
                                           AND (GATTR.TYPTR = INTPTR) THEN
                                          BEGIN GEN0(10(*FLT*));
                                            GATTR.TYPTR := REALPTR
                                          END;
                                        LOCPAR := LOCPAR + LSP^.SIZE
                                      END
                                      ELSE
                                      BEGIN
                                        IF (GATTR.KIND = EXPR)
                                         OR (GATTR.KIND = CST) THEN
                                        BEGIN LOAD;
                                          IF COMPTYPES(REALPTR,LSP)
                                             AND (GATTR.TYPTR = INTPTR) THEN
                                            BEGIN GEN0(10(*FLT*));
                                              GATTR.TYPTR := REALPTR
                                            END;
                                          GEN2(56(*STR*),0,LC);
                                          GEN2(50(*LDA*),0,LC);
                                          LC := LC + GATTR.TYPTR^.SIZE;
                                          IF LCMAX < LC THEN LCMAX := LC
                                        END
                                        ELSE
                                          IF COMPTYPES(REALPTR,LSP)
                                           AND (GATTR.TYPTR = INTPTR) THEN
                                          BEGIN LOAD;
                                            GEN0(10(*FLT*));
                                            GEN2(56(*STR*),0,LC);
                                            GEN2(50(*LDA*),0,LC);
                                            LC := LC + GATTR.TYPTR^.SIZE;
                                            IF LCMAX < LC THEN LCMAX := LC
                                          END
                                          ELSE LOADADDRESS;
                                        LOCPAR := LOCPAR + PTRSIZE
                                      END
                                    ELSE
                                      IF GATTR.KIND = VARBL THEN
                                        BEGIN LOADADDRESS; LOCPAR := LOCPAR + PTRSIZE
                                        END
                                      ELSE ERROR(154);
                                    IF NOT COMPTYPES(LSP,GATTR.TYPTR) THEN
                                      ERROR(142)
                                  END
                              END
                          END
                      ELSE (*LKIND = FORMAL*)
                        BEGIN (*PASS FORMAL PARAM*)
                        END
                    END;
                  IF (LKIND = ACTUAL) AND (NXT <> NIL) THEN NXT := NXT^.NEXT
                UNTIL SY <> COMMA;
                LC := LLC;
              IF SY = RPARENT THEN INSYMBOL ELSE ERROR(4)
            END (*IF LPARENT*);
            IF LKIND = ACTUAL THEN
              BEGIN IF NXT <> NIL THEN ERROR(126);
                WITH FCP^ DO
                  BEGIN
                    IF EXTERN THEN GEN1(30(*CSP*),PFNAME)
                    ELSE GENCUP(LOCPAR, PFNAME);
                  END
              END;
            GATTR.TYPTR := FCP^.IDTYPE
          END (*CALLNONSTANDARD*) ;

        BEGIN (*CALL*)
          IF FCP^.PFDECKIND = STANDARD THEN
            BEGIN IF SY = LPARENT THEN INSYMBOL ELSE ERROR(9);
              LKEY := FCP^.KEY;
              IF FCP^.KLASS = PROC THEN
                CASE LKEY OF
                  1,2,
                  3,4:  GETPUTRESETREWRITE;
                  5,11:    READ;
                  6,12:    WRITE;
                  7:    PACK;
                  8:    UNPACK;
                  9:    NEW;
                  10:   RELEASE;
                  13:   MARK
                END
              ELSE
                BEGIN EXPRESSION(FSYS + [RPARENT]);
                      IF LKEY <= 8 THEN LOAD ELSE LOADADDRESS;
                  CASE LKEY OF
                    1:    ABS;
                    2:    SQR;
                    3:    TRUNC;
                    4:    ODD;
                    5:    ORD;
                    6:    CHR;
                    7,8:  PREDSUCC;
                    9,10:    EOF
                  END
                END;
              IF SY = RPARENT THEN INSYMBOL ELSE ERROR(4)
            END (*STANDARD PROCEDURES AND FUNCTIONS*)
          ELSE CALLNONSTANDARD
        END (*CALL*) ;

        PROCEDURE EXPRESSION;
          VAR LATTR: ATTR; LOP: OPERATOR; TYPIND: CHAR; LSIZE: ADDRRANGE;

          PROCEDURE SIMPLEEXPRESSION(FSYS: SETOFSYS);
            VAR LATTR: ATTR; LOP: OPERATOR; SIGNED: BOOLEAN;

            PROCEDURE TERM(FSYS: SETOFSYS);
              VAR LATTR: ATTR; LOP: OPERATOR;

              PROCEDURE FACTOR(FSYS: SETOFSYS);
                VAR LCP: CTP; LVP: CSP; VARPART: BOOLEAN;
                    CSTPART: SET OF 0..58; LSP: STP;
              BEGIN
                IF NOT (SY IN FACBEGSYS) THEN
                  BEGIN ERROR(58); SKIP(FSYS + FACBEGSYS);
                    GATTR.TYPTR := NIL
                  END;
                WHILE SY IN FACBEGSYS DO
                  BEGIN
                    CASE SY OF
              (*ID*)    IDENT:
                        BEGIN SEARCHID([KONST,VARS,FIELD,FUNC],LCP);
                          INSYMBOL;
                          IF LCP^.KLASS = FUNC THEN
                            BEGIN CALL(FSYS,LCP); GATTR.KIND := EXPR END
                          ELSE
                            IF LCP^.KLASS = KONST THEN
                              WITH GATTR, LCP^ DO
                                BEGIN TYPTR := IDTYPE; KIND := CST;
                                  CVAL := VALUES
                                END
                            ELSE
                              BEGIN SELECTOR(FSYS,LCP);
                                IF GATTR.TYPTR<>NIL THEN(*ELIM.SUBR.TYPES TO*)
                                  WITH GATTR,TYPTR^ DO(*SIMPLIFY LATER TESTS*)
                                    IF FORM = SUBRANGE THEN
                                      TYPTR := RANGETYPE
                              END
                        END;
              (*CST*)   INTCONST:
                        BEGIN
                          WITH GATTR DO
                            BEGIN TYPTR := INTPTR; KIND := CST;
                              CVAL := VAL
                            END;
                          INSYMBOL
                        END;
                      REALCONST:
                        BEGIN
                          WITH GATTR DO
                            BEGIN TYPTR := REALPTR; KIND := CST;
                              CVAL := VAL
                            END;
                          INSYMBOL
                        END;
                      STRINGCONST:
                        BEGIN
                          WITH GATTR DO
                            BEGIN
                              IF LGTH = 1 THEN TYPTR := CHARPTR
                              ELSE
                                BEGIN NEW(LSP,ARRAYS);
                                  WITH LSP^ DO
                                    BEGIN AELTYPE := CHARPTR; FORM:=ARRAYS;
                                      INXTYPE := NIL; SIZE := LGTH*CHARSIZE
                                    END;
                                  TYPTR := LSP
                                END;
                              KIND := CST; CVAL := VAL
                            END;
                          INSYMBOL
                        END;
              (*(*)   LPARENT:
                        BEGIN INSYMBOL; EXPRESSION(FSYS + [RPARENT]);
                          IF SY = RPARENT THEN INSYMBOL ELSE ERROR(4)
                        END;
              (*NOT*)   NOTSY:
                        BEGIN INSYMBOL; FACTOR(FSYS);
                          LOAD; GEN0(19(*NOT*));
                          IF GATTR.TYPTR <> NIL THEN
                            IF GATTR.TYPTR <> BOOLPTR THEN
                              BEGIN ERROR(135); GATTR.TYPTR := NIL END;
                        END;
              (*[*)     LBRACK:
                        BEGIN INSYMBOL; CSTPART := [ ]; VARPART := FALSE;
                          NEW(LSP,POWER);
                          WITH LSP^ DO
                            BEGIN ELSET:=NIL;SIZE:=SETSIZE;FORM:=POWER END;
                          IF SY = RBRACK THEN
                            BEGIN
                              WITH GATTR DO
                                BEGIN TYPTR := LSP; KIND := CST END;
                              INSYMBOL
                            END
                          ELSE
                            BEGIN
                              REPEAT EXPRESSION(FSYS + [COMMA,RBRACK]);
                                IF GATTR.TYPTR <> NIL THEN
                                  IF GATTR.TYPTR^.FORM <> SCALAR THEN
                                    BEGIN ERROR(136); GATTR.TYPTR := NIL END
                                  ELSE
                                    IF COMPTYPES(LSP^.ELSET,GATTR.TYPTR) THEN
                                      BEGIN
                                        IF GATTR.KIND = CST THEN
                                          CSTPART := CSTPART+[GATTR.CVAL.IVAL]
                                        ELSE
                                          BEGIN LOAD; GEN0(23(*SGS*));
                                            IF VARPART THEN GEN0(28(*UNI*))
                                            ELSE VARPART := TRUE
                                          END;
                                        LSP^.ELSET := GATTR.TYPTR;
                                        GATTR.TYPTR := LSP
                                      END
                                    ELSE ERROR(137);
                                TEST := SY <> COMMA;
                                IF NOT TEST THEN INSYMBOL
                              UNTIL TEST;
                              IF SY = RBRACK THEN INSYMBOL ELSE ERROR(12)
                            END;
                          IF VARPART THEN
                            BEGIN
                              IF CSTPART <> [ ] THEN
                                BEGIN NEW(LVP,PSET); LVP^.PVAL := CSTPART;
                                  LVP^.CCLASS := PSET;
                                  IF CSTPTRIX = CSTOCCMAX THEN ERROR(254)
                                  ELSE
                                    BEGIN CSTPTRIX := CSTPTRIX + 1;
                                      CSTPTR[CSTPTRIX] := LVP;
                                      GEN2(51(*LDC*),5,CSTPTRIX);
                                      GEN0(28(*UNI*)); GATTR.KIND := EXPR
                                    END
                                END
                            END
                          ELSE
                            BEGIN NEW(LVP,PSET); LVP^.PVAL := CSTPART;
                              LVP^.CCLASS := PSET;
                              GATTR.CVAL.VALP := LVP
                            END
                        END
                    END (*CASE*) ;
                    IF NOT (SY IN FSYS) THEN
                      BEGIN ERROR(6); SKIP(FSYS + FACBEGSYS) END
                  END (*WHILE*)
              END (*FACTOR*) ;

            BEGIN (*TERM*)
              FACTOR(FSYS + [MULOP]);
              WHILE SY = MULOP DO
                      BEGIN LOAD; LATTR := GATTR; LOP := OP;
                  INSYMBOL; FACTOR(FSYS + [MULOP]); LOAD;
                  IF (LATTR.TYPTR <> NIL) AND (GATTR.TYPTR <> NIL) THEN
                    CASE LOP OF
            (***)       MUL:  IF (LATTR.TYPTR=INTPTR)AND(GATTR.TYPTR=INTPTR)
                              THEN GEN0(15(*MPI*))
                            ELSE
                              BEGIN
                                IF LATTR.TYPTR = INTPTR THEN
                                  BEGIN GEN0(9(*FLO*));
                                    LATTR.TYPTR := REALPTR
                                  END
                                ELSE
                                  IF GATTR.TYPTR = INTPTR THEN
                                    BEGIN GEN0(10(*FLT*));
                                      GATTR.TYPTR := REALPTR
                                    END;
                                IF (LATTR.TYPTR = REALPTR)
                                  AND(GATTR.TYPTR=REALPTR)THEN GEN0(16(*MPR*))
                                ELSE
                                  IF(LATTR.TYPTR^.FORM=POWER)
                                    AND COMPTYPES(LATTR.TYPTR,GATTR.TYPTR)THEN
                                    GEN0(12(*INT*))
                                  ELSE BEGIN ERROR(134);GATTR.TYPTR:=NIL END
                              END;
            (*/*)       RDIV: BEGIN
                              IF LATTR.TYPTR = INTPTR THEN
                                BEGIN GEN0(9(*FLO*));
                                  LATTR.TYPTR := REALPTR
                                END;
                              IF GATTR.TYPTR = INTPTR THEN
                                  BEGIN GEN0(10(*FLT*));
                                  GATTR.TYPTR := REALPTR
                                END;
                              IF (LATTR.TYPTR = REALPTR)
                                AND (GATTR.TYPTR=REALPTR)THEN GEN0(7(*DVR*))
                              ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END
                            END;
            (*DIV*)     IDIV: IF (LATTR.TYPTR = INTPTR)
                              AND (GATTR.TYPTR = INTPTR) THEN GEN0(6(*DVI*))
                            ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END;
            (*MOD*)     IMOD: IF (LATTR.TYPTR = INTPTR)
                              AND (GATTR.TYPTR = INTPTR) THEN GEN0(14(*MOD*))
                            ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END;
            (*AND*)     ANDOP:IF (LATTR.TYPTR = BOOLPTR)

          AND (GATTR.TYPTR = BOOLPTR) THEN GEN0(4(*AND*))
                            ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END
                    END (*CASE*)
                  ELSE GATTR.TYPTR := NIL
                END (*WHILE*)
            END (*TERM*) ;

          BEGIN (*SIMPLEEXPRESSION*)
            SIGNED := FALSE;
            IF (SY = ADDOP) AND (OP IN [PLUS,MINUS]) THEN
              BEGIN SIGNED := OP = MINUS; INSYMBOL END;
            TERM(FSYS + [ADDOP]);
            IF SIGNED THEN
              BEGIN LOAD;
                IF GATTR.TYPTR = INTPTR THEN GEN0(17(*NGI*))
                ELSE
                  IF GATTR.TYPTR = REALPTR THEN GEN0(18(*NGR*))
                  ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END
              END;
            WHILE SY = ADDOP DO
              BEGIN LOAD; LATTR := GATTR; LOP := OP;
                INSYMBOL; TERM(FSYS + [ADDOP]); LOAD;
                IF (LATTR.TYPTR <> NIL) AND (GATTR.TYPTR <> NIL) THEN
                  CASE LOP OF
          (*+*)       PLUS:
                      IF (LATTR.TYPTR = INTPTR)AND(GATTR.TYPTR = INTPTR) THEN
                        GEN0(2(*ADI*))
                      ELSE
                        BEGIN
                          IF LATTR.TYPTR = INTPTR THEN
                            BEGIN GEN0(9(*FLO*));
                              LATTR.TYPTR := REALPTR
                            END
                          ELSE
                            IF GATTR.TYPTR = INTPTR THEN
                              BEGIN GEN0(10(*FLT*));
                                GATTR.TYPTR := REALPTR
                              END;
                          IF (LATTR.TYPTR = REALPTR)AND(GATTR.TYPTR = REALPTR)
                            THEN GEN0(3(*ADR*))
                          ELSE IF(LATTR.TYPTR^.FORM=POWER)
                                 AND COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                                 GEN0(28(*UNI*))
                               ELSE BEGIN ERROR(134);GATTR.TYPTR:=NIL END
                        END;
          (*-*)       MINUS:
                      IF (LATTR.TYPTR = INTPTR)AND(GATTR.TYPTR = INTPTR) THEN
                        GEN0(21(*SBI*))
                      ELSE
                        BEGIN
                          IF LATTR.TYPTR = INTPTR THEN
                            BEGIN GEN0(9(*FLO*));
                              LATTR.TYPTR := REALPTR
                            END
                          ELSE
                            IF GATTR.TYPTR = INTPTR THEN
                            BEGIN GEN0(10(*FLT*));
                                GATTR.TYPTR := REALPTR
                              END;
                          IF (LATTR.TYPTR = REALPTR)AND(GATTR.TYPTR = REALPTR)
                            THEN GEN0(22(*SBR*))
                          ELSE
                            IF (LATTR.TYPTR^.FORM = POWER)
                              AND COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                              GEN0(5(*DIF*))
                            ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END
                        END;
          (*OR*)      OROP:
                      IF(LATTR.TYPTR=BOOLPTR)AND(GATTR.TYPTR=BOOLPTR)THEN
                        GEN0(13(*IOR*))
                      ELSE BEGIN ERROR(134); GATTR.TYPTR := NIL END
                  END (*CASE*)
                ELSE GATTR.TYPTR := NIL
              END (*WHILE*)
          END (*SIMPLEEXPRESSION*) ;

        BEGIN (*EXPRESSION*)
          SIMPLEEXPRESSION(FSYS + [RELOP]);
          IF SY = RELOP THEN
            BEGIN
              IF GATTR.TYPTR <> NIL THEN
                IF GATTR.TYPTR^.FORM <= POWER THEN LOAD
                ELSE LOADADDRESS;
                LATTR := GATTR; LOP := OP;
              INSYMBOL; SIMPLEEXPRESSION(FSYS);
              IF GATTR.TYPTR <> NIL THEN
                IF GATTR.TYPTR^.FORM <= POWER THEN LOAD
                ELSE LOADADDRESS;
              IF (LATTR.TYPTR <> NIL) AND (GATTR.TYPTR <> NIL) THEN
                IF LOP = INOP THEN
                  IF GATTR.TYPTR^.FORM = POWER THEN
                    IF COMPTYPES(LATTR.TYPTR,GATTR.TYPTR^.ELSET) THEN
                      GEN0(11(*INN*))
                    ELSE BEGIN ERROR(129); GATTR.TYPTR := NIL END
                  ELSE BEGIN ERROR(130); GATTR.TYPTR := NIL END
                ELSE
                  BEGIN

                    IF LATTR.TYPTR <> GATTR.TYPTR THEN
                      IF LATTR.TYPTR = INTPTR THEN
                        BEGIN GEN0(9(*FLO*));
                          LATTR.TYPTR := REALPTR
                        END
                      ELSE
                        IF GATTR.TYPTR = INTPTR THEN
                          BEGIN GEN0(10(*FLT*));
                            GATTR.TYPTR := REALPTR
                          END;
                    IF COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                      BEGIN LSIZE := LATTR.TYPTR^.SIZE;
                        CASE LATTR.TYPTR^.FORM OF
                          SCALAR:
                            IF LATTR.TYPTR = REALPTR THEN TYPIND := 'R'
                            ELSE
                              IF LATTR.TYPTR = BOOLPTR THEN TYPIND := 'B'
                              ELSE TYPIND := 'I';
                          POINTER:
                            BEGIN
                              IF LOP IN [LTOP,LEOP,GTOP,GEOP] THEN ERROR(131);
                              TYPIND := 'A'
                            END;
                          POWER:
                            BEGIN IF LOP IN [LTOP,GTOP] THEN ERROR(132);
                              TYPIND := 'S'
                          END;
                          ARRAYS:
                            BEGIN
                              IF NOT STRING(LATTR.TYPTR)
                              AND(LOP IN[LTOP,LEOP,GTOP,GEOP])THEN ERROR(131);
                              TYPIND := 'M'
                            END;
                          RECORDS:
                            BEGIN
                              IF LOP IN [LTOP,LEOP,GTOP,GEOP] THEN ERROR(131);
                              TYPIND := 'M'
                            END;
                          FILES:
                            BEGIN ERROR(133); TYPIND := 'F' END
                        END;
                        CASE LOP OF
                          LTOP: GEN2(53(*LES*),ORD(TYPIND),LSIZE);
                          LEOP: GEN2(52(*LEQ*),ORD(TYPIND),LSIZE);
                          GTOP: GEN2(49(*GRT*),ORD(TYPIND),LSIZE);
                          GEOP: GEN2(48(*GEQ*),ORD(TYPIND),LSIZE);
                          NEOP: GEN2(55(*NEQ*),ORD(TYPIND),LSIZE);
                          EQOP: GEN2(47(*EQU*),ORD(TYPIND),LSIZE)
                        END
                      END
                    ELSE ERROR(129)
                  END;
              GATTR.TYPTR := BOOLPTR; GATTR.KIND := EXPR
            END (*SY = RELOP*)
        END (*EXPRESSION*) ;

        PROCEDURE ASSIGNMENT(FCP: CTP);
          VAR LATTR: ATTR;
        BEGIN SELECTOR(FSYS + [BECOMES],FCP);
          IF SY = BECOMES THEN
            BEGIN
              IF GATTR.TYPTR <> NIL THEN
                IF (GATTR.ACCESS<>DRCT) OR (GATTR.TYPTR^.FORM>POWER) THEN
                  LOADADDRESS;
              LATTR := GATTR;
              INSYMBOL; EXPRESSION(FSYS);
              IF GATTR.TYPTR <> NIL THEN
                IF GATTR.TYPTR^.FORM <= POWER THEN LOAD
                ELSE LOADADDRESS;
              IF (LATTR.TYPTR <> NIL) AND (GATTR.TYPTR <> NIL) THEN
                BEGIN
                  IF COMPTYPES(REALPTR,LATTR.TYPTR)AND(GATTR.TYPTR=INTPTR)THEN
                    BEGIN GEN0(10(*FLT*));
                      GATTR.TYPTR := REALPTR
                    END;
                  IF COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                    CASE LATTR.TYPTR^.FORM OF
                      SCALAR,
                      SUBRANGE,
                      POINTER,
                      POWER:   STORE(LATTR);
                      ARRAYS,
                      RECORDS: GEN1(40(*MOV*),LATTR.TYPTR^.SIZE);
                      FILES: ERROR(146)
                    END
                  ELSE ERROR(129)
                END
            END (*SY = BECOMES*)
          ELSE ERROR(51)
        END (*ASSIGNMENT*) ;

        PROCEDURE GOTOSTATEMENT;
          VAR LLP: LBP; FOUND: BOOLEAN; TTOP,TTOP1: DISPRANGE;
        BEGIN
          IF SY = INTCONST THEN
            BEGIN
              FOUND := FALSE;
              TTOP := TOP;
              REPEAT
                WHILE DISPLAY[TTOP].OCCUR <> BLCK DO TTOP := TTOP - 1;
                TTOP1 := TTOP; LLP := DISPLAY[TTOP].FLABEL;
                WHILE (LLP <> NIL) AND NOT FOUND DO
                  WITH LLP^ DO
                    IF LABVAL = VAL.IVAL THEN
                      BEGIN FOUND := TRUE;
                        IF TTOP = TTOP1 THEN
                          GENUJPENT(57(*UJP*),LABNAME)
                        ELSE (*GOTO LEADS OUT OF PROCEDURE*) ERROR(399)
                      END
                    ELSE LLP := NEXTLAB;
                TTOP := TTOP - 1
              UNTIL FOUND OR (TTOP = 0);
              IF NOT FOUND THEN ERROR(167);
              INSYMBOL
            END
          ELSE ERROR(15)
        END (*GOTOSTATEMENT*) ;

        PROCEDURE COMPOUNDSTATEMENT;
        BEGIN
          REPEAT
            REPEAT STATEMENT(FSYS + [SEMICOLON,ENDSY])
            UNTIL NOT (SY IN STATBEGSYS);
            TEST := SY <> SEMICOLON;
            IF NOT TEST THEN INSYMBOL
          UNTIL TEST;
          IF SY = ENDSY THEN INSYMBOL ELSE ERROR(13)
        END (*COMPOUNDSTATEMENET*) ;

        PROCEDURE IFSTATEMENT;
          VAR LCIX1,LCIX2: INTEGER;
        BEGIN EXPRESSION(FSYS + [THENSY]);
          GENLABEL(LCIX1); GENFJP(LCIX1);
          IF SY = THENSY THEN INSYMBOL ELSE ERROR(52);

          STATEMENT(FSYS + [ELSESY]);
          IF SY = ELSESY THEN
            BEGIN GENLABEL(LCIX2); GENUJPENT(57(*UJP*),LCIX2);
              PUTLABEL(LCIX1);
              INSYMBOL; STATEMENT(FSYS);
              PUTLABEL(LCIX2)
            END
          ELSE PUTLABEL(LCIX1)
        END (*IFSTATEMENT*) ;

        PROCEDURE CASESTATEMENT;
          LABEL 1;
          TYPE CIP = ^CASEINFO;
               CASEINFO = PACKED
                          RECORD NEXT: CIP;
                            CSSTART: INTEGER;
                            CSLAB: INTEGER
                          END;
          VAR LSP,LSP1: STP; FSTPTR,LPT1,LPT2,LPT3: CIP; LVAL: VALU;
              LADDR, LCIX, LCIX1, LMIN, LMAX: INTEGER;
        BEGIN EXPRESSION(FSYS + [OFSY,COMMA,COLON]);
          LOAD; GENLABEL(LCIX); GENUJPENT(57(*UJP*),LCIX);
          LSP := GATTR.TYPTR;
          IF LSP <> NIL THEN
            IF (LSP^.FORM <> SCALAR) OR (LSP = REALPTR) THEN
              BEGIN ERROR(144); LSP := NIL END;
          IF SY = OFSY THEN INSYMBOL ELSE ERROR(8);
          FSTPTR := NIL; GENLABEL(LADDR);
          REPEAT
            LPT3 := NIL; GENLABEL(LCIX1);
            REPEAT CONSTANT(FSYS + [COMMA,COLON],LSP1,LVAL);
              IF LSP <> NIL THEN
                IF COMPTYPES(LSP,LSP1) THEN
                  BEGIN LPT1 := FSTPTR; LPT2 := NIL;
                    WHILE LPT1 <> NIL DO
                      WITH LPT1^ DO
                        BEGIN
                          IF CSLAB <= LVAL.IVAL THEN
                            BEGIN IF CSLAB = LVAL.IVAL THEN ERROR(156);
                              GOTO 1
                            END;
                          LPT2 := LPT1; LPT1 := NEXT
                        END;
        1:          NEW(LPT3);
                    WITH LPT3^ DO
                      BEGIN NEXT := LPT1; CSLAB := LVAL.IVAL;
                        CSSTART := LCIX1
                      END;
                    IF LPT2 = NIL THEN FSTPTR := LPT3
                    ELSE LPT2^.NEXT := LPT3
                  END
                ELSE ERROR(147);
              TEST := SY <> COMMA;
              IF NOT TEST THEN INSYMBOL
            UNTIL TEST;
            IF SY = COLON THEN INSYMBOL ELSE ERROR(5);
            PUTLABEL(LCIX1);
            REPEAT STATEMENT(FSYS + [SEMICOLON])
            UNTIL NOT (SY IN STATBEGSYS);
            IF LPT3 <> NIL THEN
              GENUJPENT(57(*UJP*),LADDR);
            TEST := SY <> SEMICOLON;
            IF NOT TEST THEN INSYMBOL
          UNTIL TEST;
          PUTLABEL(LCIX);
          IF FSTPTR <> NIL THEN
            BEGIN LMAX := FSTPTR^.CSLAB;
              (*REVERSE POINTERS*)
              LPT1 := FSTPTR; FSTPTR := NIL;
              REPEAT LPT2 := LPT1^.NEXT; LPT1^.NEXT := FSTPTR;
                FSTPTR := LPT1; LPT1 := LPT2
              UNTIL LPT1 = NIL;
              LMIN := FSTPTR^.CSLAB;
              IF LMAX - LMIN < CIXMAX THEN
                BEGIN IF LC+INTSIZE > LCMAX THEN LCMAX := LC + INTSIZE;
                  GEN2(56(*STR*),0,LC); GEN2(54(*LOD*),0,LC);
                  GEN2(51(*LDC*),1,LMIN); GEN2(48(*GEQ*),ORD('I'),0);
                  GENUJPENT(33(*FJP*),LADDR); GEN2(54(*LOD*),0,LC);
                  GEN2(51(*LDC*),1,LMAX); GEN2(52(*LEQI*),ORD('I'),0);
                  GENUJPENT(33(*FJP*),LADDR); GEN2(54(*LOD*),0,LC);
                  GEN2(51(*LDC*),1,LMIN); GEN0(21(*SBI*)); GENLABEL(LCIX);
                  GENUJPENT(44(*XJP*),LCIX); PUTLABEL(LCIX);
                  REPEAT
                    WITH FSTPTR^ DO
                      BEGIN
                        WHILE CSLAB > LMIN DO
                          BEGIN GENUJPENT(57(*UJP*),LADDR); LMIN:=LMIN+1 END;
                        GENUJPENT(57(*UJP*),CSSTART);
                        FSTPTR := NEXT; LMIN := LMIN + 1
                      END
                  UNTIL FSTPTR = NIL;
                  PUTLABEL(LADDR)
                END
              ELSE ERROR(157)
            END;
            IF SY = ENDSY THEN INSYMBOL ELSE ERROR(13)
        END (*CASESTATEMENT*) ;

        PROCEDURE REPEATSTATEMENT;
          VAR LADDR: INTEGER;
        BEGIN GENLABEL(LADDR); PUTLABEL(LADDR);
          REPEAT
            REPEAT STATEMENT(FSYS + [SEMICOLON,UNTILSY])
            UNTIL NOT (SY IN STATBEGSYS);
            TEST := SY <> SEMICOLON;
            IF NOT TEST THEN INSYMBOL
          UNTIL TEST;
          IF SY = UNTILSY THEN
            BEGIN INSYMBOL; EXPRESSION(FSYS); GENFJP(LADDR)
            END
          ELSE ERROR(53)
        END (*REPEATSTATEMENT*) ;

        PROCEDURE WHILESTATEMENT;
          VAR LADDR, LCIX: INTEGER;
        BEGIN GENLABEL(LADDR); PUTLABEL(LADDR);
          EXPRESSION(FSYS + [DOSY]); GENLABEL(LCIX); GENFJP(LCIX);
          IF SY = DOSY THEN INSYMBOL ELSE ERROR(54);
          STATEMENT(FSYS); GENUJPENT(57(*UJP*),LADDR); PUTLABEL(LCIX)
        END (*WHILESTATEMENT*) ;

        PROCEDURE FORSTATEMENT;
          VAR LATTR: ATTR; {LSP: STP;}  LSY: SYMBOL;
              LCIX, LADDR: INTEGER;
        BEGIN
          IF SY = IDENT THEN
            BEGIN SEARCHID([VARS],LCP);
              WITH LCP^, LATTR DO
                BEGIN TYPTR := IDTYPE; KIND := VARBL;
                  IF VKIND = ACTUAL THEN
                    BEGIN ACCESS := DRCT; VLEVEL := VLEV;
                      DPLMT := VADDR
                    END
                  ELSE BEGIN ERROR(155); TYPTR := NIL END
                END;
              IF LATTR.TYPTR <> NIL THEN
                IF (LATTR.TYPTR^.FORM > SUBRANGE)
                   OR COMPTYPES(REALPTR,LATTR.TYPTR) THEN
                  BEGIN ERROR(143); LATTR.TYPTR := NIL END;
              INSYMBOL
            END
          ELSE
            BEGIN ERROR(2); SKIP(FSYS + [BECOMES,TOSY,DOWNTOSY,DOSY]) END;
          IF SY = BECOMES THEN
            BEGIN INSYMBOL; EXPRESSION(FSYS + [TOSY,DOWNTOSY,DOSY]);
              IF GATTR.TYPTR <> NIL THEN
                  IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(144)
                  ELSE
                    IF COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                      BEGIN LOAD; STORE(LATTR) END
                    ELSE ERROR(145)
            END
          ELSE
            BEGIN ERROR(51); SKIP(FSYS + [TOSY,DOWNTOSY,DOSY]) END;
          IF SY IN [TOSY,DOWNTOSY] THEN
            BEGIN LSY := SY; INSYMBOL; EXPRESSION(FSYS + [DOSY]);
              IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM <> SCALAR THEN ERROR(144)
                ELSE
                  IF COMPTYPES(LATTR.TYPTR,GATTR.TYPTR) THEN
                    BEGIN LOAD; GEN2(56(*STR*),0,LC);
                      GENLABEL(LADDR); PUTLABEL(LADDR);
                      GATTR := LATTR; LOAD; GEN2(54(*LOD*),0,LC);
                      LC := LC + INTSIZE;
                      IF LC > LCMAX THEN LCMAX := LC;
                      IF LSY = TOSY THEN GEN2(52(*LEQ*),ORD('I'),1)
                      ELSE GEN2(48(*GEQ*),ORD('I'),1);
                    END
                  ELSE ERROR(145)
            END
          ELSE BEGIN ERROR(55); SKIP(FSYS + [DOSY]) END;
          GENLABEL(LCIX); GENUJPENT(33(*FJP*),LCIX);
          IF SY = DOSY THEN INSYMBOL ELSE ERROR(54);
          STATEMENT(FSYS);
          GATTR := LATTR; LOAD;
          IF LSY = TOSY THEN GEN1(34(*INC*),1) ELSE GEN1(31(*DEC*),1);
          STORE(LATTR); GENUJPENT(57(*UJP*),LADDR); PUTLABEL(LCIX);
          LC := LC - INTSIZE
        END (*FORSTATEMENT*) ;


        PROCEDURE WITHSTATEMENT;
          VAR LCP: CTP; LCNT1,LCNT2: DISPRANGE;
        BEGIN LCNT1 := 0; LCNT2 := 0;
          REPEAT
            IF SY = IDENT THEN
              BEGIN SEARCHID([VARS,FIELD],LCP); INSYMBOL END
            ELSE BEGIN ERROR(2); LCP := UVARPTR END;
            SELECTOR(FSYS + [COMMA,DOSY],LCP);
            IF GATTR.TYPTR <> NIL THEN
              IF GATTR.TYPTR^.FORM = RECORDS THEN
                IF TOP < DISPLIMIT THEN
                  BEGIN TOP := TOP + 1; LCNT1 := LCNT1 + 1;
                    WITH DISPLAY[TOP] DO
                      BEGIN FNAME := GATTR.TYPTR^.FSTFLD;
                        FLABEL := NIL
                      END;
                    IF GATTR.ACCESS = DRCT THEN
                      WITH DISPLAY[TOP] DO
                        BEGIN OCCUR := CREC; CLEV := GATTR.VLEVEL;
                          CDSPL := GATTR.DPLMT
                        END
                    ELSE
                      BEGIN LOADADDRESS; GEN2(56(*STR*),0,LC);
                        WITH DISPLAY[TOP] DO
                          BEGIN OCCUR := VREC; VDSPL := LC END;
                        LC := LC + PTRSIZE; LCNT2 := LCNT2 + PTRSIZE;
                        IF LC > LCMAX THEN LCMAX := LC
                      END
                  END
                ELSE ERROR(250)
              ELSE ERROR(140);
            TEST := SY <> COMMA;
            IF NOT TEST THEN INSYMBOL
          UNTIL TEST;
          IF SY = DOSY THEN INSYMBOL ELSE ERROR(54);
          STATEMENT(FSYS);
          TOP := TOP - LCNT1; LC := LC - LCNT2;
        END (*WITHSTATEMENT*) ;

      BEGIN (*STATEMENT*)
        IF SY = INTCONST THEN (*LABEL*)
          BEGIN LLP := DISPLAY[TOP].FLABEL;
            WHILE LLP <> NIL DO
              WITH LLP^ DO
                IF LABVAL = VAL.IVAL THEN
                  BEGIN IF DEFINED THEN ERROR(165);
                    PUTLABEL(LABNAME); DEFINED := TRUE;
                    GOTO 1
                  END
                ELSE LLP := NEXTLAB;
            ERROR(167);
      1:    INSYMBOL;
            IF SY = COLON THEN INSYMBOL ELSE ERROR(5)
          END;
        IF NOT (SY IN FSYS + [IDENT]) THEN
          BEGIN ERROR(6); SKIP(FSYS) END;
        IF SY IN STATBEGSYS + [IDENT] THEN
          BEGIN
            CASE SY OF
              IDENT:    BEGIN SEARCHID([VARS,FIELD,FUNC,PROC],LCP); INSYMBOL;
                          IF LCP^.KLASS = PROC THEN CALL(FSYS,LCP)
                          ELSE ASSIGNMENT(LCP)
                        END;
              BEGINSY:  BEGIN INSYMBOL; COMPOUNDSTATEMENT END;
              GOTOSY:   BEGIN INSYMBOL; GOTOSTATEMENT END;
              IFSY:     BEGIN INSYMBOL; IFSTATEMENT END;
              CASESY:   BEGIN INSYMBOL; CASESTATEMENT END;
              WHILESY:  BEGIN INSYMBOL; WHILESTATEMENT END;
              REPEATSY: BEGIN INSYMBOL; REPEATSTATEMENT END;
              FORSY:    BEGIN INSYMBOL; FORSTATEMENT END;
              WITHSY:   BEGIN INSYMBOL; WITHSTATEMENT END
            END;
            IF NOT (SY IN [SEMICOLON,ENDSY,ELSESY,UNTILSY]) THEN
              BEGIN ERROR(6); SKIP(FSYS) END
          END
      END (*STATEMENT*) ;

    BEGIN (*BODY*)
      IF FPROCP <> NIL THEN ENTNAME := FPROCP^.PFNAME
      ELSE GENLABEL(ENTNAME);
      CSTPTRIX := 0;
      PUTLABEL(ENTNAME); GENLABEL(SEGSIZE);
      GENUJPENT(32(*ENT*),SEGSIZE);
      IF FPROCP <> NIL THEN (*COPY MULTIPLE VALUES INTO LOACAL CELLS*)
        BEGIN LLC1 := LCAFTERMARKSTACK;
          LCP := FPROCP^.NEXT;
          WHILE LCP <> NIL DO
            WITH LCP^ DO
              BEGIN
                IF KLASS = VARS THEN
                  IF IDTYPE <> NIL THEN
                    IF (VKIND = ACTUAL) AND (IDTYPE^.SIZE > PTRSIZE) THEN
                      BEGIN
                        GEN2(50(*LDA*),0,VADDR);
                        GEN2(54(*LOD*),0,LLC1);
                        GEN1(40(*MOV*),IDTYPE^.SIZE);
                        LLC1 := LLC1 + PTRSIZE
                      END
                    ELSE LLC1 := LLC1 + IDTYPE^.SIZE;
                LCP := LCP^.NEXT;
              END;
        END;
      LCMAX := LC;
      REPEAT
        REPEAT STATEMENT(FSYS + [SEMICOLON,ENDSY])
        UNTIL NOT (SY IN STATBEGSYS);
        TEST := SY <> SEMICOLON;
        IF NOT TEST THEN INSYMBOL
      UNTIL TEST;
      IF SY = ENDSY THEN INSYMBOL ELSE ERROR(13);
      LLP := DISPLAY[TOP].FLABEL; (*TEST FOR UNDEFINED LABELS*)
      WHILE LLP <> NIL DO
        WITH LLP^ DO
          BEGIN
            IF NOT DEFINED THEN
              BEGIN ERROR(168);
                WRITELN(OUTPUT); WRITELN(OUTPUT,' LABEL ',LABVAL);
                WRITE(OUTPUT,' ':CHCNT+16)
              END;
            LLP := NEXTLAB
          END;
      IF FPROCP <> NIL THEN
        BEGIN
          IF FPROCP^.IDTYPE = NIL THEN GEN1(42(*RET*),ORD('P'))
          ELSE
            WITH FPROCP^ DO
              IF IDTYPE = REALPTR THEN GEN1(42(*RET*),ORD('R'))
              ELSE IF IDTYPE = BOOLPTR THEN GEN1(42(*RET*),ORD('B'))
                   ELSE IF IDTYPE^.FORM = POINTER THEN
                          GEN1(42(*RET*),ORD('A'))
                        ELSE IF (IDTYPE = CHARPTR)
                                OR ((IDTYPE^.FORM = SUBRANGE)
                                    AND (IDTYPE^.RANGETYPE = CHARPTR)) THEN
                               GEN1(42(*RET*),ORD('C'))
                             ELSE GEN1(42(*RET*),ORD('I'));
          IF PRCODE THEN WRITELN(PRR,'L',SEGSIZE:4,'=',LCMAX)
        END
      ELSE
        BEGIN GEN1(42(*RET*),ORD('P')); LCMAX := LCMAX - 1;
          IF PRCODE THEN WRITELN(PRR,'L',SEGSIZE:4,'=',LCMAX);
          IF PRCODE THEN
            BEGIN  WRITELN(PRR) (*SIMULATES EOR*) END;
          IC := 0;
          (*GENERATE CALL OF MAIN PROGRAM; NOTE THAT THIS CALL MUST BE LOADED
           AT ABSOLUTE ADDRESS ZERO*)
          GEN1(41(*MST*),0); GENCUP(0,ENTNAME); GEN0(29(*STP*));
          IF PRCODE THEN
            BEGIN  WRITELN(PRR) (*SIMULATES EOR*) END;
          SAVEID := ID;
          WHILE FEXTFILEP <> NIL DO
            BEGIN
              WITH FEXTFILEP^ DO
                IF NOT ((FILENAME = 'INPUT   ') OR (FILENAME = 'OUTPUT  ') OR
                        (FILENAME = 'PRD     ') OR (FILENAME = 'PRR     '))
                THEN BEGIN ID := FILENAME;
                       SEARCHID([VARS],LLCP);
                       IF LLCP <> NIL THEN
                         IF LLCP^.IDTYPE^.FORM <> FILES THEN
                           LLCP := NIL;
                       IF LLCP = NIL THEN
                         BEGIN WRITELN(OUTPUT);
                           WRITELN(OUTPUT,' ':8,'UNDECLARED ','EXTERNAL FILE',
                                     FEXTFILEP^.FILENAME:8);
                           WRITE(OUTPUT,' ':CHCNT+16)
                         END
                     END;
                FEXTFILEP := FEXTFILEP^.NEXTFILE
            END;
          ID := SAVEID;
          IF LIST THEN
            WRITELN(OUTPUT);
          IF PRTABLES THEN PRINTTABLES(TRUE)
        END;
    END (*BODY*) ;

  BEGIN (*BLOCK*)
    DP := TRUE;
    REPEAT
      IF SY = LABELSY THEN
        BEGIN INSYMBOL; LABELDECLARATION END;
      IF SY = CONSTSY THEN
        BEGIN INSYMBOL; CONSTDECLARATION END;
      IF SY = TYPESY THEN
        BEGIN INSYMBOL; TYPEDECLARATION END;
      IF SY = VARSY THEN
        BEGIN INSYMBOL; VARDECLARATION END;
      WHILE SY IN [PROCSY,FUNCSY] DO
        BEGIN LSY := SY; INSYMBOL; PROCDECLARATION(LSY) END;
      IF SY <> BEGINSY THEN
        BEGIN ERROR(18); SKIP(FSYS) END
    UNTIL SY IN STATBEGSYS;
    DP := FALSE;
    IF SY = BEGINSY THEN INSYMBOL ELSE ERROR(17);
    REPEAT BODY(FSYS + [CASESY]);
      IF SY <> FSY THEN
        BEGIN ERROR(6); SKIP(FSYS + [FSY]) END
    UNTIL (SY = FSY) OR (SY IN BLOCKBEGSYS);
  END (*BLOCK*) ;

  PROCEDURE PROGRAMME(FSYS:SETOFSYS);
    VAR EXTFP:EXTFILEP;
  BEGIN
    IF SY = PROGSY THEN
      BEGIN INSYMBOL; IF SY <> IDENT THEN ERROR(2); INSYMBOL;
        IF NOT (SY IN [LPARENT,SEMICOLON]) THEN ERROR(14);
        IF SY = LPARENT  THEN
          BEGIN
            REPEAT INSYMBOL;
              IF SY = IDENT THEN
                BEGIN NEW(EXTFP);
                  WITH EXTFP^ DO
                    BEGIN FILENAME := ID; NEXTFILE := FEXTFILEP END;
                  FEXTFILEP := EXTFP;
                  INSYMBOL;
                  IF NOT ( SY IN [COMMA,RPARENT] ) THEN ERROR(20)
                END
              ELSE ERROR(2)
            UNTIL SY <> COMMA;
            IF SY <> RPARENT THEN ERROR(4);
            INSYMBOL
          END;
        IF SY <> SEMICOLON THEN ERROR(14)
        ELSE INSYMBOL;
      END;
    REPEAT BLOCK(FSYS,PERIOD,NIL);
      IF SY <> PERIOD THEN ERROR(21)
    UNTIL SY = PERIOD
  END (*PROGRAMME*) ;


  PROCEDURE STDNAMES;
  BEGIN
    NA[ 1] := 'FALSE   '; NA[ 2] := 'TRUE    '; NA[ 3] := 'INPUT   ';
    NA[ 4] := 'OUTPUT  '; NA[ 5] := 'GET     '; NA[ 6] := 'PUT     ';
    NA[ 7] := 'RESET   '; NA[ 8] := 'REWRITE '; NA[ 9] := 'READ    ';
    NA[10] := 'WRITE   '; NA[11] := 'PACK    '; NA[12] := 'UNPACK  ';
    NA[13] := 'NEW     '; NA[14] := 'RELEASE '; NA[15] := 'READLN  ';
    NA[16] := 'WRITELN ';
    NA[17] := 'ABS     '; NA[18] := 'SQR     '; NA[19] := 'TRUNC   ';
    NA[20] := 'ODD     '; NA[21] := 'ORD     '; NA[22] := 'CHR     ';
    NA[23] := 'PRED    '; NA[24] := 'SUCC    '; NA[25] := 'EOF     ';
    NA[26] := 'EOLN    ';
    NA[27] := 'SIN     '; NA[28] := 'COS     '; NA[29] := 'EXP     ';
    NA[30] := 'SQRT    '; NA[31] := 'LN      '; NA[32] := 'ARCTAN  ';
    NA[33] := 'PRD     '; NA[34] := 'PRR     '; NA[35] := 'MARK    ';
  END (*STDNAMES*) ;

  PROCEDURE ENTERSTDTYPES;
    {VAR SP: STP;}
  BEGIN                                                 (*TYPE UNDERLIEING:*)
                                                         (*******************)

    NEW(INTPTR,SCALAR,STANDARD);                              (*INTEGER*)
    WITH INTPTR^ DO
      BEGIN SIZE := INTSIZE; FORM := SCALAR; SCALKIND := STANDARD END;
    NEW(REALPTR,SCALAR,STANDARD);                             (*REAL*)
    WITH REALPTR^ DO
      BEGIN SIZE := REALSIZE; FORM := SCALAR; SCALKIND := STANDARD END;
    NEW(CHARPTR,SCALAR,STANDARD);                             (*CHAR*)
    WITH CHARPTR^ DO
      BEGIN SIZE := CHARSIZE; FORM := SCALAR; SCALKIND := STANDARD END;
    NEW(BOOLPTR,SCALAR,DECLARED);                             (*BOOLEAN*)
    WITH BOOLPTR^ DO
      BEGIN SIZE := BOOLSIZE; FORM := SCALAR; SCALKIND := DECLARED END;
    NEW(NILPTR,POINTER);                                      (*NIL*)
    WITH NILPTR^ DO
      BEGIN ELTYPE := NIL; SIZE := PTRSIZE; FORM := POINTER END;
    NEW(TEXTPTR,FILES);                                       (*TEXT*)
    WITH TEXTPTR^ DO
      BEGIN FILTYPE := CHARPTR; SIZE := CHARSIZE; FORM := FILES END
  END (*ENTERSTDTYPES*) ;

  PROCEDURE ENTSTDNAMES;
    VAR CP,CP1: CTP; I: INTEGER;
  BEGIN                                                       (*NAME:*)
                                                              (*******)

    NEW(CP,TYPES);                                            (*INTEGER*)
    WITH CP^ DO
      BEGIN NAME := 'INTEGER '; IDTYPE := INTPTR; KLASS := TYPES END;
    ENTERID(CP);
    NEW(CP,TYPES);                                            (*REAL*)
    WITH CP^ DO
      BEGIN NAME := 'REAL    '; IDTYPE := REALPTR; KLASS := TYPES END;
    ENTERID(CP);
    NEW(CP,TYPES);                                            (*CHAR*)
    WITH CP^ DO
      BEGIN NAME := 'CHAR    '; IDTYPE := CHARPTR; KLASS := TYPES END;
    ENTERID(CP);
    NEW(CP,TYPES);                                            (*BOOLEAN*)
    WITH CP^ DO
      BEGIN NAME := 'BOOLEAN '; IDTYPE := BOOLPTR; KLASS := TYPES END;
    ENTERID(CP);
    CP1 := NIL;
    FOR I := 1 TO 2 DO
      BEGIN NEW(CP,KONST);                                    (*FALSE,TRUE*)
        WITH CP^ DO
          BEGIN NAME := NA[I]; IDTYPE := BOOLPTR;
            NEXT := CP1; VALUES.IVAL := I - 1; KLASS := KONST
          END;
        ENTERID(CP); CP1 := CP
      END;
    BOOLPTR^.FCONST := CP;
    NEW(CP,KONST);                                             (*NIL*)
    WITH CP^ DO
      BEGIN NAME := 'NIL     '; IDTYPE := NILPTR;
        NEXT := NIL; VALUES.IVAL := 0; KLASS := KONST
      END;
    ENTERID(CP);
    FOR I := 3 TO 4 DO
      BEGIN NEW(CP,VARS);                                     (*INPUT,OUTPUT*)
        WITH CP^ DO
          BEGIN NAME := NA[I]; IDTYPE := TEXTPTR; KLASS := VARS;
            VKIND := ACTUAL; NEXT := NIL; VLEV := 1;
            VADDR := LCAFTERMARKSTACK + (I-3)*CHARSIZE
          END;
        ENTERID(CP)
      END;
    FOR I:=33 TO 34 DO
      BEGIN NEW(CP,VARS);                                     (*PRD,PRR FILES*)
         WITH CP^ DO
           BEGIN NAME := NA[I]; IDTYPE := TEXTPTR; KLASS := VARS;
              VKIND := ACTUAL; NEXT := NIL; VLEV := 1;
              VADDR := LCAFTERMARKSTACK + (I-31)*CHARSIZE
           END;
         ENTERID(CP)
      END;
    FOR I := 5 TO 16 DO
      BEGIN NEW(CP,PROC,STANDARD);                         (*GET,PUT,RESET*)
        WITH CP^ DO                                           (*REWRITE,READ*)
          BEGIN NAME := NA[I]; IDTYPE := NIL;                 (*WRITE,PACK*)
            NEXT := NIL; KEY := I - 4;                        (*UNPACK,PACK*)
            KLASS := PROC; PFDECKIND := STANDARD
          END;
        ENTERID(CP)
      END;
    NEW(CP,PROC,STANDARD);
    WITH CP^ DO
        BEGIN NAME:=NA[35]; IDTYPE:=NIL;
              NEXT:= NIL; KEY:=13;
              KLASS:=PROC; PFDECKIND:= STANDARD
        END; ENTERID(CP);
    FOR I := 17 TO 26 DO
      BEGIN NEW(CP,FUNC,STANDARD);                         (*ABS,SQR,TRUNC*)
        WITH CP^ DO                                           (*ODD,ORD,CHR*)
          BEGIN NAME := NA[I]; IDTYPE := NIL;              (*PRED,SUCC,EOF*)
            NEXT := NIL; KEY := I - 16;
            KLASS := FUNC; PFDECKIND := STANDARD
          END;
        ENTERID(CP)
      END;
    NEW(CP,VARS);                      (*PARAMETER OF PREDECLARED FUNCTIONS*)
    WITH CP^ DO
      BEGIN NAME := '        '; IDTYPE := REALPTR; KLASS := VARS;
        VKIND := ACTUAL; NEXT := NIL; VLEV := 1; VADDR := 0
      END;
    FOR I := 27 TO 32 DO
      BEGIN NEW(CP1,FUNC,DECLARED,ACTUAL);                    (*SIN,COS,EXP*)
        WITH CP1^ DO                                       (*SQRT,LN,ARCTAN*)
          BEGIN NAME := NA[I]; IDTYPE := REALPTR; NEXT := CP;
            FORWDECL := FALSE; EXTERN := TRUE; PFLEV := 0; PFNAME := I - 12;
            KLASS := FUNC; PFDECKIND := DECLARED; PFKIND := ACTUAL
          END;
        ENTERID(CP1)
      END
  END (*ENTSTDNAMES*) ;

  PROCEDURE ENTERUNDECL;
  BEGIN
    NEW(UTYPPTR,TYPES);
    WITH UTYPPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; KLASS := TYPES END;
    NEW(UCSTPTR,KONST);
    WITH UCSTPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; NEXT := NIL;
        KLASS := KONST; VALUES.IVAL := 0
      END;
    NEW(UVARPTR,VARS);
    WITH UVARPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; VKIND := ACTUAL;
        NEXT := NIL; VLEV := 0; VADDR := 0; KLASS := VARS
      END;
    NEW(UFLDPTR,FIELD);
    WITH UFLDPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; NEXT := NIL; FLDADDR := 0;
        KLASS := FIELD
      END;
    NEW(UPRCPTR,PROC,DECLARED,ACTUAL);
    WITH UPRCPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; FORWDECL := FALSE;
        NEXT := NIL; EXTERN := FALSE; PFLEV := 0; GENLABEL(PFNAME);
        KLASS := PROC; PFDECKIND := DECLARED; PFKIND := ACTUAL
      END;
    NEW(UFCTPTR,FUNC,DECLARED,ACTUAL);
    WITH UFCTPTR^ DO
      BEGIN NAME := '        '; IDTYPE := NIL; NEXT := NIL;
        FORWDECL := FALSE; EXTERN := FALSE; PFLEV := 0; GENLABEL(PFNAME);
        KLASS := FUNC; PFDECKIND := DECLARED; PFKIND := ACTUAL
      END
  END (*ENTERUNDECL*) ;

  PROCEDURE INITSCALARS;
  BEGIN FWPTR := NIL;
    { TURN OUTPUT CODE BACK ON. [SAM] }
    PRTABLES := FALSE; LIST := TRUE; PRCODE := TRUE {FALSE};
    DP := TRUE; PRTERR := TRUE; ERRINX := 0;
    INTLABEL := 0; KK := 8; FEXTFILEP := NIL;
    LC := LCAFTERMARKSTACK + FILEBUFFER*CHARSIZE;
    (* NOTE IN THE ABOVE RESERVATION OF BUFFER STORE FOR 2 TEXT FILES *)
    IC := 3; EOL := TRUE; LINECOUNT := 0;
    CH := ' '; CHCNT := 0;
    GLOBTESTP := NIL;
    MXINT10 := MAXINT DIV 10; DIGMAX := STRGLGTH - 1;

  END (*INITSCALARS*) ;

  PROCEDURE INITSETS;
  BEGIN
    CONSTBEGSYS := [ADDOP,INTCONST,REALCONST,STRINGCONST,IDENT];
    SIMPTYPEBEGSYS := [LPARENT] + CONSTBEGSYS;
    TYPEBEGSYS:=[ARROW,PACKEDSY,ARRAYSY,RECORDSY,SETSY,FILESY]+SIMPTYPEBEGSYS;
    TYPEDELS := [ARRAYSY,RECORDSY,SETSY,FILESY];
    BLOCKBEGSYS := [LABELSY,CONSTSY,TYPESY,VARSY,PROCSY,FUNCSY,
                    BEGINSY];
    SELECTSYS := [ARROW,PERIOD,LBRACK];
    FACBEGSYS := [INTCONST,REALCONST,STRINGCONST,IDENT,LPARENT,LBRACK,NOTSY];
    STATBEGSYS := [BEGINSY,GOTOSY,IFSY,WHILESY,REPEATSY,FORSY,WITHSY,
                   CASESY];
  END (*INITSETS*) ;

  PROCEDURE INITTABLES;
    PROCEDURE RESWORDS;
    BEGIN
      RW[ 1] := 'IF      '; RW[ 2] := 'DO      '; RW[ 3] := 'OF      ';
      RW[ 4] := 'TO      '; RW[ 5] := 'IN      '; RW[ 6] := 'OR      ';
      RW[ 7] := 'END     '; RW[ 8] := 'FOR     '; RW[ 9] := 'VAR     ';
      RW[10] := 'DIV     '; RW[11] := 'MOD     '; RW[12] := 'SET     ';
      RW[13] := 'AND     '; RW[14] := 'NOT     '; RW[15] := 'THEN    ';
      RW[16] := 'ELSE    '; RW[17] := 'WITH    '; RW[18] := 'GOTO    ';
      RW[19] := 'CASE    '; RW[20] := 'TYPE    ';
      RW[21] := 'FILE    '; RW[22] := 'BEGIN   ';
      RW[23] := 'UNTIL   '; RW[24] := 'WHILE   '; RW[25] := 'ARRAY   ';
      RW[26] := 'CONST   '; RW[27] := 'LABEL   ';
      RW[28] := 'REPEAT  '; RW[29] := 'RECORD  '; RW[30] := 'DOWNTO  ';
      RW[31] := 'PACKED  '; RW[32] := 'FORWARD '; RW[33] := 'PROGRAM ';
      RW[34] := 'FUNCTION'; RW[35] := 'PROCEDUR';
      FRW[1] :=  1; FRW[2] :=  1; FRW[3] :=  7; FRW[4] := 15; FRW[5] := 22;
      FRW[6] := 28; FRW[7] := 32; FRW[8] := 34; FRW[9] := 36;
    END (*RESWORDS*) ;

    PROCEDURE SYMBOLS;
    BEGIN
      RSY[1] := IFSY; RSY[2] := DOSY; RSY[3] := OFSY; RSY[4] := TOSY;
      RSY[5] := RELOP; RSY[6] := ADDOP; RSY[7] := ENDSY; RSY[8] := FORSY;
      RSY[9] := VARSY; RSY[10] := MULOP; RSY[11] := MULOP; RSY[12] := SETSY;
      RSY[13] := MULOP; RSY[14] := NOTSY; RSY[15] := THENSY;
      RSY[16] := ELSESY; RSY[17] := WITHSY; RSY[18] := GOTOSY;
      RSY[19] := CASESY; RSY[20] := TYPESY;
      RSY[21] := FILESY; RSY[22] := BEGINSY;
      RSY[23] := UNTILSY; RSY[24] := WHILESY; RSY[25] := ARRAYSY;
      RSY[26] := CONSTSY; RSY[27] := LABELSY;
      RSY[28] := REPEATSY; RSY[29] := RECORDSY; RSY[30] := DOWNTOSY;
      RSY[31] := PACKEDSY; RSY[32] := FORWARDSY; RSY[33] := PROGSY;
      RSY[34] := FUNCSY; RSY[35] := PROCSY;
      SSY['+'] := ADDOP; SSY['-'] := ADDOP; SSY['*'] := MULOP;
      SSY['/'] := MULOP; SSY['('] := LPARENT; SSY[')'] := RPARENT;
      SSY['$'] := OTHERSY; SSY['='] := RELOP; SSY[' '] := OTHERSY;
      SSY[','] := COMMA; SSY['.'] := PERIOD; SSY[''''] := OTHERSY;
      SSY['['] := LBRACK; SSY[']'] := RBRACK; SSY[':'] := COLON;
      SSY['^'] := ARROW;
      SSY['<'] := RELOP; SSY['>'] := RELOP;
      SSY[';'] := SEMICOLON;
    END (*SYMBOLS*) ;

    PROCEDURE RATORS;
      VAR I: INTEGER; CH: CHAR;
    BEGIN
      FOR I := 1 TO 35 (*NR OF RES WORDS*) DO ROP[I] := NOOP;
      ROP[5] := INOP; ROP[10] := IDIV; ROP[11] := IMOD;
      ROP[6] := OROP; ROP[13] := ANDOP;
      FOR CH := chr(0) { '+' } TO chr(maxchr) { ';' } DO SOP[CH] := NOOP;
      SOP['+'] := PLUS; SOP['-'] := MINUS; SOP['*'] := MUL; SOP['/'] := RDIV;
      SOP['='] := EQOP;
      SOP['<'] := LTOP; SOP['>'] := GTOP;
    END (*RATORS*) ;

    PROCEDURE PROCMNEMONICS;
    BEGIN
      SNA[ 1] :=' GET'; SNA[ 2] :=' PUT'; SNA[ 3] :=' RDI'; SNA[ 4] :=' RDR';
      SNA[ 5] :=' RDC'; SNA[ 6] :=' WRI'; SNA[ 7] :=' WRO'; SNA[ 8] :=' WRR';
      SNA[ 9] :=' WRC'; SNA[10] :=' WRS'; SNA[11] :=' PAK'; SNA[12] :=' NEW';
      SNA[13] :=' RST'; SNA[14] :=' ELN'; SNA[15] :=' SIN'; SNA[16] :=' COS';
      SNA[17] :=' EXP'; SNA[18] :=' SQT'; SNA[19] :=' LOG'; SNA[20] :=' ATN';
      SNA[21] :=' RLN'; SNA[22] :=' WLN'; SNA[23] :=' SAV';
    END (*PROCMNEMONICS*) ;

    PROCEDURE INSTRMNEMONICS;
    BEGIN
      MN[0] :=' ABI'; MN[1] :=' ABR'; MN[2] :=' ADI'; MN[3] :=' ADR';
      MN[4] :=' AND'; MN[5] :=' DIF'; MN[6] :=' DVI'; MN[7] :=' DVR';
      MN[8] :=' EOF'; MN[9] :=' FLO'; MN[10] :=' FLT'; MN[11] :=' INN';
      MN[12] :=' INT'; MN[13] :=' IOR'; MN[14] :=' MOD'; MN[15] :=' MPI';
      MN[16] :=' MPR'; MN[17] :=' NGI'; MN[18] :=' NGR'; MN[19] :=' NOT';
      MN[20] :=' ODD'; MN[21] :=' SBI'; MN[22] :=' SBR'; MN[23] :=' SGS';
      MN[24] :=' SQI'; MN[25] :=' SQR'; MN[26] :=' STO'; MN[27] :=' TRC';
      MN[28] :=' UNI'; MN[29] :=' STP'; MN[30] :=' CSP'; MN[31] :=' DEC';
      MN[32] :=' ENT'; MN[33] :=' FJP'; MN[34] :=' INC'; MN[35] :=' IND';
      MN[36] :=' IXA'; MN[37] :=' LAO'; MN[38] :=' LCA'; MN[39] :=' LDO';
      MN[40] :=' MOV'; MN[41] :=' MST'; MN[42] :=' RET'; MN[43] :=' SRO';
      MN[44] :=' XJP'; MN[45] :=' CHK'; MN[46] :=' CUP'; MN[47] :=' EQU';
      MN[48] :=' GEQ'; MN[49] :=' GRT'; MN[50] :=' LDA'; MN[51] :=' LDC';
      MN[52] :=' LEQ'; MN[53] :=' LES'; MN[54] :=' LOD'; MN[55] :=' NEQ';
      MN[56] :=' STR'; MN[57] :=' UJP';
    END (*INSTRMNEMONICS*) ;

  BEGIN (*INITTABLES*)
    RESWORDS; SYMBOLS; RATORS;
    INSTRMNEMONICS; PROCMNEMONICS;
  END (*INITTABLES*) ;

BEGIN

  (*INITIALIZE*)
  (************)
  INITSCALARS; INITSETS; INITTABLES;

  (*ENTER STANDARD NAMES AND STANDARD TYPES:*)
  (******************************************)

  LEVEL := 0; TOP := 0;
  WITH DISPLAY[0] DO
    BEGIN FNAME := NIL; FLABEL := NIL; OCCUR := BLCK END;
  ENTERSTDTYPES;   STDNAMES; ENTSTDNAMES;   ENTERUNDECL;
  TOP := 1; LEVEL := 1;
  WITH DISPLAY[1] DO
    BEGIN FNAME := NIL; FLABEL := NIL; OCCUR := BLCK END;

  (*COMPILE:*)
  (**********)

  { REWRITE(PRR); } (* REQUIRED FOR ISO 7185 [SAM] *)
  INSYMBOL;
  PROGRAMME(BLOCKBEGSYS+STATBEGSYS-[CASESY]);

END.
