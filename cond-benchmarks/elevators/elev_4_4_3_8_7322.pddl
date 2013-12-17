(define (problem elev_4_4_3_8_7322)
  (:domain elevators)
  (:objects f2 f3 f4 - floor p2 p3 p4 - pos e1 e2 e3 - elevator c1 c2 c3 c4 c5 c6 c7 c8 - coin)
  (:init (at f1 p1) (dec_f f2 f1) (dec_f f3 f2) (dec_f f4 f3) (dec_p p2 p1) (dec_p p3 p2) (dec_p p4 p3) (shaft e1 p4) (in e1 f2) (shaft e2 p4) (in e2 f3) (shaft e3 p2) (in e3 f4) (coin-at c1 f3 p4) (coin-at c2 f4 p2) (coin-at c3 f2 p1) (coin-at c4 f1 p2) (coin-at c5 f4 p4) (coin-at c6 f1 p3) (coin-at c7 f2 p3) (coin-at c8 f4 p3))
  (:goal (and (have c1) (have c2) (have c3) (have c4) (have c5) (have c6) (have c7) (have c8)))
)