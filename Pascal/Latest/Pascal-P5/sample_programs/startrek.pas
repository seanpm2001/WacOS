{$l-}
PROGRAM startrek(input, output);

label 99;

CONST entenergy = 5000;        {units of energy to start enterprise}
      starttorps = 10;         {photon torpedos to start}
      klingenergy = 300;       {units of energy to start klingon ships}
      galaxysize = 7;          {square size of galaxy - 1}
      quadsize = 7;            {square size of quadrant - 1}
      maxdigit = 9;            {maximum value of single digit}
      mindevice = '0';         {lowest device number}
      maxdevice = '7';         {highest device number}
      maxklingons = 11;        {maximum number of klingon entities in 1 quad}
      nosym = '.';             {symbol for nothingness}
      starsym = '*';           {symbol for star}
      novasym = '+';           {symbol for nova}
      entsym = 'E';            {symbol for enterprise}
      fbasesym = 'B';         {symbol for federation base}
      klingsym = 'K';          {symbol for klingon ship}
      kbasesym = '@';         {symbol for klingon base}
      alarm = 7;                {terminal alarm}

TYPE digits = 0..maxdigit;
     quadrange = 0..galaxysize;
     sectrange = 0..quadsize;
     quadrec = RECORD
                  ishistory : BOOLEAN;         {seen in long range scanner}
                  klingbasenum,               {number of klingon bases}
                  klingnum,                    {number of klingons}
                  fedbasenum,                 {number of federation bases}
                  starnum : digits;            {number of stars}
                END {of quadrec};
     objects = (snothing, sstar, senterprise, snova, sklingon,
                sfedbase, sklingbase);
     condtypes = (cgreen, cred, cyellow, cblack, cdocked);
     sectxy = RECORD
                  x, y : sectrange;
                END {of sectxy};
     quadxy = RECORD
                  x, y : quadrange;
                END {of quadxy};
     klingonrec = RECORD
                     position : sectxy;
                     energyleft : INTEGER;
                   END {of klingonrec};
     devicerec = RECORD
                    name : packed array [1..20] of char;
                    downtime : INTEGER;
                  END {of devicerec};

VAR rndseq: integer;
    bell : CHAR;
    curyear, startyear, endyear, curenergy, curtorps,
      startklingons, totalkbases, totalklingons, badpoints : INTEGER;
    cursect : sectxy;
    curquad : quadxy;
    device : ARRAY [mindevice..maxdevice] OF devicerec;
    quadrant : ARRAY [sectrange, sectrange] OF objects;
    galaxy : ARRAY [quadrange, quadrange] OF quadrec;
    klingons : ARRAY [0..maxklingons] OF klingonrec;
    symbols : PACKED ARRAY [objects] OF CHAR;
    condnames : ARRAY [condtypes] OF packed array [1..10] of char;
    condition : condtypes;

function expp(r: real): real;

var i: integer;
    t, m: real;

begin

   t := 0.0;
   m := 1.0;
   for i := 1 to 15 do begin

      t := t+m;
      m := m*(r/i)

   end;
   expp := t

end;

FUNCTION random (low, hi : INTEGER) : INTEGER;
{Return a random number between two bounds}
const a = 16807;
      m = 2147483647;
var gamma: integer;
BEGIN
  gamma := a*(rndseq mod (m div a))-(m mod a)*(rndseq div (m div a));
  if gamma > 0 then rndseq := gamma else rndseq := gamma+m;
  random := rndseq div (m div (hi-low+1))+low
END {of random};

FUNCTION distance (pos1x, pos1y : sectrange; pos2 : sectxy) : INTEGER;
BEGIN
  distance := ROUND (SQRT (SQR (pos1x - pos2.x) + SQR (pos1y - pos2.y)));
END {of distance};

FUNCTION radians (degrees : INTEGER) : REAL;
BEGIN
  radians := degrees * 0.0174533;
END {of radians};

FUNCTION interval (number, minvalue, maxvalue : INTEGER) : INTEGER;
BEGIN
  IF number < minvalue THEN
    interval := minvalue
  ELSE
    IF number > maxvalue THEN
      interval := maxvalue
    ELSE
      interval := number;
END {of interval};

PROCEDURE reinitialize;
VAR ch : CHAR;
BEGIN
  curenergy := entenergy;
  curtorps := starttorps;
  FOR ch := mindevice TO maxdevice DO
    device[ch].downtime := 0;
END {of reinitialize};

PROCEDURE initialize;
VAR rnum, totalfedbase, i, j, k : INTEGER;
BEGIN
  device['0'].name := 'Warp Engines        ';
  device['1'].name := 'Short Range Sensors ';
  device['2'].name := 'Long Range Sensors  ';
  device['3'].name := 'Phaser Control      ';
  device['4'].name := 'Photon Tubes        ';
  device['5'].name := 'Damage Control      ';
  device['6'].name := 'History Computers   ';
  device['7'].name := 'Self Destruct       ';
  symbols[snothing] := nosym;
  symbols[sstar] := starsym;
  symbols[senterprise] := entsym;
  symbols[snova] := novasym;
  symbols[sklingon] := klingsym;
  symbols[sfedbase] := fbasesym;
  symbols[sklingbase] := kbasesym;
  condnames[cred]    := 'Red       ';
  condnames[cgreen]  := 'Green     ';
  condnames[cyellow] := 'Yellow    ';
  condnames[cblack]  := 'Black     ';
  condnames[cdocked] := 'Docked    ';
  cursect.x := random (0, quadsize);
  cursect.y := random (0, quadsize);
  curquad.x := random (0, galaxysize);
  curquad.y := random (0, galaxysize);
  totalklingons := 0;
  totalkbases := 0;
  totalfedbase := 0;
  FOR i := 0 TO galaxysize DO
    FOR j := 0 TO galaxysize DO
      WITH galaxy[i, j] DO
        BEGIN
          ishistory := FALSE;
          rnum := random (0, SQR (galaxysize));
          IF random (0, SQR (galaxysize)) <= 6 THEN
            klingbasenum := 1
          ELSE
            klingbasenum := 0;
          totalkbases := totalkbases + klingbasenum;
          { note: this calculation was overflowing }
          repeat
             k := TRUNC (EXPp (-random (0, galaxysize)) * maxdigit) DIV 2
          until k <= maxdigit;
          klingnum := k;
          totalklingons := totalklingons + klingnum;
          IF random (0, SQR (galaxysize)) < 3 THEN
            fedbasenum := 1
          ELSE
            fedbasenum := 0;
          totalfedbase := totalfedbase + fedbasenum;
          starnum := random (0, quadsize);
        END {of WITH};
  startklingons := totalklingons;
  IF totalfedbase = 0 THEN
    galaxy[random (0, galaxysize), random (0, galaxysize)].fedbasenum := 1;
  IF totalkbases = 0 THEN
    BEGIN
      galaxy[random (0, galaxysize), random (0, galaxysize)].klingbasenum
                                                                          := 1;
      totalkbases := 1;
    END {of IF};
  curyear := random (3000, 4000);
  startyear := curyear;
  endyear := startyear + random (10, 40);
  badpoints := 0;
  bell := CHR (alarm);
  reinitialize;
  { initalize items that cause uninit errors [sam] }
  for k := 0 to maxklingons do with klingons[k] do begin
    position.x := 0;
    position.y := 0;
    energyleft := 0
  end
END {of initialize};

PROCEDURE setcondition;
VAR i, j : INTEGER;
BEGIN
  IF galaxy[curquad.x, curquad.y].klingbasenum <> 0 THEN
    condition := cblack
  ELSE
    IF galaxy[curquad.x, curquad.y].klingnum <> 0 THEN
      condition := cred
    ELSE
      IF curenergy < entenergy DIV 10 THEN
        condition := cyellow
      ELSE
        condition := cgreen;
  FOR i := cursect.x - 1 TO cursect.x + 1 DO
    FOR j := cursect.y - 1 TO cursect.y + 1 DO
      IF quadrant[interval (i, 0, quadsize), interval (j, 0, quadsize)] =
         sfedbase THEN
        condition := cdocked;
END {of setcondition};

PROCEDURE klingonattack;
VAR hit, i : INTEGER;
    shiptype : packed array [1..8] of char;
BEGIN
  WITH galaxy[curquad.x, curquad.y] DO
    IF (klingbasenum <> 0) OR (klingnum <> 0) THEN
      BEGIN
        IF condition = cdocked THEN
          WRITELN ('Starbase shields protect the Enterprise')
        ELSE
          FOR i := 0 TO maxklingons DO
            WITH klingons[i] DO
              IF energyleft > 0 THEN
                BEGIN
                  hit := TRUNC (energyleft /
                                distance (position.x, position.y, cursect) *
                                (10 + random (0, 10)) / 10);
                  curenergy := curenergy - hit;
                  IF energyleft = entenergy THEN
                    shiptype := 'Starbase'
                  ELSE
                    shiptype := '        ';
                  WRITE(hit:1, ' unit hit on Enterprise from Klingon ');
                  if shiptype[1] <> ' ' then write(shiptype, ' ');
                  writeln('at sector ', position.x:1, '-', position.y:1,
                          ' (', curenergy:1, ' left)');
                END {of IF energyleft};
      END {of IF (};
END {of klingonattack};

PROCEDURE printdigit (number : INTEGER; VAR mustprint : BOOLEAN);
BEGIN
  mustprint := mustprint OR (number <> 0);
  IF mustprint THEN
    WRITE (number:1)
  ELSE
    WRITE (' ');
END {of printdigit};

PROCEDURE setupquad (quad : quadxy; VAR entsect : sectxy);
VAR i, j, novacount, klingindex : INTEGER;

  PROCEDURE setupstuff (object : objects; count : INTEGER);
  VAR x, y : INTEGER;
  BEGIN
    WHILE count <> 0 DO
      BEGIN
        REPEAT
          x := random (0, quadsize);
          y := random (0, quadsize);
        UNTIL quadrant[x, y] = snothing;
        quadrant[x, y] := object;
        count := count - 1;
      END {of WHILE};
  END {of setupstuff};

BEGIN
  FOR i := 0 TO quadsize DO
    FOR j := 0 TO quadsize DO
      quadrant[i, j] := snothing;
  entsect.x := random (0, quadsize);
  entsect.y := random (0, quadsize);
  quadrant[entsect.x, entsect.y] := senterprise;
  WITH galaxy[quad.x, quad.y] DO
    BEGIN
      novacount := random (0, starnum DIV 2);
      setupstuff (sstar, starnum - novacount);
      setupstuff (snova, novacount);
      setupstuff (sklingon, klingnum);
      setupstuff (sfedbase, fedbasenum);
      setupstuff (sklingbase, klingbasenum);
    END {of WITH};
  klingindex := 0;
  FOR i := 0 TO quadsize DO
    FOR j := 0 TO quadsize DO
      IF quadrant[i, j] IN [sklingon, sklingbase] THEN
        WITH klingons[klingindex] DO
          BEGIN
            position.x := i;
            position.y := j;
            IF quadrant[i, j] = sklingbase THEN
              energyleft := entenergy
            ELSE
              energyleft := klingenergy;
            klingindex := klingindex + 1;
          END {of WITH};
  FOR klingindex := klingindex TO maxklingons DO
    klingons[klingindex].energyleft := 0;
END {of setupquad};

PROCEDURE printquadrant;
VAR i, j : quadrange;
BEGIN
  setcondition;
  IF device['1'].downtime <> 0 THEN
    WRITELN ('*** Short Range Sensors Inoperable ***')
  ELSE
    BEGIN
      WRITELN ('----------------------');
      FOR i := 0 TO quadsize DO
        BEGIN
          FOR j := 0 TO quadsize DO
            WRITE (symbols[quadrant[i, j]], ' ');
          WRITE ('   ');
          CASE i OF
            0 : WRITELN ('Stardate         ', curyear:1);
            1 : WRITELN ('Condition        ', condnames[condition]);
            2 : WRITELN ('Quadrant         ', curquad.x:1, '-', curquad.y:1);
            3 : WRITELN ('Sector           ', cursect.x:1, '-', cursect.y:1);
            4 : WRITELN ('Energy           ', curenergy:1);
            5 : WRITELN ('Photon torpedoes ', curtorps:1);
            6 : WRITELN ('Klingons left    ', totalklingons:1);
            7 : WRITELN;
          END {of CASE};
        END {of FOR i};
      WRITELN ('----------------------');
    END {of ELSE};
END {of printquadrant};

PROCEDURE printgalaxy (topx, lefty : INTEGER; size : INTEGER;
                        markhistory : BOOLEAN);
VAR i, j : INTEGER;
    mustprint : BOOLEAN;

  PROCEDURE printseparator (entries : INTEGER);
  VAR count : INTEGER;
  BEGIN
    FOR count := 0 TO entries DO
      WRITE ('|-----');
    WRITELN ('|');
  END {of printseparator};

BEGIN
  IF markhistory THEN
    WRITELN ('Long Range Sensor Scan For Quadrant ', curquad.x:1, '-',
             curquad.y:1)
  ELSE
    BEGIN
      WRITELN ('History Computer Report; Stardate ', curyear:1);
      IF condition <> cdocked THEN
        curenergy := curenergy - 100;
    END {of ELSE};
  printseparator (size);
  FOR i := topx TO topx + size DO
    BEGIN
      FOR j := lefty TO lefty + size DO
        IF (i IN [0..quadsize]) AND (j IN [0..quadsize]) THEN
          WITH galaxy[i, j] DO
            IF markhistory OR ishistory THEN
              BEGIN
                ishistory := ishistory OR (device['6'].downtime = 0);
                mustprint := FALSE;
                WRITE ('|');
                printdigit (klingbasenum, mustprint);
                printdigit (klingnum, mustprint);
                printdigit (fedbasenum, mustprint);
                mustprint := TRUE;
                printdigit (starnum, mustprint);
                WRITE (' ');
              END {of WITH}
            ELSE
              WRITE ('|+++++')
        ELSE
          WRITE ('|xxxxx');
      WRITELN ('|');
      printseparator (size);
    END {of FOR i};
END {of printgalaxy};

PROCEDURE printdamage;
VAR ch : CHAR;
BEGIN
  WRITELN ('Device name:      Repair Time:');
  FOR ch := mindevice TO maxdevice DO
    WRITELN (device[ch].name:20, device[ch].downtime:5);
END {of printdamage};

PROCEDURE moveenterprise;
VAR course : INTEGER;
    xinc, yinc, xpos, ypos, warp : REAL;

  PROCEDURE handledamage;
  VAR ch, startch : CHAR;
  BEGIN
    FOR ch := mindevice TO maxdevice DO
      IF device[ch].downtime <> 0 THEN
        device[ch].downtime := device[ch].downtime - 1;
    IF random (0, 100) < 6 THEN
      BEGIN
        ch := CHR (random (ORD (mindevice), ORD (maxdevice)));
        WRITELN ('*** Space storm, ', device[ch].name, ' damaged ***');
        device[ch].downtime := random (device[ch].downtime, 5);
      END {of IF}
    ELSE
      IF random (0, 100) < 12 THEN
        BEGIN
          ch := CHR (random (ORD (mindevice), ORD (maxdevice)));
          startch := ch;
          REPEAT
            IF ch = maxdevice THEN
              ch := mindevice
            ELSE
              ch := SUCC (ch);
          UNTIL (ch = startch) OR (device[ch].downtime <> 0);

          IF device[ch].downtime <> 0 THEN
            BEGIN
              WRITELN ('*** Truce, ', device[ch].name,
                       ' state of repair improved ***');
              device[ch].downtime := random (0, device[ch].downtime - 1);
            END {of IF device};
        END {of IF random};
  END {of handledamage};

  PROCEDURE moveintra (VAR xpos, ypos, xinc, yinc : REAL;
                        course : INTEGER; warp : REAL);
  BEGIN
    xinc := -COS (radians (course));
    yinc := SIN (radians (course));
    xpos := cursect.x;
    ypos := cursect.y;
    WHILE (ROUND (xpos) IN [0..quadsize]) AND
          (ROUND (ypos) IN [0..quadsize]) AND (warp >= 0.125) DO
      IF quadrant[ROUND (xpos), ROUND (ypos)] = snothing THEN
        BEGIN
          xpos := xpos + xinc;
          ypos := ypos + yinc;
          warp := warp - 0.125;
        END {of IF}
      ELSE
        warp := 0.0;
  END {of moveintra};

BEGIN {of moveenterprise}
  WRITE ('Course: ');
  READLN (course);
  WRITE ('Warp factor (0-12): ');
  READLN (warp);
  IF (warp < 0.0) OR (warp > 12.0) OR
     ((warp > 0.2) AND (device[mindevice].downtime <> 0)) THEN
    WRITELN ('Can''t move that fast !!')
  ELSE
    BEGIN
      curyear := curyear + 1;
      curenergy := TRUNC (curenergy - 8 * warp);
      handledamage;
      quadrant[cursect.x, cursect.y] := snothing;
      moveintra (xpos, ypos, xinc, yinc, course, warp);
      IF (ROUND (xpos) IN [0..quadsize]) AND
                                         (ROUND (ypos) IN [0..quadsize]) THEN
        IF quadrant[ROUND (xpos), ROUND (ypos)] = sfedbase THEN
          BEGIN
            WRITELN ('Collision with starbase''s elastic shields at sector ',
                     ROUND (xpos):1, '-', ROUND (ypos):1);
            moveintra (xpos, ypos, xinc, yinc, (course + 180) MOD 360, warp);
          END {of IF};
      IF (ROUND (xpos) IN [0..quadsize]) AND
                                         (ROUND (ypos) IN [0..quadsize]) THEN
        BEGIN
          IF quadrant[ROUND (xpos), ROUND (ypos)] IN
                                 [sstar, snova, sklingon, sklingbase] THEN
            BEGIN
              WRITELN ('Enterprise blocked by object at sector ', ROUND (xpos):1,
                       '-', ROUND (ypos):1);
              xpos := xpos - xinc;
              ypos := ypos - yinc;
            END {of IF quadrant};
          cursect.x := interval (ROUND (xpos), 0, quadsize);
          cursect.y := interval (ROUND (ypos), 0, quadsize);
          quadrant[cursect.x, cursect.y] := senterprise;
        END {of IF ROUND}
      ELSE
        BEGIN           {Inter-Quadrant moving}
          curquad.x := interval (TRUNC (curquad.x + warp * xinc +
                                         cursect.x * 0.125), 0, galaxysize);
          curquad.y := interval (TRUNC (curquad.y + warp * yinc +
                                         cursect.y * 0.125), 0, galaxysize);
          setupquad (curquad, cursect);
        END {of IF};
    END {of ELSE};
  setcondition;
  IF condition = cdocked THEN
    reinitialize;
END {of moveenterprise};

PROCEDURE firephasers;
VAR i, fireamount, hit : INTEGER;
BEGIN
  WRITELN ('Phasers locked on target.  Energy available = ', curenergy:1);
  WRITE ('Number of units to fire: ');
  READLN (fireamount);
  IF fireamount > curenergy THEN
    WRITELN ('Unable to fire.')
  ELSE
    IF fireamount > 0 THEN
      BEGIN
        IF condition <> cdocked THEN
          curenergy := curenergy - fireamount;
        FOR i := 0 TO maxklingons DO
          WITH klingons[i] DO
            IF energyleft > 0 THEN
              BEGIN
                hit := TRUNC (fireamount /
                              distance (position.x, position.y, cursect) *
                              (10 + random (0, 10))) DIV 10;
                energyleft := energyleft - hit;
                WRITE (hit, ' unit hit on Klingon at sector ', position.x:1, '-',
                       position.y:1);
                IF energyleft > 0 THEN
                  WRITELN (' (', energyleft:1, ' left)')
                ELSE
                  BEGIN
                    WRITELN ('.  Klingon DESTROYED', bell);
                    totalklingons := totalklingons - 1;
                    galaxy[curquad.x, curquad.y].klingnum :=
                                  galaxy[curquad.x, curquad.y].klingnum - 1;
                    quadrant[position.x, position.y] := snothing;
                  END {of ELSE};
              END {of IF energyleft}
      END {of IF >};
END {of firephasers};

PROCEDURE firetorpedoes;
VAR i, course : INTEGER;
    hitsomething : BOOLEAN;
    xinc, yinc, xpos, ypos : REAL;

  PROCEDURE hitnova (novax, novay : sectrange; VAR klingnum : digits);
  VAR hit, i : INTEGER;
  BEGIN
    WRITELN ('Torpedo causes unstable star to nova');
    IF condition <> cdocked THEN
      BEGIN
        hit := 600 * random (0, 10) DIV distance (novax, novay, cursect);
        IF hit > 0 THEN
          WRITELN ('Enterprise loses ', hit:1, ' units of energy');
        curenergy := curenergy - hit;
      END {of IF};
    FOR i := 0 TO maxklingons DO
      WITH klingons[i] DO
        IF energyleft > 0 THEN
          BEGIN
            energyleft := energyleft - 120 * random (0, 10) DIV
                             distance (novax, novay, position);
            IF energyleft <= 0 THEN
              BEGIN
                quadrant[position.x, position.y] := snothing;
                totalklingons := totalklingons - 1;
                klingnum := klingnum - 1;
              END {of IF <=};
          END {of IF >};
  END {of hitnova};

  PROCEDURE hitklingbase (VAR klingbasenum : digits);
  VAR i, kdocked : INTEGER;
      quadx, quady : quadrange;
  BEGIN
    WRITELN ('*** Klingon Starbase DESTROYED ***', bell);
    klingbasenum := klingbasenum - 1;
    kdocked := 0;
    FOR i := 1 TO random (0, SQR (galaxysize)) DO
      BEGIN
        REPEAT
          quadx := random (0, galaxysize);
          quady := random (0, galaxysize);
        UNTIL (quadx <> curquad.x) OR (quady <> curquad.y);
        kdocked := kdocked + galaxy[quadx, quady].klingnum;
        galaxy[quadx, quady].klingnum := 0;
      END {of FOR};
    WRITELN (kdocked:1, ' Klingons were killed while docked');
    totalklingons := totalklingons - kdocked;
  END {of hitklingbase};

BEGIN {of firetorpedoes}
  IF curtorps = 0 THEN
    WRITELN ('All photon torpedoes expended.')
  ELSE
    BEGIN
      WRITE ('Torpedo course: ');
      READLN (course);
      IF condition <> cdocked THEN
        curtorps := curtorps - 1;
      xinc := -COS (radians (course));
      yinc := SIN (radians (course));
      xpos := cursect.x;
      ypos := cursect.y;
      hitsomething := FALSE;
      WRITELN ('Torpedo track:');
      WITH galaxy[curquad.x, curquad.y] DO
        WHILE NOT hitsomething AND (ROUND (xpos) IN [0..quadsize]) AND
              (ROUND (ypos) IN [0..quadsize]) DO
          CASE quadrant[ROUND (xpos), ROUND (ypos)] OF
            senterprise,
            snothing    : BEGIN
                             WRITELN (ROUND (xpos):1, '-', ROUND (ypos):1);
                             xpos := xpos + xinc;
                             ypos := ypos + yinc;
                           END {of snothing};
            sstar       : BEGIN
                             hitsomething := TRUE;
                             WRITELN ('Star destroyed, you got the planets, ',
                                      'too!  Nice shot!');
                             badpoints := badpoints + random (0, 500);
                             starnum := starnum - 1;
                           END {of sstar};
            snova       : BEGIN
                             hitsomething := TRUE;
                             starnum := starnum - 1;
                             hitnova (ROUND (xpos), ROUND (ypos), klingnum);
                           END {of snova};
            sklingon    : BEGIN
                             hitsomething := TRUE;
                             WRITE ('*** Klingon DESTROYED ***', bell);
                             IF random (0, 100) < 30 THEN
                               WRITE (' (The only good Klingon is a dead',
                                      ' Klingon)');
                             WRITELN;
                             klingnum := klingnum - 1;
                             totalklingons := totalklingons - 1;
                             FOR i := 0 TO maxklingons DO
                               WITH klingons[i] DO
                                 IF (energyleft > 0) AND
                                    (ROUND (xpos) = position.x) AND
                                    (ROUND (ypos) = position.y) THEN
                                   energyleft := 0;
                           END {of sklingon};
            sfedbase   : BEGIN
                             hitsomething := TRUE;
                             WRITELN ('*** Starbase destroyed ... Not good ***');
                             badpoints := badpoints + random (0, 500);
                             fedbasenum := fedbasenum - 1;
                           END {of sfedbase};
            sklingbase : BEGIN
                             hitsomething := TRUE;
                             hitklingbase (klingbasenum);
                             totalkbases := totalkbases - 1;
                           END {of sklingbase};
          END {of CASE};
      IF hitsomething THEN
        quadrant[ROUND (xpos), ROUND (ypos)] := snothing
      ELSE
        WRITELN ('Torpedo missed.');
    END {of ELSE};
END {of firetorpedoes};

PROCEDURE selfdestruct;
VAR ch : CHAR;
BEGIN
  REPEAT
    WRITE ('Are you SURE ? ');
    READLN (ch);
    WRITELN;
  UNTIL ch IN ['y', 'Y', 'n', 'N'];
  IF ch IN ['y', 'Y'] THEN
    goto 99
END {of selfdestruct};

PROCEDURE command;
VAR ch : CHAR;
    validcommand : BOOLEAN;
BEGIN
  REPEAT
    WRITE ('Command: ');
    READLN (ch);
    WRITELN;
    validcommand := ch IN [mindevice..maxdevice];
    IF validcommand THEN
      BEGIN
        IF (device[ch].downtime <> 0) AND (ch > SUCC (mindevice)) THEN
          WRITELN ('*** ', device[ch].name, ' INOPERABLE ***')
        ELSE
          CASE ch OF
            '0' : moveenterprise;
            '1' : printquadrant;
            '2' : printgalaxy (curquad.x - 1, curquad.y - 1, 2, TRUE);
            '3' : firephasers;
            '4' : firetorpedoes;
            '5' : printdamage;
            '6' : printgalaxy (0, 0, galaxysize, FALSE);
            '7' : selfdestruct;
          END {of CASE};
      END {of IF}
    ELSE
      BEGIN
        WRITELN ('0 = Set course');
        WRITELN ('1 = Short range sensor scan');
        WRITELN ('2 = Long range sensor scan');
        WRITELN ('3 = Fire phasors');
        WRITELN ('4 = Fire photon torpedoes');
        WRITELN ('5 = Damage control report');
        WRITELN ('6 = History computer report');
        WRITELN ('7 = Self destruct');
      END {of ELSE};
  UNTIL validcommand;
  IF ch IN ['0', '3', '4'] THEN
    BEGIN
      klingonattack;
      printquadrant;
    END {of IF};
END {of command};

PROCEDURE instructions;
VAR ch : CHAR;

  PROCEDURE spacewait;
  BEGIN
    WRITELN;
    WRITE ('Hit <return> to continue');
    READLN;
    WRITELN;
  END {of spacewait};

  PROCEDURE page1;
  BEGIN
    WRITELN ('The galaxy is divided into 64 quadrants with the');
    WRITELN ('following coordinates:');
    WRITELN;
    WRITELN ('  0   1   2   3   4   5   6   7');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  0');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  1');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  2');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  3');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  4');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  5');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  6');
    WRITELN ('---------------------------------');
    WRITELN ('|   |   |   |   |   |   |   |   |  7');
    WRITELN;
    WRITELN ('Each quadrant is similarly divided into 64 sectors.');
    spacewait;
  END {of page1};

  PROCEDURE page2;
  BEGIN
    WRITELN;
    WRITELN ('::: DEVICES :::');
    WRITELN;
    WRITELN (' :: Warp Engines ::');
    WRITELN;
    WRITELN (' Course = a number in degrees.');
    WRITELN ('   Numbers indicate direction starting at the top and');
    WRITELN ('   going counter clockwise.');
    WRITELN;
    WRITELN ('                     0');
    WRITELN ('                 315 | 45');
    WRITELN ('                    \\|/');
    WRITELN ('               270 --*-- 90');
    WRITELN ('                    /|\\');
    WRITELN ('                 225 | 135');
    WRITELN ('                    180');
    WRITELN;
    WRITELN (' Warp Factor = a REAL number from 0 to 12.');
    WRITELN ('   Distance traveled = <warp factor> quadrants.');
    WRITELN ('     Warp  .2  =  The Enterprise travels 1 sector.');
    WRITELN ('           .5  =                         4 sectors.');
    WRITELN ('            1  =                         1 quadrant.');
    WRITELN ('            2  =                         2 quadrants.');
    spacewait;
  END {of page2};

  PROCEDURE page3;
  BEGIN
    WRITELN;
    WRITELN ('   For example, if you travel from quadrant 1-1 in the');
    WRITELN ('   direction of 90 degrees at warp 2, you would stop at');
    WRITELN ('   quadrant 1-3 in the next stardate.  NOTE: every use of');
    WRITELN ('   the warp engines takes one stardate.  If the Enterprise');
    WRITELN ('   is blocked by something during an intra-quadrant travel,');
    WRITELN ('   it will stop in front of it (and waste a stardate).');
    WRITELN;
    WRITELN (' :: Short Range Sensors ::');
    WRITELN;
    WRITELN ('  The short range sensors display a detailed view of the ');
    WRITELN ('  quadrant currently occupied by the Enterprise.  The ');
    WRITELN ('  The following symbols have meanings as follows:');
    WRITELN;
    WRITELN ('          Symbol          Meaning');
    WRITELN ('             ', nosym,    '            empty space');
    WRITELN ('             ', starsym,  '            a stable star');
    WRITELN ('             ', novasym,  '            an unstable star');
    WRITELN ('             ', entsym,   '            the Enterprise');
    WRITELN ('             ', fbasesym, '            a Federation base');
    WRITELN ('             ', klingsym, '            a Klingon ship');
    WRITELN ('             ', kbasesym, '            a Klingon base');
    spacewait;
  END {of page3};

  PROCEDURE page4;
  BEGIN
    WRITELN;
    WRITELN (' :: Long Range Sensors ::');
    WRITELN;
    WRITELN ('  The long range sensors display the objects in the nine');
    WRITELN ('  closest quadrants.  Each digit in each box means ');
    WRITELN ('  means something:');
    WRITELN;
    WRITELN ('    The ONES digit represents the number of STARS.');
    WRITELN ('        TENS                                FEDERATION BASES.');
    WRITELN ('        HUNDREDS                            KLINGON SHIPS');
    WRITELN ('        THOUSANDS                           KLINGON BASES');
    WRITELN;
    WRITELN ('  For example:');
    WRITELN ('    319 means 3 Klingons, 1 Federation base, and 9 stars.');
    WRITELN ('    206 means 2 Klingons, 0 Federation bases, and 6 stars.');
    WRITELN ('   1007 means 1 Klingon base and 7 stars.');
    WRITELN;
    WRITELN (' :: Phasers ::');
    WRITELN;
    WRITELN ('  Any portion of the energy available can be fired.  The');
    WRITELN ('  battle computer divides this amount among the Klingon');
    WRITELN ('  ships in the quadrant and determines the various directions');
    spacewait;
  END {of page4};

  PROCEDURE page5;
  BEGIN
    WRITELN;
    WRITELN ('  of fire.  The effectiveness of a hit depends mostly on the');
    WRITELN ('  distance to the target.  A Klingon battle cruiser starts with');
    WRITELN (klingenergy:5, ' units of energy.  It can fire an amount equal to');
    WRITELN ('  whatever energy is left.  Note that phasers are ineffective ');
    WRITELN ('  against stars, Klingon bases, and Federation bases.');
    WRITELN;
    WRITELN (' :: Photon Torpedoes ::');
    WRITELN;
    WRITELN ('  Initially the Enterprise has ', starttorps, ' photon torpedoes.');
    WRITELN ('  One torpedo destroys whatever it hits.  The range of the');
    WRITELN ('  photon torpedoes (like phasers) is limited to the current');
    WRITELN ('  quadrant.  The course of a photon torpedo is set the same');
    WRITELN ('  way as that of the Enterprise.  Torpedoes and phasers are');
    WRITELN ('  restocked when the Enterprise docks at a Federation base.');
    WRITELN;
    WRITELN (' :: Damage Control Report ::');
    WRITELN;
    WRITELN ('  The damage control report lists the state of repair of each');
    WRITELN ('  device.  A non-zero state indicates the number of stardates');
    WRITELN ('  required to repair the device.  Devices can be damaged by');
    WRITELN ('  space storms, and are repaired by time and truces.');
    spacewait;
  END {of page5};

  PROCEDURE page6;
  BEGIN
    WRITELN;
    WRITELN (' :: History Computers ::');
    WRITELN;
    WRITELN ('  The history computers keep a record of all the quadrants');
    WRITELN ('  scanned with the long range scanners.  The history report');
    WRITELN ('  uses the same display format as the long range scanners,');
    WRITELN ('  except that the entire galaxy is displayed.  A quadrant');
    WRITELN ('  that has not been scanned is printed as "+++++".');
    WRITELN;
    WRITELN (' :: Suicide Device ::');
    WRITELN;
    WRITELN ('  It is possible to implement a self-destruct sequence merely');
    WRITELN ('  by invoking this command.  The game is terminated.');
    WRITELN;
    WRITELN ('To get a list of all commands, type "9" when asked for a');
    WRITELN ('command.  All commands are terminated by the [RETURN] key.');
    WRITELN ('You have at least on supporting starbase.  Your energy and');
    WRITELN ('photon torpedoes are replenished when you are docked at a');
    WRITELN ('Federation starbase.  G O O D   L U C K !');
    WRITELN;
    spacewait;
  END {of page6};

BEGIN
  WRITELN ('Orders:  Stardate ', curyear:1);
  WRITELN;
  WRITELN ('As commander of the United Starship Enterprise,');
  WRITELN ('your mission is to rid the galaxy of the deadly');
  WRITELN ('Klingon menace.  To do this, you must destroy the ');
  WRITELN ('Klingon invasion force of ', totalklingons:1, ' battle cruisers.');
  WRITELN ('You have ', endyear - curyear + 1:1, ' solar years to complete');
  WRITELN ('your mission (i.e. until stardate ', endyear:1, ').  The ');
  WRITELN ('Enterprise is currently in quadrant ', curquad.x:1, '-',
           curquad.y:1, ', sector ', cursect.x:1, '-', cursect.y:1, '.');
  WRITELN;
  WRITE ('Do you need further instructions (y/n) ? ');
  READLN (ch);
  WRITELN;
  WRITELN;
  IF ch IN ['Y', 'y'] THEN
    BEGIN
      page1;
      page2;
      page3;
      page4;
      page5;
      page6;
      WRITELN;
      WRITELN;
    END {of IF};
END {of instructions};

PROCEDURE finishgame;
VAR rating : INTEGER;
BEGIN
  IF (curenergy <= 0) OR (curyear >= endyear) THEN
    BEGIN
      WRITELN ('It is stardate ', curyear:1, '.  The Enterprise has been');
      WRITELN ('destroyed.  The Federation will be conquered.  There');
      WRITELN ('are still ', totalklingons:1, ' Klingon battle cruisers.');
      WRITELN ('You are dead.');
    END {of IF}
  ELSE
    BEGIN
      rating := startklingons DIV (curyear - startyear) * 100;
      WRITELN ('It is stardate ', curyear:1, '.  The last Klingon battle');
      WRITELN ('cruiser in the galaxy has been destroyed.  The Federation');
      WRITE ('has been saved.  ');
      IF badpoints > rating THEN
        BEGIN
          WRITELN ('However, because of your wanton ');
          WRITELN ('destruction of Federation bases and planet systems,');
          WRITELN ('you have been thrown in the brig never to see the');
          WRITELN ('light of day again.');
        END {of IF badpoints}
      ELSE
        BEGIN
          WRITELN ('You are a hero and a new admiral.');
          WRITELN (startklingons:1, ' Klingons in ', curyear - startyear:1,
                   ' years gives a rating of ', rating:1);
        END {of ELSE badpoints};
    END {of ELSE};
  END {of finishgame};

BEGIN {of startrek}
  rndseq := 1;
  initialize;
  setupquad (curquad, cursect);
  setcondition;
  instructions;
  klingonattack;
  printquadrant;
  WHILE (curenergy > 0) AND (totalklingons > 0) AND
        (totalkbases > 0) AND (curyear <> endyear) DO
    command;
  finishgame;

  99:

END {of startrek}.

