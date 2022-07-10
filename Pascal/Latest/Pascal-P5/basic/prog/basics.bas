1000 rem
1010 rem BASIC SUBSET INTERPRETER
1020 rem
1030 print "Tiny Basic Vs. 1.0":print
1100 dim p$(100),s(100),v(25)
1110 print "Ready"
1115 input "",p$(0):l=0:c=1
1120 gosub 2400:if n=0 then 1180
1125 rem here we enter numbered program lines to store
1130 l=1:c=1:t=n
1140 gosub 2400:if (n<t) and (n<>0) then l=l+1:c=1:goto 1140
1150 if l=100 then print "*** Program overflow":goto 1110
1160 if t<>n then for i=99 to l step -1:p$(i)=p$(i-1):next i
1170 p$(l)=p$(0):goto 1115
1175 rem here we execute the next statement
1178 gosub 2400
1180 gosub 2470
1190 if l$<>"if" then 1230
1200 gosub 2030:if not n then c=len(p$(l)): goto 1180
1210 gosub 2470:if l$<>"then" then print """then"" expected": goto 1110
1220 goto 1180
1230 if l$="input" then gosub 2520:input n:v(v)=n:goto 1620
1280 if l$<>"print" then 1330
1290 gosub 2440:gosub 2460:if c$<>"""" then 1298
1292 c=c+1:gosub 2460:if c$="" then print "*** Unterminated string":goto 1110
1294 if c$<>"""" then print c$;:goto 1292
1296 c=c+1:gosub 2460:if c$="""" then print c$;:goto 1292
1297 goto 1300
1298 gosub 2030:print n;
1300 gosub 2440:gosub 2460:if c$="," then c=c+1:goto 1290
1310 gosub 2440:gosub 2460:if c$<>";" then print else c=c+1
1320 goto 1620
1330 if l$<>"goto" then 1370
1340 gosub 2400:l=0:c=1:t=n
1350 if l=100 then print "*** Line not found":goto 1110
1360 gosub 2400:if n=t then 1180 else l=l+1:c=1:goto 1350
1370 if l$="rem" then c=len(p$(l)):goto 1620
1380 if l$="stop" then 1110
1390 if l$<>"list" then 1500
1400 for i=1 to 99:if p$(i)<>"" then print p$(i) else 1620:next i
1500 if l$<>"new" then 1520
1510 for i=0 to 100:p$(i)="":next i:for i=0 to 25:v(i)=0:next i:goto 1110
1520 if l$="bye" then stop
1525 if l$="run" then for i=0 to 25:v(i)=0:next i:l=1:c=1:goto 1650
1530 if l$="let" then gosub 2470
1540 gosub 2530:gosub 2440:gosub 2460
1570 if c$<>"=" then print "*** ""="" expected":goto 1110
1580 c=c+1:gosub 2030:v(v)=n
1600 rem finish statement
1620 gosub 2440:gosub 2460: if c$=":" then c=c+1:goto 1180
1630 if c$<>"" then print "*** End of statement expected": goto 1110
1640 if l=0 then 1110 else l=l+1:c=1
1650 if p$(l)="" then 1110 else 1178
2010 rem expression processor
2030 s=0:gosub 2040:n=s(s):s=s-1:return
2040 gosub 2080:gosub 2440:gosub 2460
2050 if c$="+" then c=c+1:v=n:gosub 2080:s(s-1)=s(s-1)+s(s):s=s-1: return
2060 if c$="-" then c=c+1:v=n:gosub 2080:s(s-1)=s(s-1)-s(s):s=s-1
2070 return
2080 gosub 2120:gosub 2440:gosub 2460
2090 if c$="*" then c=c+1:v=n:gosub 2120:s(s-1)=s(s-1)*s(s):s=s-1: return
2100 if c$="/" then c=c+1:v=n:gosub 2120:s(s-1)=s(s-1)/s(s):s=s-1
2110 return
2120 gosub 2440:gosub 2460:if c$<>"(" then 2160
2130 c=c+1:gosub 2040:gosub 2440:gosub 2460
2140 if c$<>")" then print """)"" expected":goto 1110
2150 c=c+1:return
2160 if c$="" then print "*** Invalid factor":goto 1110
2170 t=asc(c$):if (t<asc("0")) or (t>asc("9")) then 2190
2180 gosub 2400:s=s+1:s(s)=n:return
2190 gosub 2520:s=s+1:s(s)=v(v):return
2305 rem get number to n, none=0
2400 gosub 2440:n=0
2410 gosub 2460:if c$="" then return
2420 if (asc(c$)<asc("0")) or (asc(c$)>asc("9")) then return
2430 n=n*10+asc(c$)-asc("0"):c=c+1:goto 2410
2435 rem skip spaces
2440 gosub 2460:if c$=" " then c=c+1:goto 2440
2450 return
2455 rem check next character to c$, ""=end of line
2460 if c>len(p$(l)) then c$="" else c$=mid$(p$(l), c, 1):return
2465 rem get next label to l$
2470 gosub 2440:gosub 2460:l$="":if c$="" then 2490
2480 if (asc(c$)>=asc("a")) and (asc(c$)<=asc("z")) then 2500
2490 print "*** Invalid label":goto 1110
2500 l$=l$+c$:c=c+1:gosub 2460:if c$="" then return
2510 if (asc(c$)>=asc("a")) and (asc(c$)<=asc("z")) then 2500 else return
2515 rem get next variable to v (0 to 25)
2520 gosub 2470
2530 v=asc(l$):if (len(l$)<>1) or (v<asc("a")) or (v>asc("z")) then 2550
2540 v=v-asc("a"):return
2550 print "*** Variable expected":goto 1110
2560 end
