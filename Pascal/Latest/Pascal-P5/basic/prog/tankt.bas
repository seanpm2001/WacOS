10  rem ***********************************************************************
15  rem *                                                                     *
20  rem * Tank game, based on my 1978 original [sam]                          *
30  rem *                                                                     *
35  rem * Tank, sometimes referred to as "fox and hounds", consists of a      *
36  rem * player piece pitted against a number of enemy pieces that advance   *
37  rem * mindlessly in his direction.                                        *
38  rem * This version runs under the terminal level calls.                   *
39  rem *                                                                     *
40  rem ***********************************************************************
41  clear
51  print "******************************************************************************"
52  print
53  print "TANK GAME"
54  print
55  print "******************************************************************************"
60  print
61  print "You are playing against several tanks on a field. ""*"" marks the edges of the"
62  print "field, ""+"" mark enemy tanks, ""@"" marks your position, and ""#"" mark the"
63  print "position of tank traps. You can move in any direction on your turn, including"
64  print "the diagonals. After you move, each of the enemy tanks will also move, in your"
65  print "direction. If they hit you, you lose. If they hit a tank trap, they are out"
66  print "of the game. If all the enemy tanks die in this fashion, you win the game."
67  print "The keys for directions of movement are:": print
68  print "8 1 2"
69  print "7 0 3"
70  print "6 5 4 Where 0 means stay put, 2 means up and right, etc."
71  print: input "Hit return to continue"; a$
72  ascroll 0: rem turn off autoscroll
73  numtanks% = maxx%*maxy%*0.05: rem find %2 of board as tanks
74  numtraps% = maxx%*maxy%*0.07: rem find %10 of board as traps 
75  dim board%(maxx%, maxy%): rem playing field
80  rem establish playing field tolkens
90  space%  = 1: rem empty space
100 fence%  = 2: rem border
110 trap%   = 3: rem tank trap
120 tank%   = 4: rem enemy tank
125 tankm%  = 5: rem moved enemy tank
130 player% = 6: rem player tank
140 rem set up the board
150 for x% = 1 to maxx%: for y% = 1 to maxy%: board%(x%, y%) = space%: next y%: next x%
160 for x% = 1 to maxx%: board%(x%, 1) = fence%: next x% 
170 for x% = 1 to maxx%: board%(x%, maxy%) = fence%: next x% 
180 for y% = 1 to maxy%: board%(1, y%) = fence%: next y% 
190 for y% = 1 to maxy%: board%(maxx%, y%) = fence%: next y% 
200 rem place tank traps at random
210 for c% = 1 to numtraps%
220    repeat: x% = rnd(1)*maxx%+1: y% = rnd(1)*maxy%+1: until board%(x%, y%) = space%
230    board%(x%, y%) = trap%
240 next c%
250 rem place enemy tanks at random
260 for c% = 1 to numtanks%
270    repeat: x% = rnd(1)*maxx%+1: y% = rnd(1)*maxy%+1: until board%(x%, y%) = space%
280    board%(x%, y%) = tank%
290 next c%
300 rem place player
310 repeat: x% = rnd(1)*maxx%+1: y% = rnd(1)*maxy%+1: until board%(x%, y%) = space%
320 board%(x%, y%) = player%: playerx% = x%: playery% = y%
330 rem print out board
340 clear: curvis 0
350 for y% = 1 to maxy% 
360    for x% = 1 to maxx%: print mid$(" *#+?@", board%(x%, y%), 1);: next x%
380 next y%
400 rem
410 rem player move
420 rem
430 repeat: a$ = inkey$: until (a$ >= "0") and (a$ <= "8")
440 a% = val(a$)
460 rem find potential new player location
470 pplayerx% = playerx%
480 pplayery% = playery%
490 if (a% = 8) or (a% = 1) or (a% = 2) then pplayery% = pplayery%-1: rem move up
500 if (a% = 6) or (a% = 5) or (a% = 4) then pplayery% = pplayery%+1: rem move down
510 if (a% = 8) or (a% = 7) or (a% = 6) then pplayerx% = pplayerx%-1: rem move left
520 if (a% = 2) or (a% = 3) or (a% = 4) then pplayerx% = pplayerx%+1: rem move right
525 rem check occupied
530 if (board%(pplayerx%, pplayery%) <> space%) and (board%(pplayerx%, pplayery%) <> player%) then goto 430
540 board%(playerx%, playery%) = space%: rem erase old position
550 playerx% = pplayerx%: rem set new position
560 playery% = pplayery%
570 board%(playerx%, playery%) = player%
580 rem
590 rem enemy move
600 rem
610 for x% = 1 to maxx%
620    for y% = 1 to maxy%
630       if board%(x%, y%) <> tank% then goto 780
640       ptankx% = x%: rem find potential tank location
650       ptanky% = y%
660       if playerx% < ptankx% then ptankx% = ptankx%-1
670       if playerx% > ptankx% then ptankx% = ptankx%+1
680       if playery% < ptanky% then ptanky% = ptanky%-1
690       if playery% > ptanky% then ptanky% = ptanky%+1
700       if board%(ptankx%, ptanky%) <> player% then goto 750
710       clear: print "Killed by enemy tank !!!"
720       input "Play again (Yes/No)? ", a$
730       if left$(a$, 1) = "y" then goto 150
740       clear: ascroll 1: stop
750       if (board%(ptankx%, ptanky%) <> space%) and (board%(ptankx%, ptanky%) <> trap%) then goto 780
760       board%(x%, y%) = space%: rem erase old position
762       rem tank is dead
764       if board%(ptankx%, ptanky%) = trap% then goto 780
770       board%(ptankx%, ptanky%) = tankm%: rem place new
780    next y%
790 next x%
800 rem now convert moved tanks to real tanks
810 for x% = 1 to maxx%: for y% = 1 to maxy% 
820    if board%(x%, y%) = tankm% then board%(x%, y%) = tank%
830 next y%: next x%
835 rem count remaining tanks
836 tanks% = 0
837 for x% = 1 to maxx%: for y% = 1 to maxy% 
838    if board%(x%, y%) = tank% then tanks% = tanks%+1
839 next y%: next x%
840 if tanks% > 0 then goto 340: rem go print board
850 clear: print "You killed them all !!!"
860 input "Play again (Yes/No)? ", a$
870 if left$(a$, 1) = "y" then goto 150
871 clear: ascroll 1
880 end
