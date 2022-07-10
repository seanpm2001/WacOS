10  rem
20  rem show distribution of random numbers
30  rem
35  print "Calculating distribution plot, wait..."
40  dim c%(80)
50  for n% = 1 to 1000: rem calculate a meaningfull number of samples
60     k% = 0: rem clear toss count
70     for i% = 1 to 79: rem calculate tosses
80        if int(2*rnd(1)) then k% = k%+1: rem toss the coin and count
90     next i%
100    c%(k%+1) = c%(k%+1)+1: rem tally counts
110 next n%
115 print "distribution for 1000 samples:": print
120 for y% = 150 to 0 step -10: rem run "strips" of the graph
130    for x% = 1 to 80: rem print characters
140       if c%(x%) >= y% then print "*"; else print " ";
150    next x%
160    print: rem next line
170 next y%
180 end
