10 REM GUESS - a number guessing game
20 CLS
30 RANDOMIZE
40 N=INT(RND*100)+1
50 T=0
60 PRINT "I am thinking of 1 to 100."
70 INPUT "Your guess";G
80 T=T+1
90 IF G<N THEN PRINT "Too low." : GOTO 70
100 IF G>N THEN PRINT "Too high." : GOTO 70
110 PRINT "Got it in";T;"tries!"
120 END
