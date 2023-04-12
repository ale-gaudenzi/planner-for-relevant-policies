#!/bin/bash


cd ~/planners/MyNDPlanner/translator-fond/

for i in {1..2}
do
	for j in {1..20}
	do
		python3.7 translate.py ~/planners/problems/zenotravel/domain${i}k.pddl ~/planners/problems/zenotravel/pfile${j}
		mv output.sas ~/planners/problems/zenotravel/sas/p${j}k${i}.sas
		echo "created p${j}k${i}"
	done
done