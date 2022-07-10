print "flip program"
2   for y = 1 to 10
5   let c = 0
10  for x = 1 to 50
20     let f = int(2*rnd(1))
30     if f = 1 then goto 60
40     print "T";
50     goto 100
58     rem c counts the number of heads
60     let c = c+1
70     print "H";
100 next x
110 print
120 print "heads "; c; " out of 50 flips"
125 next y
130 end
