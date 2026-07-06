10 REM ART - a graphics demo (240x160, 4 colours)
20 CLS
30 REM a fan of lines from the corner
40 FOR X=0 TO 239 STEP 20
50 LINE (0,159)-(X,0),1
60 NEXT X
70 REM concentric circles
80 FOR R=8 TO 40 STEP 8
90 CIRCLE (180,110),R,3
100 NEXT R
110 REM a filled box with an outline
120 LINE (150,10)-(230,60),3,B
130 LINE (158,18)-(222,52),2,BF
140 END
