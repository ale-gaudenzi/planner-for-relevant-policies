(define (problem prob)
 (:domain barman)
 (:objects 
     shaker1 shaker2 shaker3 - shaker
     left left_b - hand_a
     right right_b - hand_b
     shot1 - shot
     ingredient3 ingredient4 - ingredient
     cocktail1 - cocktail
     dispenser2 dispenser1 dispenser3 dispenser4 - dispenser
     l0 l1 l2 l3 - level
)
 (:init 
  (ontable shaker1)
  (ontable shaker2)
  (ontable shaker3)
  (ontable shot1)
  (dispenses dispenser1 ingredient3)
  (dispenses dispenser1 ingredient4)
  (dispenses dispenser2 ingredient4)
  (clean shaker1)
  (clean shaker2)
  (clean shot1)
  (empty shaker1)
  (empty shaker2)
  (empty shot1)
  (handempty left)
  (handempty left_b)
  (handempty right)
  (handempty right_b)
  (shaker-empty-level shaker1 l0)
  (shaker-empty-level shaker2 l0)
  (shaker-level shaker1 l0)
  (shaker-level shaker2 l0)
  (next l0 l1)
  (next l1 l2)
  (next l2 l3)
  (cocktail-part1 cocktail1 ingredient3)
  (cocktail-part2 cocktail1 ingredient4)
)
 (:goal
  (and
     (contains shot1 cocktail1)
)))
