#!/bin/bash
cd ~/assignment2
./sim --msgs 200 --interval 10 --loss 0.1 --corrupt 0.1 --seed 42 --out load_50 && cat load_50/summary.txt
./sim --msgs 200 --interval 6.7 --loss 0.1 --corrupt 0.1 --seed 42 --out load_75 && cat load_75/summary.txt
./sim --msgs 200 --interval 5 --loss 0.1 --corrupt 0.1 --seed 42 --out load_100 && cat load_100/summary.txt
./sim --msgs 200 --interval 4 --loss 0.1 --corrupt 0.1 --seed 42 --out load_125 && cat load_125/summary.txt
./sim --msgs 200 --interval 3.3 --loss 0.1 --corrupt 0.1 --seed 42 --out load_150 && cat load_150/summary.txt
./sim --msgs 200 --interval 2.9 --loss 0.1 --corrupt 0.1 --seed 42 --out load_175 && cat load_175/summary.txt
./sim --msgs 200 --interval 2.5 --loss 0.1 --corrupt 0.1 --seed 42 --out load_200 && cat load_200/summary.txt
